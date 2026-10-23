#include "chatstore.h"
#include "common/localdataprotection.h"
#include "common/localstoresecurity.h"
#include "runtimepaths.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRunnable>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <algorithm>
#include <stdexcept>

namespace {
constexpr int kMaxAttachmentTasks = 32;
constexpr qint64 kMaxAttachmentBytes = 64 * 1024 * 1024;
constexpr qint64 kMaxAttachmentSize = 20 * 1024 * 1024;
const QString kDatabaseRelativePath = QStringLiteral("sql/db.sqlite");

QString chatDataRoot()
{
  if (!RuntimePaths::dataDirectory().isEmpty())
    return RuntimePaths::dataDirectory();
  QDir directory(QCoreApplication::applicationDirPath());
  QString root = directory.filePath("chat_data");
  // 开发运行时定位项目根目录，让数据与 build 目录分开。
  // 用客户端 CMake 入口及 main.cpp 区分客户端与上层服务端工程。
  // 发布目录没有客户端源码时，使用 EXE 同级目录。
  do {
    if (QFileInfo(directory.filePath("CMakeLists.txt")).isFile() &&
        QFileInfo(directory.filePath("app/main.cpp")).isFile()) {
      root = directory.filePath("chat_data");
      break;
    }
  } while (directory.cdUp());
  return root;
}
void checked(bool success, const QString &error)
{
  if (!success)
    throw std::runtime_error(error.toStdString());
}

void run(QSqlQuery &query)
{
  const bool success = query.exec();
  checked(success, query.lastError().text());
}

void sql(QSqlDatabase &database, const QString &statement)
{
  QSqlQuery query(database);
  const bool success = query.exec(statement);
  checked(success, query.lastError().text());
}

// 首次打开新目录时保留旧账号数据；快照会包含已提交到 WAL 的记录。
// 临时目录完整落盘后才改名为正式账号目录，失败时仍可从旧目录重试。
void migrateLegacyAccount(const QStringList &legacyDirectories, const QString &directory)
{
  if (QFile::exists(QDir(directory).filePath(kDatabaseRelativePath)))
    return;
  QString legacyDirectory;
  for (const auto &candidate : legacyDirectories) {
    if (QFile::exists(candidate + "/chat.db")) {
      legacyDirectory = candidate;
      break;
    }
  }
  if (legacyDirectory.isEmpty())
    return;
  const bool sameDirectory = QDir(legacyDirectory).absolutePath() == QDir(directory).absolutePath();

  const QString parentPath = QFileInfo(directory).absolutePath();
  checked(QDir().mkpath(parentPath), QStringLiteral("无法创建聊天数据目录：%1").arg(parentPath));
  QLockFile migrationLock(directory + ".migration.lock");
  checked(migrationLock.tryLock(), QStringLiteral("该账号正在迁移聊天记录，请稍后重新登录。"));
  if (QFile::exists(QDir(directory).filePath(kDatabaseRelativePath)))
    return;

  QTemporaryDir staging(parentPath + "/.chat-migration-XXXXXX");
  checked(staging.isValid(), QStringLiteral("无法创建聊天记录迁移临时目录：%1").arg(parentPath));
  checked(QDir().mkpath(staging.filePath("sql")), "Cannot create migrated SQL directory");
  const QString connectionName =
      "cppchat-migration-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  try {
    auto source = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    source.setDatabaseName(legacyDirectory + "/chat.db");
    source.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000");
    const bool opened = source.open();
    checked(opened, QStringLiteral("无法读取原聊天数据库：%1").arg(source.lastError().text()));
    sql(source, "PRAGMA synchronous=FULL");
    QSqlQuery snapshot(source);
    snapshot.prepare("VACUUM main INTO ?");
    snapshot.addBindValue(staging.filePath(kDatabaseRelativePath));
    run(snapshot);
  } catch (...) {
    QSqlDatabase::removeDatabase(connectionName);
    throw;
  }
  QSqlDatabase::removeDatabase(connectionName);

  checked(QDir().mkpath(staging.filePath("attachments")),
          "Cannot create migrated attachment directory");
  const QFileInfo attachmentDirectory(legacyDirectory + "/attachments");
  if (!sameDirectory && attachmentDirectory.exists()) {
    checked(attachmentDirectory.isDir() && !attachmentDirectory.isSymLink(),
            QStringLiteral("原图片缓存目录异常，已保留原数据，请检查：%1")
                .arg(attachmentDirectory.filePath()));
    const auto files =
        QDir(attachmentDirectory.filePath())
            .entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const auto &file : files) {
      checked(file.isFile() && !file.isSymLink(),
              QStringLiteral("图片缓存中存在无法迁移的文件：%1").arg(file.filePath()));
      const QString destination = staging.filePath("attachments/" + file.fileName());
      checked(
          QFile::copy(file.filePath(), destination) && QFileInfo(destination).size() == file.size(),
          QStringLiteral("无法复制原图片缓存，请检查磁盘空间及目录权限：%1").arg(file.fileName()));
    }
  }

  // 同一账号目录只更新数据库布局，附件保持原位置，旧数据库保留。
  if (sameDirectory) {
    const QString sqlDirectory = QDir(directory).filePath("sql");
    if (QDir(sqlDirectory).exists())
      checked(QDir().rmdir(sqlDirectory), "Target SQL directory is not empty");
    checked(QDir().rename(staging.filePath("sql"), sqlDirectory),
            QStringLiteral("无法完成聊天数据库布局迁移：%1").arg(directory));
    qInfo() << "Local chat database layout migrated:" << directory;
    return;
  }

  // 不覆盖新位置已有的数据；只允许移除此前留下的空目录。
  QDir target(directory);
  if (target.exists()) {
    checked(target.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)
                .isEmpty(),
            QStringLiteral("目标聊天目录已有文件，无法自动迁移：%1").arg(directory));
    checked(QDir().rmdir(directory), QStringLiteral("无法移除空的目标目录：%1").arg(directory));
  }
  checked(QDir().rename(staging.path(), directory),
          QStringLiteral("无法完成聊天记录迁移：%1").arg(directory));
  staging.setAutoRemove(false);
  qInfo() << "Local chat account copied from:" << legacyDirectory << "to:" << directory;
}

