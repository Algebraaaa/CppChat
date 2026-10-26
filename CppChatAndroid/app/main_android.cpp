#include "appbridge.h"
#include "common/global.h"
#include "common/logger.h"

#include <QGuiApplication>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>

// Android entry point — uses QML instead of QWidget for mobile UI.
int main(int argc, char *argv[])
{
  // Force the Android-style look & feel
  QQuickStyle::setStyle("Material");

  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Algebra"));
  QCoreApplication::setApplicationName(QStringLiteral("CppChat"));
  QFontDatabase::addApplicationFont(":/fonts/SourceHanSansCN-Regular.otf");
  QFontDatabase::addApplicationFont(":/fonts/SourceHanSansCN-Medium.otf");
  app.setFont(QFont(QStringLiteral("Source Han Sans CN"), 11));

  // Initialize the bridge — loads config, logger, connects to backends
  AppBridge bridge;
  bridge.init();

  QQmlApplicationEngine engine;

  // Expose AppBridge to QML as "App"
  engine.rootContext()->setContextProperty("App", &bridge);

  engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));

  if (engine.rootObjects().isEmpty()) {
    qCritical() << "Failed to load QML main.qml";
    return -1;
  }

  return app.exec();
}
