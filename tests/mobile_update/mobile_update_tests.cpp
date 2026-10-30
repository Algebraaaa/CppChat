#include "update/mobileupdateservice.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <atomic>

namespace {
QJsonObject updateManifest()
{
  return {{"version", "0.1.25"},
          {"notes", "background update"},
          {"downloadUrl", "https://updates.example.invalid/updates/android/app.apk"},
          {"sha256", QString(64, 'a')},
          {"size", 1000}};
}
class SystemDownload final : public MobileDownloadBackend {
public:
  QJsonObject record{{"manifest", updateManifest()}, {"status", 2}, {"received", 410}};
  QJsonObject verification{{"verified", true}, {"path", "/private/verified.apk"}};
  std::atomic<int> cancelled{0}, started{0}, verified{0}, installed{0};
  QJsonObject inspect() override { return record; }
  QJsonObject start(const QJsonObject &manifest) override
  {
    ++started;
    record = {{"manifest", manifest}, {"status", 2}, {"received", 0}};
    return record;
  }
  QJsonObject cancel() override
  {
    ++cancelled;
    record = {{"empty", true}};
    return {{"cancelled", true}};
  }
  QJsonObject verify() override
  {
    ++verified;
    QThread::msleep(25);
    return verification;
  }
  QJsonObject install() override
  {
    ++installed;
    return {{"launched", true}};
  }
};
class ManifestReply final : public QNetworkReply {
public:
  ManifestReply(const QNetworkRequest &request, const QByteArray &data, QObject *parent)
      : QNetworkReply(parent), data(data)
  {
    setRequest(request);
    setUrl(request.url());
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
    open(QIODevice::ReadOnly);
    QTimer::singleShot(0, this, [this] {
      emit readyRead();
      setFinished(true);
      emit finished();
    });
  }
  void abort() override {}
  qint64 bytesAvailable() const override
  {
    return data.size() - position + QNetworkReply::bytesAvailable();
  }

protected:
  qint64 readData(char *buffer, qint64 maximum) override
  {
    const auto count = qMin(maximum, data.size() - position);
    if (count <= 0)
      return -1;
    memcpy(buffer, data.constData() + position, size_t(count));
    position += count;
    return count;
  }

private:
  QByteArray data;
  qint64 position = 0;
};
class ManifestNetwork final : public QNetworkAccessManager {
public:
  QJsonObject manifest = updateManifest();

protected:
  QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
  {
    return new ManifestReply(request, QJsonDocument(manifest).toJson(), this);
  }
};
const QUrl origin("https://updates.example.invalid/updates/android/latest.json");
using State = MobileUpdateService::State;
} // namespace