class Transaction {
public:
  explicit Transaction(QSqlDatabase &database) : _database(database)
  {
    const bool success = database.transaction();
    checked(success, database.lastError().text());
  }
  ~Transaction()
  {
    if (!_committed)
      _database.rollback();
  }
  void Commit()
  {
    const bool success = _database.commit();
    checked(success, _database.lastError().text());
    _committed = true;
  }

private:
  QSqlDatabase &_database;
  bool _committed = false;
};

const QString kMessageColumns = QStringLiteral(
    "m.thread_id,m.sender_uid,COALESCE(m.server_id,0),COALESCE(m.unique_id,''),"
    "m.content,m.chat_time,m.message_type,m.server_status,m.send_state,NULL,m.local_id");

StoredMessage readMessage(const QSqlQuery &query)
{
  StoredMessage message;
  message.localId = query.value(10).toLongLong();
  message.threadId = query.value(0).toInt();
  message.senderUid = query.value(1).toInt();
  message.serverId = query.value(2).toInt();
  message.uniqueId = query.value(3).toString();
  message.content = query.value(4).toString();
  message.time = query.value(5).toString();
  message.type = query.value(6).toInt();
  message.status = message.serverId == 0 ? SEND_FAILED : query.value(7).toInt();
  message.localImage = query.value(9).toByteArray();
  return message;
}

enum class SaveResult { Existing, New, Suppressed };

SaveResult saveMessage(QSqlDatabase &database, const StoredMessage &message)
{
  checked(message.threadId > 0 && message.senderUid > 0 &&
              (message.serverId > 0 || !message.uniqueId.isEmpty()),
          "Invalid message identity");
  QSqlQuery boundary(database);
  boundary.prepare("SELECT hidden_before_id FROM conversations WHERE thread_id=?");
  boundary.addBindValue(message.threadId);
  run(boundary);
  checked(boundary.next(), "Message conversation is missing");
  if (message.serverId > 0 && message.serverId <= boundary.value(0).toInt()) {
    // 清空发生在回执到达之前时，仍须核销对应的本机待确认记录。
    QSqlQuery remove(database);
    remove.prepare("DELETE FROM messages WHERE thread_id=? AND sender_uid=? AND unique_id=? AND "
                   "server_id IS NULL");
    remove.addBindValue(message.threadId);
    remove.addBindValue(message.senderUid);
    remove.addBindValue(message.uniqueId);
    run(remove);
    return SaveResult::Suppressed;
  }
  if (message.serverId > 0) {
    QSqlQuery deleted(database);
    deleted.prepare("SELECT 1 FROM deleted_messages WHERE thread_id=? AND server_id=?");
    deleted.addBindValue(message.threadId);
    deleted.addBindValue(message.serverId);
    run(deleted);
    if (deleted.next())
      return SaveResult::Suppressed;
  }
  // 先按 UUID 合并发送回执，再按服务端 ID 合并历史。空 UUID 不参与唯一约束。
  QSqlQuery find(database);
  find.prepare("SELECT local_id,server_id FROM messages WHERE thread_id=? AND "
               "((server_id=? AND ?>0) OR (sender_uid=? AND unique_id=? AND ?<>'')) "
               "ORDER BY server_id IS NOT NULL,local_id");
  find.addBindValue(message.threadId);
  find.addBindValue(message.serverId);
  find.addBindValue(message.serverId);
  find.addBindValue(message.senderUid);
  find.addBindValue(message.uniqueId);
  find.addBindValue(message.uniqueId);
  run(find);
  const bool exists = find.next();
  const auto localId = exists ? find.value(0) : QVariant();
  const bool confirmed = exists && !find.value(1).isNull();
  QVector<QVariant> duplicates;
  while (exists && find.next())
    duplicates.append(find.value(0));
  find.finish();
  if (confirmed && message.serverId <= 0)
    return SaveResult::Existing; // 延迟的上传/超时回调不能把已确认消息改回待发送。
  for (const auto &duplicate : duplicates) {
    QSqlQuery remove(database);
    remove.prepare("DELETE FROM messages WHERE local_id=?");
    remove.addBindValue(duplicate);
    run(remove);
  }
  QSqlQuery query(database);
  query.prepare(
      exists
          ? "UPDATE messages SET server_id=COALESCE(?,server_id),unique_id=COALESCE(?,unique_id),"
            "content=?,message_type=?,chat_time=?,server_status=?,"
            "send_state=CASE WHEN server_id IS NOT NULL OR ?=1 THEN 1 ELSE ? END,"
            "local_image=CASE WHEN ? IS NULL THEN local_image ELSE ? END WHERE local_id=?"
          : "INSERT INTO messages(server_id,unique_id,content,message_type,chat_time,server_status,"
            "send_state,local_image,thread_id,sender_uid) VALUES(?,?,?,?,?,?,?,?,?,?)");
  const int state = message.serverId > 0 ? 1 : message.status == SEND_FAILED ? 2 : 0;
  query.addBindValue(message.serverId > 0 ? QVariant(message.serverId) : QVariant());
  query.addBindValue(message.uniqueId.isEmpty() ? QVariant() : QVariant(message.uniqueId));
  query.addBindValue(message.content.isNull() ? QStringLiteral("") : message.content);
  query.addBindValue(message.type);
  query.addBindValue(message.time.isNull() ? QStringLiteral("") : message.time);
  query.addBindValue(message.status);
  if (exists) {
    query.addBindValue(state == 1 ? 1 : 0);
    query.addBindValue(state);
    query.addBindValue(message.localImage.isEmpty() ? QVariant() : QVariant(message.localImage));
    query.addBindValue(message.localImage.isEmpty() ? QVariant() : QVariant(message.localImage));
    query.addBindValue(localId);
  } else {
    query.addBindValue(state);
    query.addBindValue(message.localImage.isEmpty() ? QVariant() : QVariant(message.localImage));
    query.addBindValue(message.threadId);
    query.addBindValue(message.senderUid);
  }
  run(query);
  if (message.serverId > 0 && (message.type == 1 || message.type == 3)) {
    QSqlQuery release(database);
    release.prepare("UPDATE messages SET local_image=NULL WHERE thread_id=? AND server_id=? "
                    "AND EXISTS(SELECT 1 FROM attachments WHERE file_id=messages.content)");
    release.addBindValue(message.threadId);
    release.addBindValue(message.serverId);
    run(release);
  }
  if (message.serverId > 0) {
    QSqlQuery summary(database);
    summary.prepare("UPDATE conversations SET last_server_id=?,last_time=?,summary=? "
                    "WHERE thread_id=? AND last_server_id<=?");
    summary.addBindValue(message.serverId);
    summary.addBindValue(message.time);
    summary.addBindValue(message.type == 1   ? QStringLiteral("[图片]")
                         : message.type == 3 ? QStringLiteral("[语音]")
                                             : message.content);
    summary.addBindValue(message.threadId);
    summary.addBindValue(message.serverId);
    run(summary);
  }
  return confirmed ? SaveResult::Existing : SaveResult::New;
}
} // namespace

