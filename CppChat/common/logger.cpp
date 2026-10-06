#include "common/logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <cstdio>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

// 本模块独占 spdlog 配置，使用官方头文件版，避免依赖某个编译器的二进制库。
#ifdef _WIN32
#define SPDLOG_WCHAR_FILENAMES
#endif
#include <spdlog/async.h>
#include <spdlog/details/periodic_worker.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

namespace {
QMutex loggerMutex;
QString logsPath;
QtMessageHandler previousHandler = nullptr;
bool loggerInstalled = false;
constexpr size_t kLogQueueCapacity = 2048;
constexpr int kMaxRoutineMessageCharacters = 4096;

void reportLoggingError(const std::string &message)
{
  // 日志后端出错不能再调用 qWarning，否则会递归进入 Qt 日志处理器。
  std::fprintf(stderr, "CppChat logging error: %s\n", message.c_str());
  std::fflush(stderr);
}

class LogBackend
{
public:
  explicit LogBackend(const QString &directory)
  {
    const QString filePattern = QDir(directory).filePath("cppchat-%Y-%m-%d.log");
#ifdef _WIN32
    const auto filename = filePattern.toStdWString();
#else
    const auto filename = filePattern.toStdString();
#endif
    auto file = std::make_shared<spdlog::sinks::daily_file_format_sink_mt>(filename, 0, 0);
    auto console = std::make_shared<spdlog::sinks::stderr_sink_mt>();
    const std::vector<spdlog::sink_ptr> sinks{file, console};
    pool = std::make_shared<spdlog::details::thread_pool>(kLogQueueCapacity, 1);
    routine =
        std::make_shared<spdlog::async_logger>("cppchat-routine", sinks.begin(), sinks.end(), pool,
                                               spdlog::async_overflow_policy::overrun_oldest);
    important = std::make_shared<spdlog::logger>("cppchat-important", sinks.begin(), sinks.end());
    for (const auto &logger : {std::static_pointer_cast<spdlog::logger>(routine), important}) {
      logger->set_level(spdlog::level::trace);
      logger->set_pattern("%v");
      logger->set_error_handler(reportLoggingError);
    }
    important->flush_on(spdlog::level::warn);
  }

  void StartFlushing()
  {
    flusher = std::make_unique<spdlog::details::periodic_worker>(
        [this] {
          ReportOverruns();
          important->flush();
        },
        std::chrono::seconds(1));
  }

  void ReportOverruns()
  {
    const auto count = pool->overrun_counter();
    if (count != reportedOverruns) {
      important->warn("Async log queue full: {} older routine messages dropped (total {}).",
                      count - reportedOverruns, count);
      reportedOverruns = count;
    }
  }

  ~LogBackend()
  {
    // 停止周期工作后排空已有日志，再刷盘。重要日志从不进入会被覆盖的队列。
    flusher.reset();
    ReportOverruns();
    routine.reset();
    pool.reset();
    important->flush();
  }

  std::shared_ptr<spdlog::details::thread_pool> pool;
  std::shared_ptr<spdlog::async_logger> routine;
  std::shared_ptr<spdlog::logger> important;
  std::unique_ptr<spdlog::details::periodic_worker> flusher;
  size_t reportedOverruns = 0;
};

std::shared_ptr<LogBackend> backend;

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

QString formatLine(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
  const QString time =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
  const quintptr threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
  const QString category = context.category && *context.category
                               ? QString::fromUtf8(context.category)
                               : QStringLiteral("default");

  QString source;
  if (context.file && *context.file) {
    source = QStringLiteral(" (%1:%2)")
                 .arg(QFileInfo(QString::fromUtf8(context.file)).fileName())
                 .arg(context.line);
  }

  return QStringLiteral("%1 [%2] [%3] [thread:0x%4] %5%6")
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
  if (!QDir().mkpath(logsPath)) {
    logsPath.clear();
    return false;
  }

  try {
    backend = std::make_shared<LogBackend>(logsPath);
  } catch (const std::exception &error) {
    backend.reset();
    logsPath.clear();
    reportLoggingError(error.what());
    return false;
  }
  previousHandler = qInstallMessageHandler(&Logger::messageHandler);
  if (previousHandler) {
    // 已有自定义控制台处理器时，避免再输出一份控制台日志。
    backend->routine->sinks().pop_back();
    backend->important->sinks().pop_back();
  }
  try {
    backend->StartFlushing();
  } catch (const std::exception &error) {
    qInstallMessageHandler(previousHandler);
    previousHandler = nullptr;
    backend.reset();
    logsPath.clear();
    reportLoggingError(error.what());
    return false;
  }
  loggerInstalled = true;
  qAddPostRoutine(&Logger::shutdown);
  return true;
}

void Logger::shutdown()
{
  std::shared_ptr<LogBackend> closing;
  {
    QMutexLocker locker(&loggerMutex);
    if (!loggerInstalled)
      return;
    qInstallMessageHandler(previousHandler);
    previousHandler = nullptr;
    loggerInstalled = false;
    closing = std::move(backend);
  }
  // 不持有入口互斥锁等待日志线程；正在执行的处理器仍持有自己的后端快照。
  closing.reset();
}

QString Logger::logDirectory()
{
  QMutexLocker locker(&loggerMutex);
  return logsPath;
}

void Logger::messageHandler(QtMsgType type, const QMessageLogContext &context,
                            const QString &message)
{
  const bool important = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
  const QString bounded = !important && message.size() > kMaxRoutineMessageCharacters
                              ? message.left(kMaxRoutineMessageCharacters) + " ...[truncated]"
                              : message;
  const QString line = formatLine(type, context, bounded);
  QtMessageHandler consoleHandler = nullptr;
  std::shared_ptr<LogBackend> target;
  {
    QMutexLocker locker(&loggerMutex);
    target = backend;
    consoleHandler = previousHandler;
  }

  if (target) {
    const QByteArray bytes = line.toUtf8();
    const auto level = type == QtDebugMsg     ? spdlog::level::debug
                       : type == QtInfoMsg    ? spdlog::level::info
                       : type == QtWarningMsg ? spdlog::level::warn
                                              : spdlog::level::critical;
    const spdlog::string_view_t text(bytes.constData(), static_cast<size_t>(bytes.size()));
    if (important)
      target->important->log(level, text);
    else
      target->routine->log(level, text);
  }

  if (consoleHandler) {
    consoleHandler(type, context, message);
  } else if (!target) {
    const QByteArray consoleLine = (line + '\n').toLocal8Bit();
    std::fwrite(consoleLine.constData(), 1, static_cast<size_t>(consoleLine.size()), stderr);
    std::fflush(stderr);
  }
}
