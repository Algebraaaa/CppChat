#include "common/keyrecovery.h"
#include "common/localdataprotection.h"
#include "common/localstoresecurity.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <stdexcept>

namespace {
void require(bool ok, const char *message)
{
  if (!ok)
    throw std::runtime_error(message);
}
const QRegularExpression kAccount("^accounts/[0-9a-f]{24}/[1-9][0-9]*$");
} // namespace
namespace KeyRecovery {
QByteArray exportKeys(const QString &dataRoot, const QString &password)
{
  QJsonArray keys;
  for (const auto &server :
       QDir(dataRoot + "/accounts").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    for (const auto &account :
         QDir(server.filePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const QString relative = "accounts/" + server.fileName() + '/' + account.fileName();
      if (!kAccount.match(relative).hasMatch() ||
          !QFile::exists(account.filePath() + "/.key.dpapi"))
        continue;
      keys.append(QJsonObject{
          {"account", relative},
          {"key",
           QString::fromLatin1(LocalStoreSecurity::accountKey(account.filePath()).toHex())}});
    }
  }
  require(!keys.isEmpty() && keys.size() <= 1000,
          "No protected accounts to export or too many accounts");
  const auto salt = LocalDataProtection::randomBytes(16);
  const auto key = LocalDataProtection::passwordKey(password, salt);
  const auto encrypted =
      LocalDataProtection::encryptPayload(QJsonDocument(keys).toJson(QJsonDocument::Compact), key);
  return QJsonDocument(QJsonObject{{"format", "CppChatRecovery1"},
                                   {"salt", QString::fromLatin1(salt.toBase64())},
                                   {"payload", QString::fromLatin1(encrypted.toBase64())}})
      .toJson(QJsonDocument::Compact);
}
int importKeys(const QString &dataRoot, const QByteArray &recovery, const QString &password)
{
  require(recovery.size() <= 1024 * 1024, "Recovery file exceeds limit");
  const auto document = QJsonDocument::fromJson(recovery);
  require(document.isObject(), "Invalid recovery file");
  const auto object = document.object();
  require(object.value("format").toString() == "CppChatRecovery1", "Unsupported recovery format");
  const auto salt = QByteArray::fromBase64(object.value("salt").toString().toLatin1());
  const auto payload = QByteArray::fromBase64(object.value("payload").toString().toLatin1());
  const auto decoded = LocalDataProtection::decryptPayload(
      payload, LocalDataProtection::passwordKey(password, salt));
  const auto keysDocument = QJsonDocument::fromJson(decoded);
  require(keysDocument.isArray() && !keysDocument.array().isEmpty() &&
              keysDocument.array().size() <= 1000,
          "Invalid recovery key bundle");
  QList<QPair<QString, QByteArray>> keys;
  QSet<QString> seen;
  for (const auto &value : keysDocument.array()) {
    const auto entry = value.toObject();
    const QString relative = entry.value("account").toString();
    const QString hex = entry.value("key").toString();
    require(kAccount.match(relative).hasMatch() && !seen.contains(relative) &&
                QRegularExpression("^[0-9a-f]{64}$").match(hex).hasMatch(),
            "Invalid recovery account path or key");
    seen.insert(relative);
    const QString directory = QDir(dataRoot).filePath(relative);
    require(QFile::exists(directory + "/sql/db.sqlite"), "Copied encrypted database is missing");
    const QByteArray key = QByteArray::fromHex(hex.toLatin1());
    // Validate every key against its database before modifying any key file.
    const QString name = QUuid::createUuid().toString();
    auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(name); });
    auto database = QSqlDatabase::addDatabase(LocalStoreSecurity::driverName(), name);
    database.setDatabaseName(directory + "/sql/db.sqlite");
    LocalStoreSecurity::configureDatabase(database, key);
    database.setConnectOptions(database.connectOptions() + ";QSQLITE_OPEN_READONLY");
    require(database.open(), "Recovery key does not match copied database");
    QSqlQuery check(database);
    require(check.exec("PRAGMA integrity_check") && check.next() &&
                check.value(0).toString() == "ok",
            "Recovered database integrity failed");
    keys.append({directory, key});
  }
  for (const auto &entry : keys)
    LocalStoreSecurity::replaceAccountKey(entry.first, entry.second);
  return keys.size();
}
} // namespace KeyRecovery
