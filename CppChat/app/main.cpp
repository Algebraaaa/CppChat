#include "app/mainwindow.h"
#include "common/global.h"
#include "common/logger.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QUrl>

#include <cstdlib>

int main(int argc, char *argv[])
{
  QApplication a(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Algebra"));
  QCoreApplication::setApplicationName(QStringLiteral("CppChat"));

  if (Logger::install()) {
    qInfo() << "Log directory:" << Logger::logDirectory();
  } else {
    qWarning() << "Failed to initialize file logger";
  }

  // ":/" 表示访问编译进 Qt 资源系统中的文件，而不是磁盘上的普通文件。
  QFile qss(":/style/stylesheet.qss");

  if (qss.open(QFile::ReadOnly)) {
    qInfo() << "Stylesheet loaded";

    const QString style = QString::fromUtf8(qss.readAll());
    // 给整个 QApplication 设置样式，所有窗口和子控件都可以使用这份 QSS。
    a.setStyleSheet(style);
    qss.close();
  } else {
    // 样式加载失败不会阻止程序启动，只在输出窗口记录错误。
    qWarning() << "Failed to open stylesheet:" << qss.errorString();
  }

  // 返回当前可执行程序所在的文件夹，不包含 exe 文件名
  const QString configDir = QCoreApplication::applicationDirPath();

  // 在配置目录后拼接文件名，得到运行时配置文件的完整路径
  const QString configPath = QDir(configDir).filePath("config.ini");

  // 配置文件不存在则使用默认值
  if (!QFile::exists(configPath)) {
    // 使用 INI 格式打开目标配置文件
    QSettings defaults(configPath, QSettings::IniFormat);
    // 进入 [GateServer] 配置分组
    defaults.beginGroup("GateServer");
    // 写入默认主机和端口
    defaults.setValue("host", "127.0.0.1");
    defaults.setValue("port", 8090);
    // 退出 [GateServer] 分组
    defaults.endGroup();
    // 立即把内存中的修改同步到磁盘文件
    defaults.sync();
    // 检查默认配置是否真正写入成功。
    if (defaults.status() != QSettings::NoError) {
      qCritical() << "Failed to create default config:" << configPath;
      return EXIT_FAILURE;
    }
  }

  // 以 INI 格式读取运行时配置文件。
  QSettings settings(configPath, QSettings::IniFormat);
  // 进入 [GateServer] 分组，接下来可以直接通过 host、port 取值。
  settings.beginGroup("GateServer");
  // 读取 host；第一个参数：是配置项名称。第二个参数：是默认值。
  const QString gateHost = settings.value("host", "127.0.0.1").toString().trimmed();
  // portOk 用于接收“端口能否成功转换成整数”的结果。
  bool portOk = false;
  const int gatePort = settings.value("port", 8090).toInt(&portOk);
  settings.endGroup();
  // 检查读取配置文件的过程中是否发生访问或格式错误
  if (settings.status() != QSettings::NoError) {
    qCritical() << "Failed to read config:" << configPath;
    return EXIT_FAILURE;
  }
  // 主机地址不能为空，否则无法构造有效的服务器 URL。
  if (gateHost.isEmpty()) {
    qCritical() << "GateServer/host must not be empty. Config:" << configPath;
    return EXIT_FAILURE;
  }

  // TCP/UDP 端口必须能够转换为整数，并且位于 1～65535 范围内。
  if (!portOk || gatePort < 1 || gatePort > 65535) {
    qCritical() << "Invalid GateServer/port:" << gatePort << "Config:" << configPath;
    return EXIT_FAILURE;
  }

  // 使用 QUrl 分步骤构造服务器基础地址，避免手工拼接字符串产生格式错误。
  QUrl gateUrl;

  // 当前 GateServer 使用普通 HTTP 协议。
  gateUrl.setScheme("http");

  // 设置从配置文件读取到的服务器主机地址。
  gateUrl.setHost(gateHost);

  // 设置从配置文件读取到的服务器端口。
  gateUrl.setPort(gatePort);

  // 再次检查组合后的 URL 是否有效，并确认其中确实包含主机地址。
  if (!gateUrl.isValid() || gateUrl.host().isEmpty()) {
    qCritical() << "Invalid GateServer URL:" << gateUrl << "Config:" << configPath;
    return EXIT_FAILURE;
  }

  // 保存全局服务器地址前缀，例如 http://127.0.0.1:8090
  // StripTrailingSlash 可以去掉末尾多余的斜杠，方便后面拼接接口路径
  gate_url_prefix = gateUrl.toString(QUrl::StripTrailingSlash);

  qInfo() << "Config file:" << configPath;
  qInfo() << "GateServer URL:" << gate_url_prefix;

  MainWindow w;
  w.show();
  return a.exec();
}