struct ChatStore::Worker : QObject {
  QSqlDatabase database;
  void Close()
  {
    if (!database.isValid())
      return;
    const auto name = database.connectionName();
    database.close();
    database = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
  }
  ~Worker() override { Close(); }
};

StoredMessage StoredMessage::From(const std::shared_ptr<ChatDataBase> &message)
{
  return {message->GetThreadId(),
          message->GetSendUid(),
          message->GetMsgId(),
          message->GetUniqueId(),
          message->GetContent(),
          message->GetChatTime(),
          static_cast<int>(message->GetMsgType()),
          message->GetStatus(),
          {}};
}

std::shared_ptr<TextChatData> StoredMessage::ToMessage() const
{
  return std::make_shared<TextChatData>(serverId, uniqueId, threadId, ChatFormType::PRIVATE,
                                        static_cast<ChatMsgType>(type), content, senderUid, status,
                                        time);
}

ChatStore *ChatStore::instance()
{
  static auto *store = new ChatStore(QCoreApplication::instance());
  return store;
}

ChatStore::ChatStore(QObject *parent) : QObject(parent), _worker(new Worker)
{
  _attachmentPool.setMaxThreadCount(1);
  _attachmentCancelled = std::make_shared<std::atomic_bool>(true);
  _worker->moveToThread(&_thread);
  connect(&_thread, &QThread::finished, _worker, &QObject::deleteLater);
  _thread.start();
}

ChatStore::~ChatStore() { Shutdown(); }

void ChatStore::Shutdown()
{
  if (!_thread.isRunning())
    return;
  ++_generation;
  _ready = false;
  _attachmentCancelled->store(true);
  _attachmentPool.clear();
  _attachmentPool.waitForDone();
  _directory.clear();
  // 已排队的写入先结束；数据库连接的创建、使用和关闭都留在工作线程。
  QMetaObject::invokeMethod(_worker, [this] { _worker->Close(); }, Qt::BlockingQueuedConnection);
  _thread.quit();
  _thread.wait();
  _worker = nullptr;
}

