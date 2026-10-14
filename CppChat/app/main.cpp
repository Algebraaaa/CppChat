#include "app/mainwindow.h"
#include "app/securitycommands.h"
#include "app/thememanager.h"
#include "common/global.h"
#include "common/logger.h"
#include "common/runtimepaths.h"
#include "common/securitypolicy.h"
#include "dialogs/updatedialog.h"
#include "services/clientupdateservice.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QSettings>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>

#include <cstdlib>

int main(int argc, char *argv[])
{
  QApplication a(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Algebra"));
  QCoreApplication::setApplicationName(QStringLiteral("CppChat"));
  QCoreApplication::setApplicationVersion(QStringLiteral(CPPCHAT_VERSION));
  QCommandLineParser arguments;
  arguments.addHelpOption();
  arguments.addVersionOption();
  arguments.addOptions({{"config", "Runtime configuration file", "file"},
                        {"data-dir", "Private data and preferences directory", "directory"},
                        {"log-dir", "Log directory", "directory"}});
  SecurityCommands::registerArguments(arguments);
  arguments.process(a);
  RuntimePaths::configure(arguments.value("config"), arguments.value("data-dir"),
                          arguments.value("log-dir"));
  if (const auto result = SecurityCommands::execute(a, arguments))
    return *result;

  if (Logger::install()) {
    qInfo() << "Log directory:" << Logger::logDirectory();
  } else {
    qWarning() << "Failed to initialize file logger";
  }

  QString themeError;
  if (!ThemeManager::apply(a, &themeError)) {
    qCritical() << "Failed to load UI theme:" << themeError;
    return EXIT_FAILURE;
  }
  qInfo() << "UI theme loaded";

  // 返回当前可执行程序所在的文件夹，不包含 exe 文件名

  // 在配置目录后拼接文件名，得到运行时配置文件的完整路径
  const QString configPath = RuntimePaths::configFile();

  if (!QFile::exists(configPath)) {
    qCritical() << "Config file not found:" << configPath;
    return EXIT_FAILURE;
  }

  // 以 INI 格式读取运行时配置文件
  QSettings settings(configPath, QSettings::IniFormat);
  if (SecurityPolicy::requiresTls() &&
      (settings.value("GateServer/scheme").toString().trimmed().toLower() != "https" ||
       !settings.value("ChatServer/tls", false).toBool() ||
       !settings.value("ResourceServer/tls", false).toBool())) {
    QMessageBox::critical(nullptr, QObject::tr("连接配置错误"),
                          QObject::tr("正式版必须启用 HTTPS 和 TLS，请恢复安全的服务器配置。"));
    return EXIT_FAILURE;
  }
  // 进入 [GateServer] 分组，接下来可以直接通过 host、port 取值
  settings.beginGroup("GateServer");
  // 读取 host；第一个参数：是配置项名称。第二个参数：是默认值
  const QString gateHost = settings.value("host", "127.0.0.1").toString().trimmed();
  // portOk 用于接收“端口能否成功转换成整数”的结果
  bool portOk = false;
  const int gatePort = settings.value("port", 8090).toInt(&portOk);
  const QString gateScheme = settings.value("scheme", "http").toString().trimmed().toLower();
  settings.endGroup();
  // 检查读取配置文件的过程中是否发生访问或格式错误
  if (settings.status() != QSettings::NoError) {
    qCritical() << "Failed to read config:" << configPath;
    return EXIT_FAILURE;
  }
  // 主机地址不能为空，否则无法构造有效的服务器 URL
  if (gateHost.isEmpty()) {
    qCritical() << "GateServer/host must not be empty. Config:" << configPath;
    return EXIT_FAILURE;
  }

  // TCP/UDP 端口必须能够转换为整数，并且位于 1～65535 范围内
  if (!portOk || gatePort < 1 || gatePort > 65535) {
    qCritical() << "Invalid GateServer/port:" << gatePort << "Config:" << configPath;
    return EXIT_FAILURE;
  }

  if (gateScheme != QStringLiteral("http") && gateScheme != QStringLiteral("https")) {
    qCritical() << "GateServer/scheme must be http or https. Config:" << configPath;
    return EXIT_FAILURE;
  }
  if ((gateScheme == QStringLiteral("https") || settings.value("ChatServer/tls", false).toBool() ||
       settings.value("ResourceServer/tls", false).toBool()) &&
      !QSslSocket::supportsSsl()) {
    qCritical() << "TLS is unavailable. Check the Qt TLS plugins beside the client executable.";
    return EXIT_FAILURE;
  }

  QUrl gateUrl;
  gateUrl.setScheme(gateScheme);
  gateUrl.setHost(gateHost);
  gateUrl.setPort(gatePort);
  // 检查组合后的 URL 是否有效，并确认其中确实包含主机地址
  if (!gateUrl.isValid() || gateUrl.host().isEmpty()) {
    qCritical() << "Invalid GateServer URL:" << gateUrl << "Config:" << configPath;
    return EXIT_FAILURE;
  }
  // 保存全局服务器地址前缀，例如 http://127.0.0.1:8090
  // StripTrailingSlash 可以去掉末尾多余的斜杠，方便后面拼接接口路径
  gate_url_prefix = gateUrl.toString(QUrl::StripTrailingSlash);

  qInfo() << "Config file:" << configPath;
  qInfo() << "GateServer URL:" << gate_url_prefix;

  ClientUpdateService updates(ClientUpdateService::manifestUrlForServer(gateUrl),
                              QCoreApplication::applicationVersion());
  bool installRequested = false;
  bool updatesStarted = false;
  int result = EXIT_SUCCESS;
  {
    MainWindow w;
    UpdateDialog updateDialog(&updates, &w);
    QObject::connect(&w, &MainWindow::checkUpdatesRequested, &updateDialog, [&]() {
      updatesStarted = true;
      updateDialog.checkManually();
    });
    QObject::connect(&updateDialog, &UpdateDialog::installRequested, &w, [&]() {
      installRequested = true;
      updateDialog.hide();
      w.close();
      a.quit();
    });
    w.show();
#ifdef Q_OS_WIN
    // A local HTTP development server does not provide trusted releases.
    if (!ClientUpdateService::manifestUrlForServer(gateUrl).isEmpty())
      QTimer::singleShot(2000, &updates, [&]() {
        if (!updatesStarted) {
          updatesStarted = true;
          updates.check();
        }
      });
#endif
    result = a.exec();
  } // Save drafts and close sessions/SQLite before launching the installer.
  if (installRequested) {
    QString error;
    if (!updates.launchInstaller(QCoreApplication::applicationDirPath(), error)) {
      QMessageBox::critical(nullptr, QObject::tr("更新安装失败"), error);
      return EXIT_FAILURE;
    }
  }
  return result;
}
