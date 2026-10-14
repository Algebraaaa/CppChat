#include "common/localstoresecurity.h"
#include "common/localdataprotection.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <stdexcept>

namespace {
const QByteArray kKeyContext("CppChat local account key v1");
const QString kDatabase("sql/db.sqlite");
void require(bool ok, const QString &message)
{
  if (!ok)
    throw std::runtime_error(message.toStdString());
}
void execute(QSqlDatabase &database, const QString &statement)
{
  QSqlQuery query(database);
  require(query.exec(statement), query.lastError().text());
}
void copyAtomic(const QString &source, const QString &destination)
{
  QFile input(source);
  QSaveFile output(destination);
  require(input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly),
          "Cannot open secure migration file");
  while (!input.atEnd()) {
    const QByteArray bytes = input.read(1024 * 1024);
    require(!bytes.isEmpty() && output.write(bytes) == bytes.size(), "Cannot copy secure file");
  }
  require(output.commit(), "Cannot commit secure migration file");
}
void verifyDatabase(const QString &path, const QByteArray &key)
{
  require(QFileInfo(path).isFile() && !QFileInfo(path).isSymLink() && QFileInfo(path).size() > 0,
          "Encrypted database backup is missing or invalid");
  const QString name = QUuid::createUuid().toString();
  auto cleanup = qScopeGuard([&]() { QSqlDatabase::removeDatabase(name); });
  auto database = QSqlDatabase::addDatabase(LocalStoreSecurity::driverName(), name);
  database.setDatabaseName(path);
  LocalStoreSecurity::configureDatabase(database, key);
  database.setConnectOptions(database.connectOptions() + ";QSQLITE_OPEN_READONLY");
  require(database.open(), "Cannot open encrypted database with the protected key");
  QSqlQuery check(database);
  require(check.exec("PRAGMA integrity_check") && check.next() && check.value(0).toString() == "ok",
          "Encrypted database integrity check failed");
}
QByteArray attachmentBytes(const QString &path)
{
  QFile file(path);
  require(file.open(QIODevice::ReadOnly) && file.size() <= 21 * 1024 * 1024,
          "Attachment cannot be read within the migration budget");
  return file.readAll();
}
void protectAttachments(const QString &directory, const QByteArray &key)
{
  const auto files = QDir(directory + "/attachments").entryInfoList(QDir::Files | QDir::Hidden);
  for (const auto &file : files) {
    require(!file.isSymLink(), "Attachment migration refuses links");
    QFile header(file.filePath());
    require(header.open(QIODevice::ReadOnly), "Cannot inspect private attachment");
    const bool encrypted = LocalDataProtection::isEncryptedPayload(header.read(16));
    header.close();
    if (encrypted)
      continue;
    const auto plain = attachmentBytes(file.filePath());
    const auto protectedBytes = LocalDataProtection::encryptPayload(plain, key);
    require(LocalDataProtection::decryptPayload(protectedBytes, key) == plain,
            "Attachment encryption verification failed");
    LocalDataProtection::atomicWrite(file.filePath(), protectedBytes);
  }
}
} // namespace

