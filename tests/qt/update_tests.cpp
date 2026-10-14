#include "app/mainwindow.h"
#include "app/thememanager.h"
#include "dialogs/updatedialog.h"
#include "services/clientupdateservice.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkReply>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <cstring>

namespace {
const QUrl kManifestUrl("https://updates.example.test/updates/windows/latest.json");
const QByteArray kInstaller("test-installer-payload");
QJsonObject manifest(const QString &version = "0.1.1")
{
  return {
      {"version", version},
      {"notes", "修复登录与聊天"},
      {"downloadUrl", "./CppChat-Setup-0.1.1-x64.exe"},
      {"size", kInstaller.size()},
      {"sha256", QString::fromLatin1(
                     QCryptographicHash::hash(kInstaller, QCryptographicHash::Sha256).toHex())}};
}
struct Response {
  QByteArray data;
  int status = 200;
  QNetworkReply::NetworkError error = QNetworkReply::NoError;
  bool stall = false;
};
// Exercise the public HTTPS transport contract without trusting a test CA globally.
class Reply : public QNetworkReply {
public:
  Reply(const QNetworkRequest &request, const Response &response, QObject *parent)
      : QNetworkReply(parent), _response(response)
  {
    setRequest(request);
    setUrl(request.url());
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    QTimer::singleShot(0, this, [this]() {
      if (_aborted)
        return;
      _available = true;
      emit readyRead();
      if (_aborted || _response.stall)
        return;
      if (_response.error != NoError)
        setError(_response.error, "scripted transport failure");
      setFinished(true);
      emit finished();
    });
  }
  void abort() override
  {
    _aborted = true;
    setFinished(true);
  }
  qint64 bytesAvailable() const override
  {
    return (_available ? _response.data.size() - _position : 0) + QNetworkReply::bytesAvailable();
  }

protected:
  qint64 readData(char *data, qint64 maxSize) override
  {
    if (!_available || _position == _response.data.size())
      return -1;
    const qint64 size = qMin(maxSize, _response.data.size() - _position);
    std::memcpy(data, _response.data.constData() + _position, static_cast<size_t>(size));
    _position += size;
    return size;
  }

private:
  Response _response;
  qint64 _position = 0;
  bool _available = false;
  bool _aborted = false;
};
class Network : public QNetworkAccessManager {
public:
  QList<Response> responses;
  QList<QNetworkRequest> requests;

protected:
  QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
  {
    requests.append(request);
    const Response response = responses.isEmpty() ? Response{{}, 404} : responses.takeFirst();
    return new Reply(request, response, this);
  }
};
} // namespace

