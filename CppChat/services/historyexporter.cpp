#include "historyexporter.h"
#include "common/runtimepaths.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>

namespace {
bool within(const QString &path, const QString &directory)
{
  if (directory.isEmpty())
    return false;
  const auto info = QFileInfo(directory);
  const auto base = QDir::cleanPath(info.canonicalFilePath().isEmpty() ? info.absoluteFilePath()
                                                                       : info.canonicalFilePath());
#ifdef Q_OS_WIN
  const auto sensitivity = Qt::CaseInsensitive;
#else
  const auto sensitivity = Qt::CaseSensitive;
#endif
  return path.compare(base, sensitivity) == 0 || path.startsWith(base + '/', sensitivity);
}
} // namespace

HistoryExporter::HistoryExporter(QObject *parent, int messageLimit, qint64 byteLimit)
    : QObject(parent), _messageLimit(std::clamp(messageLimit, 1, 10000)),
      _byteLimit(std::clamp<qint64>(byteLimit, 256, 32 * 1024 * 1024))
{
  connect(ChatStore::instance(), &ChatStore::accountChanged, this, &HistoryExporter::Cancel);
  const auto deleted = [this](int thread) {
    if (Busy() && _threads.contains(thread))
      Fail(tr("导出范围内的本机记录已修改，请重新导出"));
  };
  connect(ChatStore::instance(), &ChatStore::localHistoryCleared, this, deleted);
  connect(ChatStore::instance(), &ChatStore::localMessageDeleted, this,
          [deleted](int thread, int) { deleted(thread); });
  connect(ChatStore::instance(), &ChatStore::localMessagesDeleted, this,
          [deleted](int thread, const QVector<int> &) { deleted(thread); });
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_friend_deleted, this, [this] {
    if (Busy())
      Fail(tr("好友关系已变化，请重新选择导出范围"));
  });
  connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_group_info, this,
          [this](const QJsonObject &group) {
            if (Busy() && _threads.contains(group["thread_id"].toInt()))
              Fail(tr("群聊资料或权限已变化，请重新导出"));
          });
}

HistoryExporter::~HistoryExporter() = default;
bool HistoryExporter::Busy() const { return bool(_file); }

bool HistoryExporter::ScopeValid() const
{
  if (!ChatStore::instance()->IsReady() || ChatStore::instance()->Generation() != _generation)
    return false;
  const auto mgr = UserMgr::GetInstance();
  for (auto it = _threads.cbegin(); it != _threads.cend(); ++it)
    if (mgr->GetChatThreadByThreadId(it.key()) != it.value() ||
        (!it.value()->IsGroup() && !mgr->GetFriendById(it.value()->GetOtherId())))
      return false;
  return true;
}

bool HistoryExporter::Start(HistoryFilter filter, const QString &fileName, Format format,
                            QString &error)
{
  error.clear();
  if (Busy()) {
    error = tr("已有导出正在进行");
    return false;
  }
  const QFileInfo target(fileName);
  const auto extension = format == Format::Json ? QStringLiteral("json") : QStringLiteral("txt");
  const auto parent = QFileInfo(target.absolutePath()).canonicalFilePath();
  const auto path = QDir(parent).filePath(target.fileName());
  if (!target.isAbsolute() || parent.isEmpty() || target.isSymLink() ||
      (format != Format::Json && format != Format::Text) ||
      target.suffix().toLower() != extension ||
      within(path, ChatStore::instance()->AccountDirectory()) ||
      within(path, RuntimePaths::preferencesDirectory()) ||
      within(path, RuntimePaths::configFile())) {
    error = tr("请选择现有目录下的 .json 或 .txt 文件，不能覆盖客户端内部数据或符号链接");
    return false;
  }
  _generation = ChatStore::instance()->Generation();
  _threads.clear();
  _names.clear();
  const auto mgr = UserMgr::GetInstance();
  if (filter.threadId > 0) {
    if (!filter.allowedThreads.contains(filter.threadId)) {
      error = tr("会话不在当前可访问范围内");
      return false;
    }
    filter.allowedThreads = {filter.threadId};
  }
  for (int id : filter.allowedThreads) {
    auto data = mgr->GetChatThreadByThreadId(id);
    if (!data)
      continue;
    _threads.insert(id, data);
    auto user = mgr->GetFriendById(data->GetOtherId());
    _names.insert(id, data->IsGroup() ? data->GetGroupName()
                      : user          ? (user->_back.isEmpty() ? user->_name : user->_back)
                                      : QString());
  }
  if (_threads.size() !=
          QSet<int>(filter.allowedThreads.cbegin(), filter.allowedThreads.cend()).size() ||
      !ScopeValid()) {
    error = tr("会话或账号已变化，请重新选择导出范围");
    return false;
  }
  _file = std::make_unique<QSaveFile>(path);
  _file->setDirectWriteFallback(false);
  if (!_file->open(QIODevice::WriteOnly)) {
    error = tr("无法写入导出文件：%1").arg(_file->errorString());
    _file.reset();
    return false;
  }
  filter.confirmedOnly = true;
  _filter = filter;
  _format = format;
  _cursor = {};
  _count = 0;
  _bytes = 0;
  ++_revision;
  // Only whitelisted metadata is exported; never serialize configuration or StoredMessage memory.
  if (!Write(format == Format::Json
                 ? QByteArray("{\"schema\":1,\"messages\":[\n")
                 : QStringLiteral("CppChat 本机已确认聊天记录（不含附件文件）\n\n").toUtf8())) {
    error = tr("无法准备导出文件");
    return false;
  }
  const auto revision = _revision;
  const QPointer<HistoryExporter> guard(this);
  emit busyChanged(true);
  if (guard && guard->Busy() && guard->_revision == revision)
    QTimer::singleShot(0, this, [guard, revision] {
      if (guard && guard->_revision == revision)
        guard->NextPage();
    });
  return true;
}

