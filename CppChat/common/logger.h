#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QtMessageHandler>

/**
 * @brief 接管 Qt 日志并写入本地日志文件。
 *
 * 业务代码继续使用 qDebug/qInfo/qWarning/qCritical；新模块可以使用
 * QLoggingCategory 和 qCDebug/qCInfo/qCWarning/qCCritical 标记日志来源。
 * Logger 只负责统一格式、按日期保存和线程安全写入，不参与任何业务逻辑。
 */
class Logger
{
public:
  // 安装日志处理器。重复调用是安全的。
  static bool install();

  // 恢复安装前的 Qt 日志处理器并关闭文件。
  static void shutdown();

  // 返回日志目录；安装失败时可能为空。
  static QString logDirectory();

private:
  static void messageHandler(QtMsgType type, const QMessageLogContext &context,
                             const QString &message);
};

#endif // LOGGER_H