namespace LocalStoreSecurity {
QString driverName()
{
#ifdef Q_OS_WIN
  return QStringLiteral("SQLITE_MULTIPLE_CIPHERS");
#else
  return QStringLiteral("QSQLITE");
#endif
}

void configureDatabase(QSqlDatabase &database, const QByteArray &key)
{
  QString options("QSQLITE_BUSY_TIMEOUT=5000");
#ifdef Q_OS_WIN
  if (!key.isEmpty())
    options += ";QSQLITE_MC_CIPHER=chacha20;QSQLITE_MC_KEY=" + QString::fromLatin1(key.toHex());
#else
  Q_UNUSED(key);
#endif
  database.setConnectOptions(options);
}

QByteArray accountKey(const QString &directory, bool create)
{
#ifdef Q_OS_WIN
  const QString path = directory + "/.key.dpapi";
  if (!QFile::exists(path)) {
    require(create, "本地解密密钥丢失，请恢复备份或导入恢复密钥，不会重新生成并覆盖数据。");
    const auto key = LocalDataProtection::randomKey();
    LocalDataProtection::atomicWrite(path, LocalDataProtection::protect(key, kKeyContext));
  }
  QFile file(path);
  require(file.open(QIODevice::ReadOnly) && file.size() < 4096, "Cannot read local encryption key");
  const auto key = LocalDataProtection::unprotect(file.readAll(), kKeyContext);
  require(key.size() == 32, "Invalid protected local encryption key");
  return key;
#else
  Q_UNUSED(directory);
  Q_UNUSED(create);
  return {};
#endif
}

QByteArray prepareAccount(const QString &directory)
{
  LocalDataProtection::secureDirectory(directory, true);
#ifdef Q_OS_WIN
  require(QSqlDatabase::isDriverAvailable(driverName()), "缺少加密 SQLite 驱动，拒绝明文回退。");
  QLockFile lock(directory + "/.security-migration.lock");
  require(lock.tryLock(), "该账号正在加密迁移，请先退出其他客户端。");
  const QString path = directory + '/' + kDatabase;
  QFile header(path);
  const bool exists = header.exists();
  require(!exists || header.open(QIODevice::ReadOnly), "Cannot inspect local database");
  const bool plain = exists && header.read(16) == QByteArray("SQLite format 3\0", 16);
  header.close();
  const auto key = accountKey(directory, !exists || plain);
  if (plain) {
    // Snapshot committed WAL data first. Rekey only the owned snapshot, never the user's source.
    QTemporaryDir staging(directory + "/.encrypt-XXXXXX");
    require(staging.isValid(), "Cannot create encryption migration staging");
    LocalDataProtection::secureDirectory(staging.path());
    require(QDir().mkpath(staging.filePath("sql")) &&
                QDir().mkpath(staging.filePath("attachments")),
            "Cannot prepare encrypted snapshot directories");
    const QString snapshot = staging.filePath(kDatabase);
    {
      const QString name = QUuid::createUuid().toString();
      auto cleanup = qScopeGuard([&]() { QSqlDatabase::removeDatabase(name); });
      auto database = QSqlDatabase::addDatabase("QSQLITE", name);
      database.setDatabaseName(path);
      require(database.open(), "Cannot open legacy database for backup");
      QSqlQuery checkpoint(database);
      require(checkpoint.exec("PRAGMA wal_checkpoint(TRUNCATE)") && checkpoint.next() &&
                  checkpoint.value(0).toInt() == 0,
              "旧数据库仍在使用，已保留原数据，请退出其他客户端后重试。");
      checkpoint.finish();
      QSqlQuery copy(database);
      copy.prepare("VACUUM main INTO ?");
      copy.addBindValue(snapshot);
      require(copy.exec(), "Cannot snapshot legacy database");
    }
    {
      const QString name = QUuid::createUuid().toString();
      auto cleanup = qScopeGuard([&]() { QSqlDatabase::removeDatabase(name); });
      auto database = QSqlDatabase::addDatabase(driverName(), name);
      database.setDatabaseName(snapshot);
      database.setConnectOptions("QSQLITE_MC_CIPHER=chacha20;QSQLITE_MC_UPDATE_KEY=" +
                                 QString::fromLatin1(key.toHex()));
      require(database.open(), "Cannot encrypt migration snapshot");
    }
    verifyDatabase(snapshot, key);
    LocalDataProtection::atomicWrite(staging.filePath(".key.dpapi"),
                                     LocalDataProtection::protect(key, kKeyContext));
    const auto attachments =
        QDir(directory + "/attachments").entryInfoList(QDir::Files | QDir::Hidden);
    for (const auto &attachment : attachments) {
      require(!attachment.isSymLink(), "Attachment snapshot refuses links");
      const auto original = attachmentBytes(attachment.filePath());
      const auto encrypted = LocalDataProtection::isEncryptedPayload(original)
                                 ? original
                                 : LocalDataProtection::encryptPayload(original, key);
      const auto decoded = LocalDataProtection::decryptPayload(encrypted, key);
      require(LocalDataProtection::isEncryptedPayload(original) || decoded == original,
              "Encrypted attachment backup does not match original");
      LocalDataProtection::atomicWrite(staging.filePath("attachments/" + attachment.fileName()),
                                       encrypted);
    }
    // Keep a verified encrypted recovery snapshot before atomically replacing the live database.
    const QString backup = directory + "/.security-backups/" +
                           QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss") + '-' +
                           QUuid::createUuid().toString(QUuid::WithoutBraces);
    LocalDataProtection::secureDirectory(backup);
    require(QDir().mkpath(backup + "/sql") && QDir().mkpath(backup + "/attachments"),
            "Cannot create recovery backup");
    copyAtomic(snapshot, backup + '/' + kDatabase);
    copyAtomic(staging.filePath(".key.dpapi"), backup + "/.key.dpapi");
    for (const auto &attachment : QDir(staging.filePath("attachments")).entryInfoList(QDir::Files))
      copyAtomic(attachment.filePath(), backup + "/attachments/" + attachment.fileName());
    verifyDatabase(backup + '/' + kDatabase, accountKey(backup));
    copyAtomic(snapshot, path);
  } else if (exists) {
    verifyDatabase(path, key);
  }
  protectAttachments(directory, key);
  // The verified encrypted snapshot supersedes the old layout. Never leave a plaintext duplicate.
  for (const auto &name : {QString("chat.db"), QString("chat.db-wal"), QString("chat.db-shm")}) {
    const QString legacy = directory + '/' + name;
    if (QFile::exists(legacy))
      require(QFile::remove(legacy), "旧聊天数据库仍被占用，请退出其他客户端后重试。");
  }
  return key;
#else
  return {};
#endif
}

void restoreBackup(const QString &backup, const QString &emptyDestination)
{
  const auto key = accountKey(backup);
  verifyDatabase(backup + '/' + kDatabase, key);
  const auto entries =
      QDir(emptyDestination)
          .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
  require(entries.isEmpty(), "恢复目标必须为空目录，拒绝覆盖现有聊天数据。");
  LocalDataProtection::secureDirectory(emptyDestination);
  require(QDir().mkpath(emptyDestination + "/sql") &&
              QDir().mkpath(emptyDestination + "/attachments"),
          "Cannot create restore directories");
  copyAtomic(backup + '/' + kDatabase, emptyDestination + '/' + kDatabase);
  copyAtomic(backup + "/.key.dpapi", emptyDestination + "/.key.dpapi");
  for (const auto &attachment : QDir(backup + "/attachments").entryInfoList(QDir::Files)) {
    LocalDataProtection::decryptPayload(attachmentBytes(attachment.filePath()), key);
    copyAtomic(attachment.filePath(), emptyDestination + "/attachments/" + attachment.fileName());
  }
}

int prepareRoot(const QString &root)
{
  LocalDataProtection::secureDirectory(root, true);
  int count = 0;
  const auto servers = QDir(root + "/accounts").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const auto &server : servers) {
    const auto accounts = QDir(server.filePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &account : accounts) {
      if (!QFile::exists(account.filePath() + '/' + kDatabase))
        continue;
      prepareAccount(account.filePath());
      ++count;
    }
  }
  return count;
}
void replaceAccountKey(const QString &directory, const QByteArray &key)
{
  require(key.size() == 32, "Invalid recovery account key");
  verifyDatabase(directory + '/' + kDatabase, key);
  LocalDataProtection::secureDirectory(directory, true);
  const QString path = directory + "/.key.dpapi";
  if (QFile::exists(path))
    copyAtomic(path, directory + "/.key.dpapi.previous");
  LocalDataProtection::atomicWrite(path, LocalDataProtection::protect(key, kKeyContext));
}
} // namespace LocalStoreSecurity
