#include "services/clientupdateservice.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <iostream>

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  app.setOrganizationName("CppChatTests");
  app.setApplicationName("UpdateProbe");
  const auto arguments = app.arguments();
  if (arguments.size() < 3)
    return 2;
  ClientUpdateService updates(QUrl(arguments.at(1)), arguments.at(2));
  QObject::connect(&updates, &ClientUpdateService::failed, &app, [&](const QString &error) {
    std::cerr << error.toStdString() << std::endl;
    app.exit(1);
  });
  QObject::connect(&updates, &ClientUpdateService::checkFinished, &app, [&](bool available) {
    const QJsonObject result{{"version", updates.info().version}, {"available", available}};
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
    if (available && arguments.contains("--download"))
      updates.download();
    else
      app.quit();
  });
  QObject::connect(&updates, &ClientUpdateService::downloadReady, &app, [&](const QString &) {
    std::cout << "PASS: HTTPS installer download and SHA256 verification" << std::endl;
    app.quit(); // Service destruction removes the probe's downloaded installer.
  });
  QTimer::singleShot(0, &updates, &ClientUpdateService::check);
  return app.exec();
}