void HistoryExporter::Cancel()
{
  if (!Busy())
    return;
  ++_revision;
  _file->cancelWriting();
  _file.reset();
  _threads.clear();
  _names.clear();
  const QPointer<HistoryExporter> guard(this);
  emit busyChanged(false);
  if (guard)
    emit cancelled();
}

void HistoryExporter::Fail(const QString &message)
{
  if (!Busy())
    return;
  ++_revision;
  _file->cancelWriting();
  _file.reset();
  _threads.clear();
  _names.clear();
  const QPointer<HistoryExporter> guard(this);
  emit busyChanged(false);
  if (guard)
    emit failed(message);
}

bool HistoryExporter::Write(const QByteArray &bytes)
{
  if (_bytes + bytes.size() > _byteLimit) {
    Fail(tr("导出大小超过限制，请缩小日期或会话范围"));
    return false;
  }
  if (_file->write(bytes) != bytes.size()) {
    Fail(tr("导出写入失败，原目标文件保留"));
    return false;
  }
  _bytes += bytes.size();
  return true;
}

void HistoryExporter::NextPage()
{
  if (!Busy())
    return;
  if (!ScopeValid()) {
    Fail(tr("导出账号或会话权限已变化，导出已停止"));
    return;
  }
  const auto revision = _revision;
  const QPointer<HistoryExporter> guard(this);
  ChatStore::instance()->QueryHistory(
      _filter, _cursor, [guard, revision](const ChatStoreResult &result) {
        if (!guard || !guard->Busy() || guard->_revision != revision)
          return;
        if (!result.error.isEmpty() || !guard->ScopeValid()) {
          guard->Fail(result.error.isEmpty() ? guard->tr("导出权限已变化") : result.error);
          return;
        }
        for (const auto &m : result.messages) {
          if (guard->_count >= guard->_messageLimit) {
            guard->Fail(guard->tr("导出条数超过限制，请缩小日期或会话范围"));
            return;
          }
          QByteArray bytes;
          if (guard->_format == Format::Json) {
            bytes = (guard->_count ? QByteArray(",\n") : QByteArray()) +
                    QJsonDocument(QJsonObject{{"thread_id", m.threadId},
                                              {"conversation", guard->_names.value(m.threadId)},
                                              {"server_id", m.serverId},
                                              {"sender_uid", m.senderUid},
                                              {"time", m.time},
                                              {"type", m.type},
                                              {"content", m.content}})
                        .toJson(QJsonDocument::Compact);
          } else {
            bytes = QString("[%1] %2 | UID %3 | %4\n%5\n\n")
                        .arg(m.time, guard->_names.value(m.threadId))
                        .arg(m.senderUid)
                        .arg(m.type == 1   ? guard->tr("图片资源 ID")
                             : m.type == 3 ? guard->tr("语音资源 ID")
                                           : guard->tr("文字"),
                             m.content)
                        .toUtf8();
          }
          if (!guard->Write(bytes))
            return;
          ++guard->_count;
        }
        guard->_cursor = result.nextCursor;
        emit guard->progress(guard->_count);
        if (!guard || !guard->Busy() || guard->_revision != revision)
          return;
        if (!guard->ScopeValid()) {
          guard->Fail(guard->tr("导出权限已变化"));
          return;
        }
        if (result.more) {
          QTimer::singleShot(0, guard, [guard, revision] {
            if (guard && guard->_revision == revision)
              guard->NextPage();
          });
          return;
        }
        if (!guard->Write(guard->_format == Format::Json ? QByteArray("\n]}\n") : QByteArray()))
          return;
        const auto fileName = guard->_file->fileName();
        if (!guard->_file->commit()) {
          guard->Fail(guard->tr("导出提交失败，原目标文件保留"));
          return;
        }
        guard->_file.reset();
        guard->_threads.clear();
        guard->_names.clear();
        emit guard->busyChanged(false);
        if (guard && guard->_revision == revision)
          emit guard->finished(fileName, guard->_count);
      });
}