void ChatStore::Execute(std::function<ChatStoreResult(QSqlDatabase &)> operation, Callback callback)
{
  if (!_worker) {
    if (callback) {
      ChatStoreResult result;
      result.error = tr("本地存储已关闭");
      QTimer::singleShot(0, this, [callback, result] { callback(result); });
    }
    return;
  }
  const auto generation = _generation;
  QPointer<ChatStore> guard(this);
  auto *worker = _worker;
  QMetaObject::invokeMethod(
      worker,
      [worker, guard, generation, operation, callback] {
        ChatStoreResult result;
        try {
          result = operation(worker->database);
        } catch (const std::exception &error) {
          result.error = QString::fromUtf8(error.what());
        }
        if (!guard)
          return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, callback, result] {
              if (!guard || guard->_generation != generation)
                return;
              if (!result.error.isEmpty()) {
                qWarning() << "Local chat storage failed:" << result.error;
                emit guard->storageError(result.error);
              }
              if (callback)
                callback(result);
            },
            Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}

void ChatStore::ExecuteFile(std::function<ChatStoreResult()> operation, qint64 retainedBytes,
                            Callback callback)
{
  const auto generation = _generation;
  // 排队任务也计入预算；旧账号任务的计数直到其结束才释放。
  if (!_ready || _attachmentTasks >= kMaxAttachmentTasks || retainedBytes < 0 ||
      retainedBytes > kMaxAttachmentBytes - _attachmentBytes) {
    ChatStoreResult result;
    result.error = tr("图片缓存队列繁忙或账号尚未就绪");
    QTimer::singleShot(0, this, [this, generation, callback, result] {
      if (_generation != generation)
        return;
      if (callback)
        callback(result);
    });
    return;
  }
  ++_attachmentTasks;
  _attachmentBytes += retainedBytes;
  const QPointer<ChatStore> guard(this);
  const auto cancelled = _attachmentCancelled;
  _attachmentPool.start(
      QRunnable::create([guard, generation, operation, retainedBytes, callback, cancelled] {
        ChatStoreResult result;
        if (!cancelled->load()) {
          try {
            result = operation();
          } catch (const std::exception &error) {
            result.error = QString::fromUtf8(error.what());
          }
        }
        if (!guard)
          return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, retainedBytes, callback, result] {
              if (!guard)
                return;
              --guard->_attachmentTasks;
              guard->_attachmentBytes -= retainedBytes;
              if (guard->_generation != generation)
                return;
              if (!result.error.isEmpty()) {
                qWarning() << "Local attachment storage failed:" << result.error;
                emit guard->storageError(result.error);
              }
              if (callback)
                callback(result);
            },
            Qt::QueuedConnection);
      }));
}