class UpdateTests : public QObject {
  Q_OBJECT
private slots:
  void initTestCase()
  {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("CppChatTests");
    QCoreApplication::setApplicationName("UpdateTests");
  }
  void manifestValidation_data()
  {
    QTest::addColumn<QJsonObject>("object");
    QTest::addColumn<bool>("accepted");
    QTest::newRow("valid-relative-url") << manifest() << true;
    auto value = manifest();
    value["downloadUrl"] = "https://evil.example.test/setup.exe";
    QTest::newRow("foreign-origin") << value << false;
    value["downloadUrl"] = "http://updates.example.test/setup.exe";
    QTest::newRow("http-downgrade") << value << false;
    value["downloadUrl"] = "https://updates.example.test:444/setup.exe";
    QTest::newRow("foreign-port") << value << false;
    value["downloadUrl"] = "https://user:pass@updates.example.test/setup.exe";
    QTest::newRow("url-credentials") << value << false;
    value = manifest();
    value["downloadUrl"] = "./update.cmd";
    QTest::newRow("non-installer") << value << false;
    value = manifest();
    value["sha256"] = "abcd";
    QTest::newRow("invalid-hash") << value << false;
    value = manifest();
    value["size"] = 512.0 * 1024 * 1024 + 1;
    QTest::newRow("oversize") << value << false;
    value["size"] = 1.5;
    QTest::newRow("fractional-size") << value << false;
    value = manifest();
    value["version"] = "0.1.1 && command";
    QTest::newRow("invalid-version") << value << false;
    value = manifest();
    value["downloadUrl"] = "./setup.exe?token=x";
    QTest::newRow("query-url") << value << false;
  }
  void manifestValidation()
  {
    QFETCH(QJsonObject, object);
    QFETCH(bool, accepted);
    ClientUpdateInfo info;
    QString error;
    QCOMPARE(ClientUpdateService::parseManifest(QJsonDocument(object).toJson(), kManifestUrl, info,
                                                error),
             accepted);
    QCOMPARE(error.isEmpty(), accepted);
  }
  void versionComparison_data()
  {
    QTest::addColumn<QString>("current");
    QTest::addColumn<QString>("remote");
    QTest::addColumn<bool>("available");
    QTest::newRow("newer") << "0.1.0" << "0.1.1" << true;
    QTest::newRow("equal") << "0.1.1" << "0.1.1" << false;
    QTest::newRow("older") << "0.2.0" << "0.1.1" << false;
    QTest::newRow("numeric-not-lexical") << "0.2.0" << "0.10.0" << true;
  }
  void versionComparison()
  {
    QFETCH(QString, current);
    QFETCH(QString, remote);
    QFETCH(bool, available);
    Network network;
    network.responses = {{QJsonDocument(manifest(remote)).toJson()}};
    ClientUpdateService service(kManifestUrl, current, nullptr, &network);
    QSignalSpy checked(&service, &ClientUpdateService::checkFinished);
    service.check();
    service.check(); // Busy checks cannot duplicate requests.
    QTRY_COMPARE(checked.count(), 1);
    QCOMPARE(checked.at(0).at(0).toBool(), available);
    QCOMPARE(network.requests.size(), 1);
    QCOMPARE(network.requests.first().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
             int(QNetworkRequest::ManualRedirectPolicy));
  }
  void downloadVerification_data()
  {
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<int>("status");
    QTest::addColumn<int>("networkError");
    QTest::addColumn<bool>("accepted");
    QTest::newRow("verified") << kInstaller << 200 << int(QNetworkReply::NoError) << true;
    QTest::newRow("changed-hash") << QByteArray(kInstaller.size(), 'x') << 200 << 0 << false;
    QTest::newRow("truncated") << kInstaller.left(5) << 200 << 0 << false;
    QTest::newRow("oversize") << (kInstaller + "x") << 200 << 0 << false;
    QTest::newRow("redirect") << kInstaller << 302 << 0 << false;
    QTest::newRow("network-error")
        << kInstaller << 200 << int(QNetworkReply::RemoteHostClosedError) << false;
    QTest::newRow("tls-error") << QByteArray() << 0 << int(QNetworkReply::SslHandshakeFailedError)
                               << false;
  }
  void downloadVerification()
  {
    QFETCH(QByteArray, bytes);
    QFETCH(int, status);
    QFETCH(int, networkError);
    QFETCH(bool, accepted);
    Network network;
    network.responses = {{QJsonDocument(manifest()).toJson()},
                         {bytes, status, QNetworkReply::NetworkError(networkError)}};
    ClientUpdateService service(kManifestUrl, "0.1.0", nullptr, &network);
    QSignalSpy checked(&service, &ClientUpdateService::checkFinished);
    QSignalSpy ready(&service, &ClientUpdateService::downloadReady);
    QSignalSpy failed(&service, &ClientUpdateService::failed);
    service.check();
    QTRY_COMPARE(checked.count(), 1);
    service.download();
    service.download();
    QTRY_VERIFY(ready.count() + failed.count() == 1);
    QCOMPARE(ready.count(), accepted ? 1 : 0);
    QCOMPARE(network.requests.size(), 2);
    if (accepted) {
      const auto path = ready.first().first().toString();
      QFile file(path);
      QVERIFY(file.open(QIODevice::ReadOnly));
      QCOMPARE(file.readAll(), kInstaller);
      file.close();
      service.cancel();
      QVERIFY(!QFile::exists(path));
    } else {
      QCOMPARE(service.state(), ClientUpdateService::State::Available);
      network.responses = {{kInstaller}};
      service.download();
      QTRY_COMPARE(ready.count(), 1); // Failure is retryable.
    }
  }
  void malformedAndLargeManifest()
  {
    for (const auto &json : {QByteArray("{"), QByteArray(65537, ' ')}) {
      Network network;
      network.responses = {{json}};
      ClientUpdateService service(kManifestUrl, "0.1.0", nullptr, &network);
      QSignalSpy failed(&service, &ClientUpdateService::failed);
      service.check();
      QTRY_COMPARE(failed.count(), 1);
      QCOMPARE(service.state(), ClientUpdateService::State::Idle);
    }
  }
  void cancellation()
  {
    Network network;
    network.responses = {{QJsonDocument(manifest()).toJson()},
                         {kInstaller.left(5), 200, QNetworkReply::NoError, true}};
    ClientUpdateService service(kManifestUrl, "0.1.0", nullptr, &network);
    QSignalSpy checked(&service, &ClientUpdateService::checkFinished);
    QSignalSpy ready(&service, &ClientUpdateService::downloadReady);
    service.check();
    QTRY_COMPARE(checked.count(), 1);
    service.download();
    service.cancel();
    QTest::qWait(20);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(service.state(), ClientUpdateService::State::Available);
    network.responses = {{kInstaller}};
    service.download();
    QTRY_COMPARE(ready.count(), 1);
  }
  void dialogWorkflow()
  {
    Network network;
    network.responses = {{QJsonDocument(manifest()).toJson()}, {kInstaller}};
    ClientUpdateService service(kManifestUrl, "0.1.0", nullptr, &network);
    UpdateDialog dialog(&service);
    QSignalSpy install(&dialog, &UpdateDialog::installRequested);
    dialog.checkManually();
    QTRY_COMPARE(service.state(), ClientUpdateService::State::Available);
    auto *action = dialog.findChild<QPushButton *>("update_action");
    QVERIFY(action);
    QCOMPARE(action->text(), QString("下载更新"));
    QTest::mouseClick(action, Qt::LeftButton);
    QTRY_COMPARE(service.state(), ClientUpdateService::State::Ready);
    QCOMPARE(action->text(), QString("立即安装"));
    QTest::mouseClick(action, Qt::LeftButton);
    QCOMPARE(install.count(), 1); // The dialog requests shutdown; it never executes an EXE.
    QVERIFY(!action->isEnabled());
  }
  void silentStartupFailure()
  {
    Network network;
    network.responses = {{QByteArray(), 404}};
    ClientUpdateService service(kManifestUrl, "0.1.1", nullptr, &network);
    UpdateDialog dialog(&service);
    QSignalSpy failed(&service, &ClientUpdateService::failed);
    service.check();
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(!dialog.isVisible());
  }
  void deadlineAndCancelCheck()
  {
    Network network;
    network.responses = {{QByteArray(), 200, QNetworkReply::NoError, true}};
    ClientUpdateService service(kManifestUrl, "0.1.0", nullptr, &network);
    QSignalSpy failed(&service, &ClientUpdateService::failed);
    service.check();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 17000);
    QVERIFY(!service.busy());
    network.responses = {{QByteArray(), 200, QNetworkReply::NoError, true}};
    service.check();
    service.cancel();
    QTest::qWait(20);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(service.state(), ClientUpdateService::State::Idle);
  }
  void changedInstallerCannotExecute()
  {
    Network network;
    network.responses = {{QJsonDocument(manifest()).toJson()}, {kInstaller}};
    ClientUpdateService service(kManifestUrl, "0.1.0", nullptr, &network);
    QSignalSpy checked(&service, &ClientUpdateService::checkFinished);
    QSignalSpy ready(&service, &ClientUpdateService::downloadReady);
    service.check();
    QTRY_COMPARE(checked.count(), 1);
    service.download();
    QTRY_COMPARE(ready.count(), 1);
    QFile file(ready.first().first().toString());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(kInstaller.size(), 'x'));
    file.close();
    QString error;
    QVERIFY(!service.launchInstaller(QCoreApplication::applicationDirPath(), error));
    QVERIFY(!error.isEmpty());
  }
  void originalInstallationDirectory()
  {
    const auto args = ClientUpdateService::installerArguments("D:/Install With Spaces/CppChat");
    QCOMPARE(args.first(), "/DIR=" + QDir::toNativeSeparators("D:/Install With Spaces/CppChat"));
    QVERIFY(!args.contains("/VERYSILENT"));
    QCOMPARE(ClientUpdateService::manifestUrlForServer(QUrl("https://server.test:443")),
             QUrl("https://server.test:443/updates/windows/latest.json"));
    QVERIFY(ClientUpdateService::manifestUrlForServer(QUrl("http://127.0.0.1:8090")).isEmpty());
  }
  void nativeInstallerHandoff()
  {
#ifdef Q_OS_WIN
    QFile helper(QCoreApplication::applicationDirPath() + "/installer_handoff_probe.exe");
    QVERIFY(helper.open(QIODevice::ReadOnly));
    const auto bytes = helper.readAll();
    auto object = manifest();
    object["size"] = bytes.size();
    object["sha256"] =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    Network network;
    network.responses = {{QJsonDocument(object).toJson()}, {bytes}};
    ClientUpdateService service(kManifestUrl, "0.1.0", nullptr, &network);
    QSignalSpy checked(&service, &ClientUpdateService::checkFinished);
    QSignalSpy ready(&service, &ClientUpdateService::downloadReady);
    service.check();
    QTRY_COMPARE(checked.count(), 1);
    service.download();
    QTRY_COMPARE(ready.count(), 1);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString directory = root.filePath("Install With Spaces 测试 & Folder");
    QVERIFY(QDir().mkpath(directory));
    QString error;
    QVERIFY2(service.launchInstaller(directory, error), qPrintable(error));
    QFile recorded(directory + "/handoff.txt");
    QTRY_VERIFY(recorded.exists());
    QVERIFY(recorded.open(QIODevice::ReadOnly));
    const auto commandBytes = recorded.readAll();
    const auto command = QString::fromUtf16(
        reinterpret_cast<const char16_t *>(commandBytes.constData()), commandBytes.size() / 2);
    QVERIFY(command.contains("/DIR=" + QDir::toNativeSeparators(directory)));
    QVERIFY(command.contains("/NORESTART"));
    QVERIFY(command.contains("/SP-"));
    recorded.close();
    const QString retained = ready.first().first().toString();
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(retained) || QFile::remove(retained), 3000);
    QVERIFY(QDir().rmdir(QFileInfo(retained).absolutePath()));
#endif
  }
  void updateEntryAndPresentation()
  {
    QString error;
    QVERIFY2(ThemeManager::apply(*qApp, &error), qPrintable(error));
    MainWindow window;
    window.show();
    auto *button = window.findChild<QPushButton *>("check_updates_btn");
    QVERIFY(button);
    QSignalSpy requested(&window, &MainWindow::checkUpdatesRequested);
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(requested.count(), 1);
    QVERIFY(button->isVisible());
    QVERIFY(window.rect().contains(button->mapTo(&window, button->rect().bottomRight())));
    Network network;
    network.responses = {{QJsonDocument(manifest()).toJson()}};
    ClientUpdateService service(kManifestUrl, "0.1.0", nullptr, &network);
    UpdateDialog dialog(&service, &window);
    service.check();
    QTRY_VERIFY(dialog.isVisible());
    const QString screenshots = qEnvironmentVariable("CPPCHAT_UPDATE_SCREENSHOT_DIRECTORY");
    if (!screenshots.isEmpty()) {
      QVERIFY(window.grab().save(screenshots + "/login-update-entry.png"));
      QVERIFY(dialog.grab().save(screenshots + "/update-available.png"));
    }
  }
};
QTEST_MAIN(UpdateTests)
#include "update_tests.moc"
