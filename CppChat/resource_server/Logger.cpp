#include "Logger.h"

#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {
std::tm ToLocalTime(std::time_t value)
{
  std::tm result{};
#ifdef _WIN32
  localtime_s(&result, &value);
#else
  localtime_r(&value, &result);
#endif
  return result;
}

std::string CurrentDate()
{
  const auto now = ToLocalTime(std::time(nullptr));
  std::ostringstream text;
  text << std::put_time(&now, "%Y-%m-%d");
  return text.str();
}
} // namespace

Logger &Logger::GetInstance()
{
  static Logger logger;
  return logger;
}

Logger::Logger()
{
  try {
    const auto directory = std::filesystem::current_path() / "logs";
    std::filesystem::create_directories(directory);
    log_file_.open(directory / ("ResourceServer_" + CurrentDate() + ".log"),
                   std::ios::out | std::ios::app);
    if (!log_file_)
      std::cerr << "[LOGGER] ResourceServer log file cannot be opened\n";
  } catch (const std::exception &error) {
    std::cerr << "[LOGGER] ResourceServer file logging failed: " << error.what() << '\n';
  }
}

Logger::~Logger()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (log_file_) {
    log_file_.flush();
    log_file_.close();
  }
}

void Logger::SetMinimumLevel(LogLevel level)
{
  std::lock_guard<std::mutex> lock(mutex_);
  minimum_level_ = level;
}

void Logger::Write(LogLevel level, const std::string &message)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (level < minimum_level_)
    return;

  const auto now = std::chrono::system_clock::now();
  const auto local = ToLocalTime(std::chrono::system_clock::to_time_t(now));
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  std::ostringstream line;
  line << '[' << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0')
       << std::setw(3) << milliseconds.count() << ']' << " [ResourceServer] [" << LevelName(level)
       << "] [thread " << std::this_thread::get_id() << "] " << message;

  auto &console = level == LogLevel::Error ? std::cerr : std::cout;
  console << line.str() << std::endl;
  if (log_file_) {
    log_file_ << line.str() << '\n';
    log_file_.flush();
  }
}

const char *Logger::LevelName(LogLevel level)
{
  switch (level) {
  case LogLevel::Debug:
    return "DEBUG";
  case LogLevel::Info:
    return "INFO";
  case LogLevel::Warning:
    return "WARNING";
  case LogLevel::Error:
    return "ERROR";
  }
  return "UNKNOWN";
}