class MobileUpdates : public QObject {
  Q_OBJECT
private slots:
  void restoresTaskWithoutStartingAnotherDownload()
  {
    auto system = std::make_shared<SystemDownload>();
    {
      MobileUpdateService first(origin, "0.1.24", nullptr, system);
      QTRY_COMPARE(first.state(), State::Downloading);
      QCOMPARE(first.downloadedBytes(), 410);
    }
    QCOMPARE(system->cancelled.load(), 0);
    MobileUpdateService reopened(origin, "0.1.24", nullptr, system);
    QTRY_COMPARE(reopened.state(), State::Downloading);
    QCOMPARE(reopened.downloadedBytes(), 410);
    QCOMPARE(system->started.load(), 0);
  }
  void completedTaskSurvivesExitAndInstallerCancellation()
  {
    auto system = std::make_shared<SystemDownload>();
    system->record["status"] = 8;
    for (int launch = 0; launch < 2; ++launch) {
      MobileUpdateService reopened(origin, "0.1.24", nullptr, system);
      QTRY_COMPARE(reopened.state(), State::Ready);
      QCOMPARE(reopened.downloadedBytes(), 1000);
    }
    QCOMPARE(system->started.load(), 0);
    QCOMPARE(system->cancelled.load(), 0);
    QCOMPARE(system->installed.load(), 0); // Completion never silently installs.
    QCOMPARE(system->verified.load(), 2);
  }
  void checkingAgainReusesReadyPackageAndAcceptsNewerOffer()
  {
    auto system = std::make_shared<SystemDownload>();
    system->record["status"] = 8;
    ManifestNetwork network;
    MobileUpdateService service(origin, "0.1.24", nullptr, system, &network);
    QTRY_COMPARE(service.state(), State::Ready);
    service.check();
    QTRY_COMPARE(service.state(), State::Ready);
    QCOMPARE(system->started.load(), 0);
    network.manifest["version"] = "0.1.26";
    service.check();
    QTRY_COMPARE(service.state(), State::Available);
    QCOMPARE(service.info().version, QString("0.1.26"));
    QCOMPARE(system->started.load(), 0);
  }
  void networkPausePreservesTaskAndProgress()
  {
    auto system = std::make_shared<SystemDownload>();
    system->record["status"] = 4;
    system->record["reason"] = 2;
    MobileUpdateService service(origin, "0.1.24", nullptr, system);
    QTRY_COMPARE(service.state(), State::Downloading);
    QVERIFY(service.statusText().contains(QString::fromUtf8("暂缓")));
    QCOMPARE(service.downloadedBytes(), 410);
    system->record["status"] = 2;
    system->record["received"] = 720;
    service.refresh();
    QCOMPARE(service.downloadedBytes(), 720);
    QCOMPARE(system->started.load(), 0);
  }
  void explicitCancellationRemovesSystemTask()
  {
    auto system = std::make_shared<SystemDownload>();
    MobileUpdateService service(origin, "0.1.24", nullptr, system);
    QTRY_COMPARE(service.state(), State::Downloading);
    service.cancel();
    QCOMPARE(system->cancelled.load(), 1);
    QCOMPARE(service.state(), State::Available);
    QCOMPARE(service.downloadedBytes(), 0);
    service.download();
    QCOMPARE(system->started.load(), 1);
    QCOMPARE(service.state(), State::Downloading);
  }
  void failedSystemTaskCanBeRetried()
  {
    auto system = std::make_shared<SystemDownload>();
    system->record["status"] = 16;
    MobileUpdateService service(origin, "0.1.24", nullptr, system);
    QSignalSpy failures(&service, &MobileUpdateService::failed);
    QTRY_COMPARE(service.state(), State::Available);
    QCOMPARE(failures.count(), 1);
    service.download();
    QCOMPARE(service.state(), State::Downloading);
  }
  void verificationFailureNeverMakesPackageInstallable()
  {
    auto system = std::make_shared<SystemDownload>();
    system->record["status"] = 8;
    system->verification = {{"error", "wrong package or hash"}};
    MobileUpdateService service(origin, "0.1.24", nullptr, system);
    QSignalSpy failures(&service, &MobileUpdateService::failed);
    QTRY_COMPARE(failures.count(), 1);
    QCOMPARE(service.state(), State::Available);
    QString error;
    QVERIFY(!service.launchInstaller({}, error));
    QCOMPARE(system->installed.load(), 0);
  }
  void invalidPersistedManifest_data()
  {
    QTest::addColumn<QJsonObject>("manifest");
    auto manifest = updateManifest();
    manifest["downloadUrl"] = "https://attacker.invalid/app.apk";
    QTest::newRow("different-origin") << manifest;
    manifest = updateManifest();
    manifest["size"] = -1;
    QTest::newRow("invalid-size") << manifest;
    manifest = updateManifest();
    manifest["version"] = "0.1.24";
    QTest::newRow("already-installed") << manifest;
  }
  void invalidPersistedManifest()
  {
    QFETCH(QJsonObject, manifest);
    auto system = std::make_shared<SystemDownload>();
    system->record["manifest"] = manifest;
    MobileUpdateService service(origin, "0.1.24", nullptr, system);
    QTRY_COMPARE(system->cancelled.load(), 1);
    QCOMPARE(service.state(), State::Idle);
    QCOMPARE(system->started.load(), 0);
  }
  void closingDuringVerificationDoesNotCancelSystemDownload()
  {
    auto system = std::make_shared<SystemDownload>();
    system->record["status"] = 8;
    auto service = std::make_unique<MobileUpdateService>(origin, "0.1.24", nullptr, system);
    QTRY_VERIFY(service->verifying());
    service.reset();
    QTRY_COMPARE(system->verified.load(), 1);
    QCOMPARE(system->cancelled.load(), 0);
  }
};

QTEST_MAIN(MobileUpdates)
#include "mobile_update_tests.moc"