void ChatStore::OpenAccount(int uid, const QString &server, Callback callback)
{
  ++_generation;
  _attachmentCancelled->store(true);
  _attachmentCancelled = std::make_shared<std::atomic_bool>(false);
  _ready = false;
  emit accountChanged();
  const auto serverKey = QString::fromLatin1(
      QCryptographicHash::hash(server.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
  const QString accountPath = "/accounts/" + serverKey + "/" + QString::number(uid);
  const QString dataRoot = chatDataRoot();
  _directory = dataRoot.isEmpty() ? QString() : dataRoot + accountPath;
  const QString legacyRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  QStringList legacyDirectories{_directory, QCoreApplication::applicationDirPath() + "/chat_data" +
                                                accountPath};
  if (!legacyRoot.isEmpty())
    legacyDirectories.append(legacyRoot + accountPath);
  if (!RuntimePaths::dataDirectory().isEmpty())
    legacyDirectories = {_directory};
  const auto directory = _directory;
  const bool validServer = !server.trimmed().isEmpty();
  auto *worker = _worker;
  Execute(
      [worker, directory, legacyDirectories, dataRoot, uid, validServer](QSqlDatabase &) {
        worker->Close();
        checked(uid > 0 && validServer, "Invalid account or server namespace");
        checked(QSqlDatabase::isDriverAvailable("QSQLITE"),
                QStringLiteral("未找到 SQLite 驱动，请检查客户端 sqldrivers 目录中的 "
                               "qsqlite.dll（调试版可能为 qsqlited.dll）"));
        LocalDataProtection::secureDirectory(dataRoot);
        migrateLegacyAccount(legacyDirectories, directory);
        const auto key = LocalStoreSecurity::prepareAccount(directory);
        checked(QDir().mkpath(directory + "/sql") && QDir().mkpath(directory + "/attachments"),
                QStringLiteral("无法创建本地聊天数据目录，请检查数据目录的写入权限：%1")
                    .arg(directory));
        worker->database =
            QSqlDatabase::addDatabase(LocalStoreSecurity::driverName(), "cppchat-local-store");
        auto &database = worker->database;
        database.setDatabaseName(QDir(directory).filePath(kDatabaseRelativePath));
        LocalStoreSecurity::configureDatabase(database, key);
        const bool opened = database.open();
        checked(opened, database.lastError().text());
        sql(database, "PRAGMA journal_mode=WAL");
        sql(database, "PRAGMA synchronous=FULL");
        QSqlQuery version(database);
        checked(version.exec("PRAGMA user_version") && version.next(),
                "Cannot read database version");
        const int priorVersion = version.value(0).toInt();
        checked(priorVersion <= 5, "Local database is newer than this client");
        version.finish();
        QFile schema(":/storage/chatstore-schema.sql");
        checked(schema.open(QIODevice::ReadOnly), "Local database schema is missing");
        Transaction transaction(database);
        for (const auto &statement : QString::fromUtf8(schema.readAll()).split(';'))
          if (!statement.trimmed().isEmpty())
            sql(database, statement);
        if (priorVersion < 4)
          sql(database, "UPDATE sync_state SET confirmed_cursor=0 WHERE thread_id IN "
                        "(SELECT thread_id FROM messages WHERE message_type=0 AND "
                        "content='[语音消息] 请升级客户端以收听')");
        // 上次退出时仍未得到回执的消息只恢复为待核对，不在启动时自动重发。
        sql(database, "UPDATE messages SET send_state=2 WHERE server_id IS NULL AND send_state=0");
        transaction.Commit();
        ChatStoreResult result;
        QSqlQuery conversations(database);
        conversations.prepare(
            "SELECT c.thread_id,c.other_uid,c.last_server_id,c.last_time,c.summary,g.metadata "
            "FROM conversations c LEFT JOIN group_profiles g ON g.thread_id=c.thread_id");
        run(conversations);
        while (conversations.next())
          result.conversations.append(
              {conversations.value(0).toInt(), conversations.value(1).toInt(),
               conversations.value(2).toInt(), conversations.value(3).toString(),
               conversations.value(4).toString(), conversations.value(5).toString()});
        QSqlQuery cursors(database);
        cursors.prepare("SELECT thread_id,confirmed_cursor FROM sync_state");
        run(cursors);
        while (cursors.next())
          result.cursors.insert(cursors.value(0).toInt(), cursors.value(1).toInt());
        return result;
      },
      [this, callback](const ChatStoreResult &result) {
        _ready = result.error.isEmpty();
        if (_ready) {
          qInfo() << "Local chat database:" << QDir(_directory).filePath(kDatabaseRelativePath);
          emit accountReady();
        }
        if (callback)
          callback(result);
      });
}

void ChatStore::CloseAccount()
{
  ++_generation;
  _attachmentCancelled->store(true);
  _ready = false;
  emit accountChanged();
  _directory.clear();
  if (_worker)
    QMetaObject::invokeMethod(
        _worker, [worker = _worker] { worker->Close(); }, Qt::QueuedConnection);
}

void ChatStore::ForgetGroupProfile(int threadId, Callback callback)
{
  Execute(
      [threadId](QSqlDatabase &database) {
        QSqlQuery query(database);
        query.prepare("DELETE FROM group_profiles WHERE thread_id=?");
        query.addBindValue(threadId);
        run(query);
        return ChatStoreResult{};
      },
      callback);
}

void ChatStore::SaveConversations(const QVector<StoredConversation> &conversations,
                                  Callback callback)
{
  Execute(
      [conversations](QSqlDatabase &database) {
        Transaction transaction(database);
        for (const auto &conversation : conversations) {
          QSqlQuery query(database);
          query.prepare(
              "INSERT INTO conversations(thread_id,other_uid,last_server_id,last_time,summary) "
              "VALUES(?,?,?,?,?) ON CONFLICT(thread_id) DO UPDATE SET other_uid=excluded.other_uid,"
              "last_time=CASE WHEN excluded.last_server_id>=last_server_id THEN excluded.last_time "
              "ELSE last_time END,"
              "last_server_id=MAX(last_server_id,excluded.last_server_id)");
          query.addBindValue(conversation.threadId);
          query.addBindValue(conversation.otherUid);
          query.addBindValue(conversation.lastServerId);
          query.addBindValue(conversation.lastTime.isNull() ? QStringLiteral("")
                                                            : conversation.lastTime);
          query.addBindValue(conversation.summary.isNull() ? QStringLiteral("")
                                                           : conversation.summary);
          run(query);
          if (!conversation.groupJson.isEmpty()) {
            QSqlQuery metadata(database);
            metadata.prepare("INSERT INTO group_profiles(thread_id,metadata) VALUES(?,?) "
                             "ON CONFLICT(thread_id) DO UPDATE SET metadata=excluded.metadata");
            metadata.addBindValue(conversation.threadId);
            metadata.addBindValue(conversation.groupJson);
            run(metadata);
          }
        }
        transaction.Commit();
        return ChatStoreResult{};
      },
      callback);
}

void ChatStore::SaveMessages(const QVector<StoredMessage> &messages, int syncThread, int cursor,
                             Callback callback, int clearDraftThread)
{
  Execute(
      [messages, syncThread, cursor, clearDraftThread](QSqlDatabase &database) {
        Transaction transaction(database);
        if (syncThread > 0) {
          QSqlQuery previous(database);
          previous.prepare("SELECT confirmed_cursor FROM sync_state WHERE thread_id=?");
          previous.addBindValue(syncThread);
          run(previous);
          int last = previous.next() ? previous.value(0).toInt() : 0;
          for (const auto &message : messages) {
            checked(message.threadId == syncThread && message.serverId > last,
                    "Invalid history page order");
            last = message.serverId;
          }
          checked(cursor == last, "Invalid history page cursor");
        }
        ChatStoreResult result;
        for (const auto &message : messages) {
          const auto outcome = saveMessage(database, message);
          const auto key =
              QString::number(message.threadId) + ':' + QString::number(message.serverId);
          if (outcome == SaveResult::New && message.serverId > 0)
            result.newMessageKeys.insert(key);
          if (outcome == SaveResult::Suppressed)
            result.suppressedMessageKeys.insert(key);
        }
        if (syncThread > 0 && cursor >= 0) {
          QSqlQuery query(database);
          query.prepare("INSERT INTO sync_state(thread_id,confirmed_cursor) VALUES(?,?) "
                        "ON CONFLICT(thread_id) DO UPDATE SET "
                        "confirmed_cursor=MAX(confirmed_cursor,excluded.confirmed_cursor)");
          query.addBindValue(syncThread);
          query.addBindValue(cursor);
          run(query);
        }
        if (clearDraftThread > 0) {
          QSqlQuery query(database);
          query.prepare("DELETE FROM drafts WHERE thread_id=?");
          query.addBindValue(clearDraftThread);
          run(query);
        }
        transaction.Commit();
        return result;
      },
      callback);
}

void ChatStore::LoadMessages(int thread, int beforeId, int limit, Callback callback)
{
  Execute(
      [thread, beforeId, limit](QSqlDatabase &database) {
        ChatStoreResult result;
        QSqlQuery query(database);
        query.prepare(
            "SELECT " + kMessageColumns +
            " FROM messages m JOIN conversations c ON c.thread_id=m.thread_id "
            "WHERE m.thread_id=? AND m.server_id>c.hidden_before_id AND (?=0 OR m.server_id<?) "
            "ORDER BY m.server_id DESC LIMIT ?");
        query.addBindValue(thread);
        query.addBindValue(beforeId);
        query.addBindValue(beforeId);
        query.addBindValue(limit + 1);
        run(query);
        while (query.next())
          result.messages.append(readMessage(query));
        result.more = result.messages.size() > limit;
        if (result.more)
          result.messages.removeLast();
        std::reverse(result.messages.begin(), result.messages.end());
        if (beforeId == 0) {
          QSqlQuery pending(database);
          pending.prepare(
              "SELECT " + kMessageColumns +
              " FROM messages m WHERE thread_id=? AND server_id IS NULL ORDER BY local_id");
          pending.addBindValue(thread);
          run(pending);
          while (pending.next())
            result.messages.append(readMessage(pending));
          QSqlQuery summary(database);
          summary.prepare("SELECT other_uid,last_server_id,last_time,summary FROM conversations "
                          "WHERE thread_id=?");
          summary.addBindValue(thread);
          run(summary);
          if (summary.next())
            result.conversations.append({thread,
                                         summary.value(0).toInt(),
                                         summary.value(1).toInt(),
                                         summary.value(2).toString(),
                                         summary.value(3).toString(),
                                         {}});
        }
        return result;
      },
      callback);
}

void ChatStore::SearchMessages(int thread, const QString &text, int type, HistoryCursor cursor,
                               Callback callback, const QString &date,
                               const QVector<int> &allowedThreads)
{
  QueryHistory({thread, text, type, date, allowedThreads, false}, cursor, callback);
}

void ChatStore::QueryHistory(const HistoryFilter &filter, HistoryCursor cursor, Callback callback)
{
  QJsonArray scope;
  for (int id : filter.allowedThreads)
    if (id > 0)
      scope.append(id);
  Execute(
      [filter, cursor, scope](QSqlDatabase &database) mutable {
        ChatStoreResult result;
        const int thread = filter.threadId, type = filter.category;
        const auto text = filter.text, date = filter.date;
        checked(thread >= 0, "Invalid conversation");
        checked(type == -1 || type == 0 || type == 1 || type == 3 || type == 4,
                "Invalid search category");
        if (thread == 0 && scope.isEmpty())
          return result; // An empty access scope must never mean all stored conversations.
        if (cursor.ceilingId == 0) {
          QSqlQuery boundary(database);
          boundary.prepare("SELECT COALESCE(MAX(local_id),0) FROM messages");
          run(boundary);
          checked(boundary.next(), "Cannot read history boundary");
          cursor.ceilingId = boundary.value(0).toLongLong();
        }
        const auto day = QDate::fromString(date, "yyyy-MM-dd");
        checked(date.isEmpty() || (day.isValid() && day.addDays(1).isValid()),
                "Invalid search date");
        QString escaped = text;
        escaped.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
        QSqlQuery query(database);
        query.prepare(
            "SELECT " + kMessageColumns +
            " FROM messages m JOIN conversations c ON c.thread_id=m.thread_id "
            "WHERE " +
            (thread > 0 ? QStringLiteral("m.thread_id=?")
                        : QStringLiteral("m.thread_id IN (SELECT value FROM json_each(?))")) +
            " AND (m.server_id IS NULL OR m.server_id>c.hidden_before_id) "
            "AND m.local_id<=? AND (?=0 OR m.server_id IS NOT NULL) "
            "AND (?<0 OR (?<>3 AND m.message_type=?) OR "
            "(?=3 AND m.message_type=0 AND (m.content LIKE '%http://%' OR m.content LIKE "
            "'%https://%'))) "
            "AND (?='' OR (m.chat_time>=? AND m.chat_time<?)) AND m.content LIKE ? ESCAPE '\\' "
            "AND " +
            (thread > 0
                 ? QStringLiteral(
                       "COALESCE(m.server_id,2147483647)<=? "
                       "AND (COALESCE(m.server_id,2147483647),m.local_id)<(?,?) "
                       "ORDER BY COALESCE(m.server_id,2147483647) DESC,m.local_id DESC LIMIT 51")
                 : QStringLiteral("(m.chat_time,m.local_id)<(?,?) "
                                  "ORDER BY m.chat_time DESC,m.local_id DESC LIMIT 51")));
        query.addBindValue(thread > 0 ? QVariant(thread)
                                      : QVariant(QString::fromUtf8(
                                            QJsonDocument(scope).toJson(QJsonDocument::Compact))));
        query.addBindValue(cursor.ceilingId);
        query.addBindValue(filter.confirmedOnly ? 1 : 0);
        query.addBindValue(type);
        query.addBindValue(type);
        // Search category 3 remains links; 4 is the new audio category.
        query.addBindValue(type == 4 ? 3 : type);
        query.addBindValue(type);
        query.addBindValue(date.isNull() ? QStringLiteral("") : date);
        query.addBindValue(date.isNull() ? QStringLiteral("") : date);
        query.addBindValue(day.addDays(1).toString("yyyy-MM-dd"));
        query.addBindValue('%' + escaped + '%');
        if (thread > 0) {
          query.addBindValue(cursor.serverId);
          query.addBindValue(cursor.serverId);
        } else
          query.addBindValue(cursor.time.isEmpty() ? QString(QChar(0xffff)) : cursor.time);
        query.addBindValue(cursor.localId);
        run(query);
        while (query.next())
          result.messages.append(readMessage(query));
        checked(!query.lastError().isValid(), query.lastError().text());
        result.more = result.messages.size() > 50;
        if (result.more)
          result.messages.removeLast();
        if (!result.messages.isEmpty()) {
          const auto &last = result.messages.last();
          result.nextCursor = {last.serverId > 0 ? last.serverId : std::numeric_limits<int>::max(),
                               last.localId, last.time, cursor.ceilingId};
        }
        return result;
      },
      callback);
}

void ChatStore::SaveDraft(int thread, const QVector<MsgInfo> &draft)
{
  Execute(
      [thread, draft](QSqlDatabase &database) {
        Transaction transaction(database);
        QSqlQuery remove(database);
        remove.prepare("DELETE FROM drafts WHERE thread_id=? AND ordinal>=?");
        remove.addBindValue(thread);
        remove.addBindValue(draft.size());
        run(remove);
        for (int i = 0; i < draft.size(); ++i) {
          QSqlQuery insert(database);
          insert.prepare(
              "INSERT INTO drafts(thread_id,ordinal,message_flag,content,image_data) "
              "VALUES(?,?,?,?,?) "
              "ON CONFLICT(thread_id,ordinal) DO UPDATE SET message_flag=excluded.message_flag,"
              "content=excluded.content,image_data=excluded.image_data "
              "WHERE message_flag IS NOT excluded.message_flag OR content IS NOT excluded.content "
              "OR image_data IS NOT excluded.image_data");
          insert.addBindValue(thread);
          insert.addBindValue(i);
          insert.addBindValue(draft[i].msgFlag);
          insert.addBindValue(draft[i].content.isNull() ? QStringLiteral("") : draft[i].content);
          insert.addBindValue(draft[i].imageData.isEmpty() ? QVariant()
                                                           : QVariant(draft[i].imageData));
          run(insert);
        }
        transaction.Commit();
        return ChatStoreResult{};
      },
      {});
}

void ChatStore::LoadDraft(int thread, Callback callback)
{
  Execute(
      [thread](QSqlDatabase &database) {
        ChatStoreResult result;
        QSqlQuery query(database);
        query.prepare("SELECT message_flag,content,image_data FROM drafts WHERE thread_id=? ORDER "
                      "BY ordinal");
        query.addBindValue(thread);
        run(query);
        while (query.next()) {
          MsgInfo message;
          message.msgFlag = query.value(0).toString();
          message.content = query.value(1).toString();
          message.imageData = query.value(2).toByteArray();
          result.draft.append(message);
        }
        return result;
      },
      callback);
}

void ChatStore::MarkUnconfirmed(const QString &id, bool failed)
{
  Execute(
      [id, failed](QSqlDatabase &database) {
        QSqlQuery query(database);
        query.prepare("UPDATE messages SET send_state=? WHERE unique_id=? AND server_id IS NULL");
        query.addBindValue(failed ? 3 : 2);
        query.addBindValue(id);
        run(query);
        return ChatStoreResult{};
      },
      {});
}

void ChatStore::LoadPendingImage(const QString &id, Callback callback)
{
  Execute(
      [id](QSqlDatabase &database) {
        ChatStoreResult result;
        QSqlQuery query(database);
        query.prepare("SELECT local_image FROM messages WHERE unique_id=? LIMIT 1");
        query.addBindValue(id);
        run(query);
        if (query.next())
          result.image = query.value(0).toByteArray();
        return result;
      },
      callback);
}

void ChatStore::SaveAttachment(const QString &id, const QByteArray &image, Callback callback)
{
  const QString directory = _directory;
  ExecuteFile(
      [directory, id, image] {
        checked(!directory.isEmpty() && !id.isEmpty(), "No active account or attachment identity");
        checked(!image.isEmpty() && image.size() <= kMaxAttachmentSize, "Invalid attachment size");
        const QString name = QString::fromLatin1(
            QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
        const QString relative = "attachments/" + name;
        QSaveFile file(directory + '/' + relative);
        const auto encrypted =
            LocalDataProtection::encryptPayload(image, LocalStoreSecurity::accountKey(directory));
        checked(file.open(QIODevice::WriteOnly) && file.write(encrypted) == encrypted.size() &&
                    file.commit(),
                "Cannot save attachment");
        ChatStoreResult result;
        result.attachment = {
            relative,
            QString::fromLatin1(
                QCryptographicHash::hash(image, QCryptographicHash::Sha256).toHex()),
            image.size()};
        return result;
      },
      image.size(),
      [this, id, callback](const ChatStoreResult &result) {
        if (!result.error.isEmpty()) {
          if (callback)
            callback(result);
          return;
        }
        // 文件原子提交成功后才登记元数据。工作线程只接收值，不携带 SQL 连接。
        const auto attachment = result.attachment;
        Execute(
            [id, attachment](QSqlDatabase &database) {
              Transaction transaction(database);
              QSqlQuery query(database);
              query.prepare("INSERT INTO attachments(file_id,path,sha256,size) VALUES(?,?,?,?) "
                            "ON CONFLICT(file_id) DO UPDATE SET "
                            "path=excluded.path,sha256=excluded.sha256,size=excluded.size");
              query.addBindValue(id);
              query.addBindValue(attachment.path);
              query.addBindValue(attachment.checksum);
              query.addBindValue(attachment.size);
              run(query);
              // 回执可能早于缓存写入完成；缓存可用后才清理已确认消息里的重复 BLOB。
              QSqlQuery release(database);
              release.prepare("UPDATE messages SET local_image=NULL WHERE content=? "
                              "AND message_type IN (1,3) AND server_id IS NOT NULL AND local_image "
                              "IS NOT NULL");
              release.addBindValue(id);
              run(release);
              transaction.Commit();
              return ChatStoreResult{};
            },
            callback);
      });
}

void ChatStore::LoadAttachment(const QString &id, Callback callback)
{
  const QString directory = _directory;
  Execute(
      [id](QSqlDatabase &database) {
        ChatStoreResult result;
        QSqlQuery query(database);
        query.prepare("SELECT path,sha256,size FROM attachments WHERE file_id=?");
        query.addBindValue(id);
        run(query);
        if (!query.next())
          return result;
        const auto path = query.value(0).toString();
        const qint64 size = query.value(2).toLongLong();
        if (!path.startsWith("attachments/") || path.contains("..") || size <= 0 ||
            size > kMaxAttachmentSize)
          return result;
        result.attachment = {path, query.value(1).toString(), size};
        return result;
      },
      [this, directory, callback](const ChatStoreResult &result) {
        if (!result.error.isEmpty() || result.attachment.path.isEmpty()) {
          if (callback)
            callback(result);
          return;
        }
        const auto attachment = result.attachment;
        ExecuteFile(
            [directory, attachment] {
              ChatStoreResult result;
              QFile file(directory + '/' + attachment.path);
              if (!file.open(QIODevice::ReadOnly) || file.size() > 21 * 1024 * 1024)
                return result;
              // 限定读取长度，文件损坏或被外部修改时不扩大任务预算。
              const auto bytes = LocalDataProtection::decryptPayload(
                  file.readAll(), LocalStoreSecurity::accountKey(directory));
              if (bytes.size() == attachment.size &&
                  QString::fromLatin1(
                      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) ==
                      attachment.checksum)
                result.image = bytes;
              return result;
            },
            attachment.size, callback);
      });
}

void ChatStore::ClearLocalHistory(int thread, Callback callback, bool requireConfirmedOnly)
{
  Execute(
      [thread, requireConfirmedOnly](QSqlDatabase &database) {
        Transaction transaction(database);
        checked(thread > 0, "Invalid conversation");
        QSqlQuery exists(database);
        exists.prepare("SELECT 1 FROM conversations WHERE thread_id=?");
        exists.addBindValue(thread);
        run(exists);
        checked(exists.next(), "Conversation is missing");
        if (requireConfirmedOnly) {
          QSqlQuery pending(database);
          pending.prepare("SELECT 1 FROM messages WHERE thread_id=? AND server_id IS NULL LIMIT 1");
          pending.addBindValue(thread);
          run(pending);
          checked(!pending.next(), "Conversation still has pending messages");
        }
        QSqlQuery query(database);
        // 记录隐藏边界，保留同步/去重信息；后续补拉不会把清空前的内容再次显示。
        query.prepare("UPDATE conversations SET hidden_before_id=MAX(last_server_id,"
                      "hidden_before_id,COALESCE((SELECT MAX(server_id) FROM messages WHERE "
                      "thread_id=?),0)),summary='' WHERE thread_id=?");
        query.addBindValue(thread);
        query.addBindValue(thread);
        run(query);
        QSqlQuery remove(database);
        remove.prepare("DELETE FROM messages WHERE thread_id=? AND server_id IS NOT NULL");
        remove.addBindValue(thread);
        run(remove);
        ChatStoreResult result;
        QSqlQuery boundary(database);
        boundary.prepare("SELECT hidden_before_id FROM conversations WHERE thread_id=?");
        boundary.addBindValue(thread);
        run(boundary);
        if (boundary.next())
          result.cursors.insert(thread, boundary.value(0).toInt());
        transaction.Commit();
        return result;
      },
      [this, thread, callback](const ChatStoreResult &result) {
        if (result.error.isEmpty())
          emit localHistoryCleared(thread);
        if (callback)
          callback(result);
      });
}

void ChatStore::DeleteLocalMessage(int thread, int serverId, Callback callback)
{
  DeleteLocalMessages(thread, {serverId}, callback);
}

void ChatStore::DeleteLocalMessages(int thread, const QVector<int> &serverIds, Callback callback)
{
  Execute(
      [thread, serverIds](QSqlDatabase &database) {
        checked(thread > 0 && !serverIds.isEmpty() && serverIds.size() <= 50,
                "Select between 1 and 50 confirmed messages");
        for (int id : serverIds)
          checked(id > 0, "Only confirmed messages can be deleted locally");
        Transaction transaction(database);
        QSqlQuery exists(database);
        exists.prepare("SELECT 1 FROM conversations WHERE thread_id=?");
        exists.addBindValue(thread);
        run(exists);
        checked(exists.next(), "Message conversation is missing");
        for (int serverId : QSet<int>(serverIds.begin(), serverIds.end())) {
          QSqlQuery tombstone(database);
          tombstone.prepare(
              "INSERT OR IGNORE INTO deleted_messages(thread_id,server_id) VALUES(?,?)");
          tombstone.addBindValue(thread);
          tombstone.addBindValue(serverId);
          run(tombstone);
          QSqlQuery remove(database);
          remove.prepare("DELETE FROM messages WHERE thread_id=? AND server_id=?");
          remove.addBindValue(thread);
          remove.addBindValue(serverId);
          run(remove);
          QSqlQuery summary(database);
          summary.prepare(
              "UPDATE conversations SET summary=? WHERE thread_id=? AND last_server_id=?");
          summary.addBindValue(QObject::tr("[本机已删除消息]"));
          summary.addBindValue(thread);
          summary.addBindValue(serverId);
          run(summary);
        }
        transaction.Commit();
        return ChatStoreResult{};
      },
      [this, thread, serverIds, callback](const ChatStoreResult &result) {
        if (result.error.isEmpty()) {
          if (serverIds.size() == 1)
            emit localMessageDeleted(thread, serverIds.first());
          else
            emit localMessagesDeleted(thread, serverIds);
        }
        if (callback)
          callback(result);
      });
}
