#include "mobileaccount.h"
#include "appbridge.h"
#include "common/logger.h"
#include "common/runtimepaths.h"
#include "network/tcpmgr.h"
#include "network/usermgr.h"
#include "services/clientupdateservice.h"
#include <qtkeychain/keychain.h>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

class MobileFeatures : public QObject
{
  Q_OBJECT
private slots:
  void profileResponseUpdatesCacheBeforeNotification()
  {
    QVERIFY(QDir().mkpath("build"));
    QTemporaryDir directory(QDir::currentPath() + "/build/mobile-profile-XXXXXX");
    QVERIFY(directory.isValid());
    QFile config(directory.filePath("config.ini"));
    QVERIFY(config.open(QIODevice::WriteOnly));
    config.write("[GateServer]\nurl=https://profile-test.example.invalid\n");
    config.close();
    auto shutdownLogger = qScopeGuard([] { Logger::shutdown(); });
    QString observedNick, observedDescription;
    AppBridge bridge;
    QVERIFY(bridge.init(directory.path()));
    auto manager = UserMgr::GetInstance();
    manager->SetUserInfo(std::make_shared<UserInfo>(71001, "old-name", "", "", 0));
    QSignalSpy changed(&bridge, &AppBridge::loginStatusChanged);
    connect(&bridge, &AppBridge::loginStatusChanged, this, [&] {
      observedNick = bridge.currentNick();
      observedDescription = bridge.currentDescription();
    });
    QJsonObject profile{{"uid", 71001}, {"name", "saved-name"},
                        {"nick", "saved-nick"}, {"desc", "saved-description"}, {"error", 0}};
    emit TcpMgr::GetInstance()->sig_profile_updated(profile);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(bridge.currentUser(), QString("saved-name"));
    QCOMPARE(observedNick, QString("saved-nick"));
    QCOMPARE(observedDescription, QString("saved-description"));
    profile["uid"] = 71002;
    profile["nick"] = "another-account";
    emit TcpMgr::GetInstance()->sig_profile_updated(profile);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(bridge.currentNick(), QString("saved-nick"));
    profile["uid"] = 71001;
    profile["error"] = RedisError;
    profile["committed"] = true;
    profile["nick"] = "committed-nick";
    emit TcpMgr::GetInstance()->sig_profile_updated(profile);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(observedNick, QString("committed-nick"));
    manager->Reset();
    emit TcpMgr::GetInstance()->sig_profile_updated(profile);
    QCOMPARE(changed.count(), 2);
  }
  void secureRememberRestartLogoutAndForget()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    RuntimePaths::configure(directory.path() + "/config.ini", directory.path(),
                            directory.path() + "/logs");
    const QString origin("https://mobile-test.example.invalid");
    const QString email("mobile-test@example.invalid"), password("SyntheticPassword2468");
    MobileAccount first;
    first.initialize(origin);
    auto cleanup = qScopeGuard([&] {
      QSignalSpy removed(&first, &MobileAccount::secretRemoved);
      first.forget();
      removed.wait(10000);
    });
    QVERIFY(first.ready());
    QVERIFY(first.email().isEmpty());
    first.authenticated(email, password);
    QVERIFY(first.password().isEmpty());
    first.setAutomatic(true); // Enabling from the signed-in settings page must persist too.
    QVERIFY(first.remember());
    // QtKeychain serializes jobs, so the subsequent read observes the finished write.
    MobileAccount restored;
    restored.initialize(origin);
    QTRY_VERIFY_WITH_TIMEOUT(restored.ready(), 10000);
    QCOMPARE(restored.email(), email);
    QCOMPARE(restored.password(), password);
    QVERIFY(restored.automatic());
    restored.signedOut();
    QVERIFY(!restored.automatic());
    QVERIFY(restored.remember());
    QDirIterator files(directory.path(), QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
      QFile file(files.next());
      QVERIFY(file.open(QIODevice::ReadOnly));
      QVERIFY2(!file.readAll().contains(password.toUtf8()), "Password leaked to profile files");
    }
    QSignalSpy removed(&restored, &MobileAccount::secretRemoved);
    restored.forget();
    QVERIFY(removed.wait(10000));
    MobileAccount forgotten;
    forgotten.initialize(origin);
    QVERIFY(forgotten.ready());
    QVERIFY(!forgotten.remember());
    QVERIFY(!forgotten.automatic());
    QVERIFY(forgotten.email().isEmpty());
    QVERIFY(forgotten.password().isEmpty());
  }
  void platformSpecificManifests()
  {
    const QUrl origin("https://updates.example.invalid");
    const auto android = ClientUpdateService::manifestUrlForServer(origin, true);
    QCOMPARE(android.path(), QString("/updates/android/latest.json"));
    QCOMPARE(ClientUpdateService::manifestUrlForServer(origin).path(),
             QString("/updates/windows/latest.json"));
    QJsonObject object{{"version", "0.1.20"},
                       {"notes", "Android update"},
                       {"downloadUrl", "./CppChat-0.1.20-android-arm64.apk"},
                       {"size", 32},
                       {"sha256", QString(64, 'a')}};
    ClientUpdateInfo info;
    QString error;
    const auto parse = [&](bool mobile) {
      return ClientUpdateService::parseManifest(QJsonDocument(object).toJson(), android, info,
                                                error, mobile);
    };
    QVERIFY(parse(true));
    QVERIFY(!parse(false));
    object["downloadUrl"] = "https://other.example.invalid/evil.apk";
    QVERIFY(!parse(true));
    object["downloadUrl"] = "./installer.exe";
    QVERIFY(!parse(true));
    object["downloadUrl"] = "http://updates.example.invalid/app.apk";
    QVERIFY(!parse(true));
    object["downloadUrl"] = "./app.apk";
    object["sha256"] = "invalid";
    QVERIFY(!parse(true));
  }
};
QTEST_GUILESS_MAIN(MobileFeatures)
#include "mobile_features.moc"
