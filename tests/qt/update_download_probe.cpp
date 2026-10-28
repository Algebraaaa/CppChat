#include "services/clientupdateservice.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <cstdio>

// Explicit, bounded diagnostic of the production updater; never runs an installer.
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  if (argc < 2 || argc > 4)
    return 2;
  const auto platform = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString("windows");
  if (platform != "windows" && platform != "android")
    return 2;
  const bool android = platform == "android";
  bool valid = true;
  const qint64 sample = argc > 3 ? QString::fromLocal8Bit(argv[3]).toLongLong(&valid) : 0;
  if (!valid || sample < -512LL * 1024 * 1024 || sample > 512LL * 1024 * 1024)
    return 2;
  ClientUpdateService service(QUrl(QString::fromLocal8Bit(argv[1])), "0.0.0", nullptr, nullptr,
                              android);
  QElapsedTimer elapsed;
  qint64 received = 0;
  qint64 logged = 0;
  bool resumed = false;
  const auto report = [&](bool verified) {
    const QJsonObject value{{"bytes", received},
                            {"milliseconds", elapsed.elapsed()},
                            {"verified_complete_package", verified},
                            {"version", service.info().version},
                            {"resumed", resumed},
                            {"sha256", QString::fromLatin1(service.info().sha256)}};
    const auto json = QJsonDocument(value).toJson(QJsonDocument::Compact);
    std::puts(json.constData());
    std::fflush(stdout);
  };
  QObject::connect(&service, &ClientUpdateService::checkFinished, &app, [&](bool available) {
    if (!available) {
      app.exit(3);
      return;
    }
    elapsed.start();
    service.download();
  });
  QObject::connect(&service, &ClientUpdateService::downloadProgress, &app,
                   [&](qint64 bytes, qint64) {
                     received = bytes;
                     if (bytes - logged >= 4 * 1024 * 1024) {
                       logged = bytes;
                       report(false);
                     }
                     if (sample > 0 && bytes >= sample) {
                       report(false);
                       service.cancel();
                       app.exit(0);
                     } else if (sample < 0 && !resumed && bytes >= -sample) {
                       resumed = true;
                       service.cancel();
                       QTimer::singleShot(25, &service, [&] { service.download(); });
                     }
                   });
  QObject::connect(&service, &ClientUpdateService::downloadReady, &app, [&] {
    report(true);
    app.exit(0);
  });
  QObject::connect(&service, &ClientUpdateService::failed, &app, [&](const QString &error) {
    report(false);
    std::fprintf(stderr, "%s\n", error.toUtf8().constData());
    app.exit(1);
  });
  QTimer::singleShot(240000, &app, [&] {
    report(false);
    service.cancel();
    app.exit(124);
  });
  service.check();
  return app.exec();
}
