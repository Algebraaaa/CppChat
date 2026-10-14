#include "common/keyrecovery.h"
#include "common/localdataprotection.h"
#include "common/localstoresecurity.h"
#include "common/securitypolicy.h"
#include "network/httpmgr.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>

#include <aclapi.h>
#endif

namespace {
const QString kNamespace("0123456789abcdef01234567");
void createPlain(const QString &directory)
{
  QVERIFY(QDir().mkpath(directory + "/sql"));
  QVERIFY(QDir().mkpath(directory + "/attachments"));
  const QString name = QUuid::createUuid().toString();
  auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(name); });
  auto database = QSqlDatabase::addDatabase("QSQLITE", name);
  database.setDatabaseName(directory + "/sql/db.sqlite");
  QVERIFY(database.open());
  QSqlQuery query(database);
  QVERIFY(query.exec("PRAGMA journal_mode=WAL"));
  QVERIFY(query.exec("CREATE TABLE messages(content TEXT)"));
  QVERIFY(query.exec("INSERT INTO messages VALUES('private-message-secret-中文')"));
  QFile image(directory + "/attachments/test-image");
  QVERIFY(image.open(QIODevice::WriteOnly));
  image.write("private-image-secret");
}
QString readMessage(const QString &directory, const QByteArray &key)
{
  const QString name = QUuid::createUuid().toString();
  auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(name); });
  auto database = QSqlDatabase::addDatabase(LocalStoreSecurity::driverName(), name);
  database.setDatabaseName(directory + "/sql/db.sqlite");
  LocalStoreSecurity::configureDatabase(database, key);
  if (!database.open())
    return {};
  QSqlQuery query(database);
  if (!query.exec("SELECT content FROM messages") || !query.next())
    return {};
  return query.value(0).toString();
}
} // namespace
class SecurityTests : public QObject {
  Q_OBJECT
private slots:
  void releaseTransportPolicy()
  {
    QVERIFY(SecurityPolicy::requiresTls());
    QVERIFY(!SecurityPolicy::transportAllowed(QUrl("http://127.0.0.1:8090")));
    QVERIFY(!SecurityPolicy::transportAllowed(QUrl("http://public.example.test")));
    QVERIFY(SecurityPolicy::transportAllowed(QUrl("https://public.example.test")));
    QVERIFY(!SecurityPolicy::socketAllowed(false));
    QVERIFY(SecurityPolicy::socketAllowed(true));
    int failures = 0;
    auto manager = HttpMgr::GetInstance();
    auto connection = connect(manager.get(), &HttpMgr::sig_login_mod_finish, this,
                              [&](ReqId, const QByteArray &, ErrorCodes error) {
                                if (error == ErrorCodes::NetworkError)
                                  ++failures;
                              });
    manager->PostHttpReq(QUrl("http://127.0.0.1:1/user_login"), {}, ReqId::ID_LOGIN_USER,
                         Modules::LOGINMOD);
    QCOMPARE(failures, 1);
    disconnect(connection);
  }
  void authenticatedEncryption()
  {
#ifdef Q_OS_WIN
    const auto key = LocalDataProtection::randomKey();
    const QByteArray plain("private payload");
    const auto encrypted = LocalDataProtection::encryptPayload(plain, key);
    QVERIFY(!encrypted.contains(plain));
    QCOMPARE(LocalDataProtection::decryptPayload(encrypted, key), plain);
    QVERIFY(LocalDataProtection::encryptPayload(plain, key) != encrypted);
    auto changed = encrypted;
    changed[changed.size() - 1] ^= 1;
    QVERIFY_EXCEPTION_THROWN(LocalDataProtection::decryptPayload(changed, key), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(
        LocalDataProtection::decryptPayload(encrypted, LocalDataProtection::randomKey()),
        std::runtime_error);
    const auto protectedKey = LocalDataProtection::protect(key, "account");
    QCOMPARE(LocalDataProtection::unprotect(protectedKey, "account"), key);
    QVERIFY_EXCEPTION_THROWN(LocalDataProtection::unprotect(protectedKey, "different"),
                             std::runtime_error);
#endif
  }
  void migrationBackupAndRestore()
  {
#ifdef Q_OS_WIN
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString directory = root.path() + "/accounts/" + kNamespace + "/101";
    createPlain(directory);
    const auto key = LocalStoreSecurity::prepareAccount(directory);
    QCOMPARE(readMessage(directory, key), QString("private-message-secret-中文"));
    QFile database(directory + "/sql/db.sqlite");
    QVERIFY(database.open(QIODevice::ReadOnly));
    const auto bytes = database.readAll();
    database.close();
    QVERIFY(!bytes.startsWith("SQLite format 3"));
    QVERIFY(!bytes.contains("private-message-secret"));
    QFile image(directory + "/attachments/test-image");
    QVERIFY(image.open(QIODevice::ReadOnly));
    const auto encryptedImage = image.readAll();
    image.close();
    QVERIFY(!encryptedImage.contains("private-image-secret"));
    QCOMPARE(LocalDataProtection::decryptPayload(encryptedImage, key),
             QByteArray("private-image-secret"));
    const auto backups =
        QDir(directory + "/.security-backups").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(backups.size(), 1);
    const QString restore = root.path() + "/restored";
    LocalStoreSecurity::restoreBackup(directory + "/.security-backups/" + backups.first(), restore);
    QCOMPARE(readMessage(restore, LocalStoreSecurity::accountKey(restore)),
             QString("private-message-secret-中文"));
    QCOMPARE(LocalStoreSecurity::prepareAccount(directory), key); // Idempotent reopen.
    QVERIFY(QFile::remove(directory + "/.key.dpapi"));
    QVERIFY_EXCEPTION_THROWN(LocalStoreSecurity::prepareAccount(directory), std::runtime_error);
    QVERIFY(!QFile::exists(directory +
                           "/.key.dpapi")); // Never replace a missing key with a random one.
#endif
  }
  void recoveryPasswordAndCopiedData()
  {
#ifdef Q_OS_WIN
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString directory = root.path() + "/accounts/" + kNamespace + "/101";
    createPlain(directory);
    const auto key = LocalStoreSecurity::prepareAccount(directory);
    const auto recovery = KeyRecovery::exportKeys(root.path(), "strong-recovery-password");
    QVERIFY(!recovery.contains(key.toHex()));
    QVERIFY_EXCEPTION_THROWN(
        KeyRecovery::importKeys(root.path(), recovery, "wrong-recovery-password"),
        std::runtime_error);
    QCOMPARE(LocalStoreSecurity::accountKey(directory), key);
    QVERIFY(QFile::remove(directory + "/.key.dpapi"));
    QCOMPARE(KeyRecovery::importKeys(root.path(), recovery, "strong-recovery-password"), 1);
    QCOMPARE(LocalStoreSecurity::accountKey(directory), key);
    QCOMPARE(readMessage(directory, key), QString("private-message-secret-中文"));
    auto corrupted = recovery;
    corrupted[corrupted.size() / 2] ^= 1;
    QVERIFY_EXCEPTION_THROWN(
        KeyRecovery::importKeys(root.path(), corrupted, "strong-recovery-password"),
        std::runtime_error);
    QTemporaryDir copied;
    const QString copy = copied.path() + "/accounts/" + kNamespace + "/101";
    QVERIFY(QDir().mkpath(copy + "/sql") && QDir().mkpath(copy + "/attachments"));
    QVERIFY(QFile::copy(directory + "/sql/db.sqlite", copy + "/sql/db.sqlite"));
    QVERIFY(QFile::copy(directory + "/attachments/test-image", copy + "/attachments/test-image"));
    LocalDataProtection::atomicWrite(copy + "/.key.dpapi", "unavailable-old-Windows-profile");
    QVERIFY_EXCEPTION_THROWN(LocalStoreSecurity::accountKey(copy), std::runtime_error);
    QCOMPARE(KeyRecovery::importKeys(copied.path(), recovery, "strong-recovery-password"), 1);
    QCOMPARE(readMessage(copy, LocalStoreSecurity::accountKey(copy)),
             QString("private-message-secret-中文"));
    QFile image(copy + "/attachments/test-image");
    QVERIFY(image.open(QIODevice::ReadOnly));
    QCOMPARE(
        LocalDataProtection::decryptPayload(image.readAll(), LocalStoreSecurity::accountKey(copy)),
        QByteArray("private-image-secret"));
#endif
  }
  void privatePermissions()
  {
#ifdef Q_OS_WIN
    QTemporaryDir root;
    QVERIFY(root.isValid());
    LocalDataProtection::secureDirectory(root.path());
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto path = QDir::toNativeSeparators(root.path()).toStdWString();
    QCOMPARE(GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                                   DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr,
                                   &descriptor),
             DWORD(ERROR_SUCCESS));
    auto cleanup = qScopeGuard([&] { LocalFree(descriptor); });
    SECURITY_DESCRIPTOR_CONTROL control;
    DWORD revision;
    QVERIFY(GetSecurityDescriptorControl(descriptor, &control, &revision));
    QVERIFY(control & SE_DACL_PROTECTED);
    QCOMPARE(acl->AceCount, WORD(3));
    QVERIFY_EXCEPTION_THROWN(LocalDataProtection::secureDirectory("D:/"), std::runtime_error);
#endif
  }
  void encryptedWalAndNoPlainFallback()
  {
#ifdef Q_OS_WIN
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const auto key = LocalStoreSecurity::prepareAccount(root.path());
    const QString path = root.path() + "/sql.sqlite";
    const QString name = QUuid::createUuid().toString();
    auto cleanup = qScopeGuard([&] { QSqlDatabase::removeDatabase(name); });
    auto database = QSqlDatabase::addDatabase(LocalStoreSecurity::driverName(), name);
    database.setDatabaseName(path);
    LocalStoreSecurity::configureDatabase(database, key);
    QVERIFY(database.open());
    QSqlQuery query(database);
    QVERIFY(query.exec("PRAGMA journal_mode=WAL"));
    QVERIFY(query.exec("CREATE TABLE messages(content TEXT)"));
    QVERIFY(query.exec("INSERT INTO messages VALUES('secret-must-not-leak-into-wal')"));
    for (const auto &suffix : {QString(), QString("-wal")}) {
      QFile file(path + suffix);
      QVERIFY(file.open(QIODevice::ReadOnly));
      QVERIFY(!file.readAll().contains("secret-must-not-leak-into-wal"));
    }
    const QString plainName = QUuid::createUuid().toString();
    auto removePlain = qScopeGuard([&] { QSqlDatabase::removeDatabase(plainName); });
    auto plain = QSqlDatabase::addDatabase("QSQLITE", plainName);
    plain.setDatabaseName(path);
    plain.setConnectOptions("QSQLITE_OPEN_READONLY");
    bool leaked = false;
    if (plain.open()) {
      QSqlQuery attempt(plain);
      leaked = attempt.exec("SELECT content FROM messages");
    }
    QVERIFY(!leaked);
#endif
  }
};
QTEST_MAIN(SecurityTests)
#include "security_tests.moc"
