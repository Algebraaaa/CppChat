#include "ServerLogger.h"
#include "InstanceLease.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define SPDLOG_WCHAR_FILENAMES
#endif
#include <spdlog/async.h>
#include <spdlog/details/periodic_worker.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

namespace ServerLogging {
namespace {
void Diagnostic(const std::string &message)
{
  // 不再进入日志接口，避免后端故障递归
  std::fprintf(stderr, "[LOGGER] %s\n", message.c_str());
  std::fflush(stderr);
}

std::string Environment(const char *name)
{
  const auto *value = std::getenv(name);
  return value ? value : "";
}

std::size_t Number(const char *name, std::size_t fallback, std::size_t low, std::size_t high)
{
  const auto text = Environment(name);
  if (text.empty())
    return fallback;
  std::size_t number = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || number < low ||
      number > high) {
    Diagnostic(std::string("Invalid ") + name + "; using default");
    return fallback;
  }
  return number;
}

bool ValidName(const std::string &name)
{
  return !name.empty() && name.size() <= 64 &&
         std::all_of(name.begin(), name.end(), [](unsigned char ch) {
           return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
         });
}

spdlog::level::level_enum ToLevel(Level level)
{
  switch (level) {
  case Level::Debug:
    return spdlog::level::debug;
  case Level::Info:
    return spdlog::level::info;
  case Level::Warning:
    return spdlog::level::warn;
  case Level::Error:
    return spdlog::level::err;
  }
  return spdlog::level::info;
}
} // namespace

Options Options::FromEnvironment()
{
  Options options;
  const auto directory = Environment("CPPCHAT_LOG_DIRECTORY");
  if (!directory.empty())
    options.directory = std::filesystem::u8path(directory);
  options.instance = Environment("CPPCHAT_LOG_INSTANCE");
  if (!options.instance.empty() && !ValidName(options.instance)) {
    Diagnostic("Invalid CPPCHAT_LOG_INSTANCE; using service namespace");
    options.instance.clear();
  }
  const auto level = Environment("CPPCHAT_LOG_LEVEL");
  if (level == "debug")
    options.minimum = Level::Debug;
  else if (level == "warning" || level == "warn")
    options.minimum = Level::Warning;
  else if (level == "error")
    options.minimum = Level::Error;
  else if (!level.empty() && level != "info")
    Diagnostic("Invalid CPPCHAT_LOG_LEVEL; using info");
  const auto output = Environment("CPPCHAT_LOG_OUTPUT");
  if (output == "console")
    options.output = Output::Console;
  else if (output == "both")
    options.output = Output::Both;
  else if (!output.empty() && output != "file")
    Diagnostic("Invalid CPPCHAT_LOG_OUTPUT; using file");
  options.max_bytes = Number("CPPCHAT_LOG_MAX_MB", 20, 1, 1024) * 1024 * 1024;
  options.backups = Number("CPPCHAT_LOG_BACKUPS", 4, 1, 32);
  return options;
}

struct Logger::Backend {
  // 析构顺序：先停止刷新，再排空队列和关闭文件，最后释放槽位租约
  std::unique_ptr<InstanceLease> lease;
  std::shared_ptr<spdlog::details::thread_pool> pool;
  std::shared_ptr<spdlog::async_logger> routine;
  std::shared_ptr<spdlog::logger> important;
  std::unique_ptr<spdlog::details::periodic_worker> flusher;
  std::size_t reported_overruns = 0;

  void ReportOverruns()
  {
    const auto count = pool->overrun_counter();
    if (count != reported_overruns) {
      important->warn("Async log queue full: {} routine messages dropped (total {}).",
                      count - reported_overruns, count);
      reported_overruns = count;
    }
  }

  ~Backend()
  {
    flusher.reset();
    if (pool && important)
      ReportOverruns();
    routine.reset();
    pool.reset();
    if (important)
      important->flush();
    important.reset();
  }
};

Logger::Logger(std::string service, Options options)
    : minimum_(options.minimum), message_limit_(std::min<std::size_t>(16384, options.max_bytes / 2))
{
  if (!ValidName(service) || (!options.instance.empty() && !ValidName(options.instance)) ||
      options.max_bytes < 512 || options.backups > 32 || options.queue_size == 0)
    throw std::invalid_argument("Invalid server logger options");
  auto backend = std::make_unique<Backend>();
  std::vector<spdlog::sink_ptr> sinks;
  if (options.output != Output::Console) {
    try {
      const auto space = options.instance.empty() ? service : service + "-" + options.instance;
      backend->lease = std::make_unique<InstanceLease>(options.directory / space);
      const auto path = std::filesystem::absolute(backend->lease->Directory() / "events.log");
#ifdef _WIN32
      const auto filename = path.wstring();
#else
      const auto filename = path.string();
#endif
      sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          filename, options.max_bytes, options.backups));
      file_path_ = path;
      Diagnostic("file: " + path.u8string());
    } catch (const std::exception &) {
      Diagnostic("File logging unavailable; using console only");
      backend->lease.reset();
    }
  }
  if (options.output != Output::File || sinks.empty())
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
  backend->pool = std::make_shared<spdlog::details::thread_pool>(options.queue_size, 1);
  backend->routine = std::make_shared<spdlog::async_logger>(
      service + "-routine", sinks.begin(), sinks.end(), backend->pool,
      spdlog::async_overflow_policy::overrun_oldest);
  backend->important =
      std::make_shared<spdlog::logger>(service + "-important", sinks.begin(), sinks.end());
  for (const auto &logger :
       {std::static_pointer_cast<spdlog::logger>(backend->routine), backend->important}) {
    logger->set_level(spdlog::level::debug);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [" + service + "] [%l] [pid %P] [thread %t] %v");
    logger->set_error_handler(Diagnostic);
  }
  backend->important->flush_on(spdlog::level::warn);
  auto *state = backend.get();
  backend->flusher = std::make_unique<spdlog::details::periodic_worker>(
      [state] {
        state->ReportOverruns();
        state->important->flush();
      },
      std::chrono::seconds(1));
  backend_ = std::move(backend);
}

Logger::~Logger() = default;

void Logger::Write(Level level, const std::string &text)
{
  // 固定格式字符串，业务文本中的大括号不会被解释为格式参数
  const auto limit =
      level >= Level::Warning ? message_limit_ : std::min<std::size_t>(4096, message_limit_);
  std::string bounded;
  if (text.size() > limit) {
    auto length = limit;
    // 避免在 UTF-8 续字节中间截断
    while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80)
      --length;
    bounded = text.substr(0, length) + " ...[truncated]";
  }
  const auto &record = bounded.empty() ? text : bounded;
  const spdlog::string_view_t message(record.data(), record.size());
  if (level >= Level::Warning)
    backend_->important->log(ToLevel(level), message);
  else
    backend_->routine->log(ToLevel(level), message);
}
} // namespace ServerLogging
