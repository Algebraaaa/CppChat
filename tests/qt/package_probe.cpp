#include "app/thememanager.h"
#include "common/localdataprotection.h"
#include "common/localstoresecurity.h"
#include "services/videomedia.h"
#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QProcess>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSslSocket>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setApplicationVersion(QStringLiteral(CPPCHAT_VERSION));
  QString themeError;
  const bool theme = ThemeManager::apply(app, &themeError);
  const bool icon = !QIcon(":/res/app.ico").isNull();
  const bool image = !QPixmap(":/res/head_1.jpg").isNull();
  QImage cameraFrame(320, 240, QImage::Format_RGB32);
  cameraFrame.fill(Qt::blue);
  const bool videoCodec = !VideoMedia::decodeFrame(VideoMedia::encodeFrame(cameraFrame)).isNull();
  auto database = QSqlDatabase::addDatabase("QSQLITE");
  database.setDatabaseName(":memory:");
  const bool opened = database.open();
  QSqlQuery query(database);
  const bool sqlite =
      opened && query.exec("SELECT 1") && query.next() && query.value(0).toInt() == 1;
  const bool tls = QSslSocket::supportsSsl();
  QTemporaryDir privateStore;
  bool encryptedStore = false;
  try {
    const auto key = LocalStoreSecurity::prepareAccount(privateStore.path());
    auto encryptedDatabase =
        QSqlDatabase::addDatabase(LocalStoreSecurity::driverName(), "package-encryption");
    encryptedDatabase.setDatabaseName(privateStore.path() + "/encrypted.sqlite");
    LocalStoreSecurity::configureDatabase(encryptedDatabase, key);
    QSqlQuery encryptedQuery(encryptedDatabase);
    encryptedStore = encryptedDatabase.open() &&
                     encryptedQuery.exec("CREATE TABLE secrets(value TEXT)") &&
                     encryptedQuery.exec("INSERT INTO secrets VALUES('private-package-probe')");
    const auto bytes = LocalDataProtection::encryptPayload("private-image", key);
    encryptedStore =
        encryptedStore && LocalDataProtection::decryptPayload(bytes, key) == "private-image";
  } catch (...) {
    encryptedStore = false;
  }
  QProcess versionProbe;
  versionProbe.start(QDir(app.applicationDirPath()).filePath("CppChat.exe"), {"--version"});
  const bool versionMatches = versionProbe.waitForFinished(5000) && versionProbe.exitCode() == 0 &&
                              QString::fromUtf8(versionProbe.readAllStandardOutput()).trimmed() ==
                                  "CppChat " + app.applicationVersion();
  QSettings config(QDir(QCoreApplication::applicationDirPath()).filePath("config.ini"),
                   QSettings::IniFormat);
  const bool publicServer = config.status() == QSettings::NoError &&
                            config.value("GateServer/host").toString() != "127.0.0.1" &&
                            config.value("GateServer/scheme").toString() == "https" &&
                            config.value("ChatServer/tls").toBool() &&
                            config.value("ResourceServer/tls").toBool();
  const QJsonObject result{{"theme", theme},
                           {"videoCodec", videoCodec},
                           {"icon", icon},
                           {"image", image},
                           {"sqlite", sqlite},
                           {"tls", tls},
                           {"publicServer", publicServer},
                           {"themeError", themeError},
                           {"version", app.applicationVersion()},
                           {"versionMatches", versionMatches},
                           {"encryptedStore", encryptedStore}};
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
  return theme && icon && image && videoCodec && sqlite && tls && publicServer && versionMatches &&
                 encryptedStore
             ? 0
             : 1;
}
