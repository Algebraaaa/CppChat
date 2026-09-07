#include "common/logger.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <cstdio>

namespace {
QMutex loggerMutex;
QFile logFile;
QString logsPath;
QDate openedDate;
QtMessageHandler previousHandler = nullptr;
bool loggerInstalled = false;

QString findLogBasePath()
{
  const QString executablePath = QCoreApplication::applicationDirPath();
  QDir directory(executablePath);

  // 开发环境中从 build/.../debug 逐级向上寻找项目文件。
  do {
    if (QFileInfo(directory.filePath(QStringLiteral("CppChat.pro"))).isFile()) {
      return directory.absolutePath();
    }
  } while (directory.cdUp());

  // 发布后的目录中通常没有 .pro 文件，此时日志放在 EXE 同级目录。
  return executablePath;
}

QString levelName(QtMsgType type)
{
  switch (type) {
  case QtDebugMsg:
    return QStringLiteral("DEBUG");
  case QtInfoMsg:
    return QStringLiteral("INFO ");
  case QtWarningMsg:
    return QStringLiteral("WARN ");
  case QtCriticalMsg:
    return QStringLiteral("ERROR");
  case QtFatalMsg:
    return QStringLiteral("FATAL");
  }
  return QStringLiteral("UNKWN");
}

bool openLogFile(const QDate &date)
{
  if (logFile.isOpen() && openedDate == date) {
    return true;
  }

  logFile.close();
  openedDate = {};

  const QString fileName = QStringLiteral("cppchat-%1.log").arg(date.toString(Qt::ISODate));
  logFile.setFileName(QDir(logsPath).filePath(fileName));
  if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    return false;
  }

  openedDate = date;
  return true;
}

QString formatLine(QtMsgType type, const QMessageLogContext &context,
                   const QString &message)
{
  const QString time = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
  const quintptr threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
  const QString category
    = context.category && *context.category ? QString::fromUtf8(context.category)
                                           : QStringLiteral("default");

  QString source;
  if (context.file && *context.file) {
    source = QStringLiteral(" (%1:%2)")
                 .arg(QFileInfo(QString::fromUtf8(context.file)).fileName())
                 .arg(context.line);
  }

  return QStringLiteral("%1 [%2] [%3] [thread:0x%4] %5%6\n")
    .arg(time, levelName(type), category, QString::number(threadId, 16), message, source);
}
} // namespace

bool Logger::install()
{
  QMutexLocker locker(&loggerMutex);
  if (loggerInstalled) {
    return true;
  }

  logsPath = QDir(findLogBasePath()).filePath(QStringLiteral("logs"));
  if (!QDir().mkpath(logsPath) || !openLogFile(QDate::currentDate())) {
    logsPath.clear();
    return false;
  }

  loggerInstalled = true;
  previousHandler = qInstallMessageHandler(&Logger::messageHandler);
  qAddPostRoutine(&Logger::shutdown);
  return true;
}

void Logger::shutdown()
{
  QMutexLocker locker(&loggerMutex);
  if (!loggerInstalled) {
    return;
  }

  qInstallMessageHandler(previousHandler);
  previousHandler = nullptr;
  loggerInstalled = false;
  logFile.flush();
  logFile.close();
  openedDate = {};
}

QString Logger::logDirectory()
{
  QMutexLocker locker(&loggerMutex);
  return logsPath;
}

void Logger::messageHandler(QtMsgType type, const QMessageLogContext &context,
                            const QString &message)
{
  const QString line = formatLine(type, context, message);
  QtMessageHandler consoleHandler = nullptr;

  {
    QMutexLocker locker(&loggerMutex);
    if (openLogFile(QDate::currentDate())) {
      logFile.write(line.toUtf8());
      logFile.flush();
    }
    consoleHandler = previousHandler;
  }

  // 安装自定义处理器后 Qt 默认控制台输出会被替换，这里显式保留它。
  if (consoleHandler) {
    consoleHandler(type, context, message);
  } else {
    const QByteArray consoleLine = line.toLocal8Bit();
    std::fwrite(consoleLine.constData(), 1, static_cast<size_t>(consoleLine.size()), stderr);
    std::fflush(stderr);
  }
}
