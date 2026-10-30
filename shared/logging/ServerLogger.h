#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace ServerLogging {
enum class Level { Debug = 0, Info, Warning, Error };
enum class Output { File, Console, Both };

struct Options {
  std::filesystem::path directory = "logs";
  std::string instance;
  Level minimum = Level::Info;
  Output output = Output::File;
  std::size_t max_bytes = 20 * 1024 * 1024;
  std::size_t backups = 4;
  std::size_t queue_size = 2048;
  static Options FromEnvironment();
};

// 服务端公共后端不依赖 Qt；各服务仅保留自己的单例入口
class Logger {
public:
  explicit Logger(std::string service, Options options = Options::FromEnvironment());
  ~Logger();
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;
  Logger(Logger &&) = delete;
  Logger &operator=(Logger &&) = delete;

  template <typename... Args> void Debug(Args &&...args)
  {
    Log(Level::Debug, std::forward<Args>(args)...);
  }
  template <typename... Args> void Info(Args &&...args)
  {
    Log(Level::Info, std::forward<Args>(args)...);
  }
  template <typename... Args> void Warning(Args &&...args)
  {
    Log(Level::Warning, std::forward<Args>(args)...);
  }
  template <typename... Args> void Error(Args &&...args)
  {
    Log(Level::Error, std::forward<Args>(args)...);
  }

  void SetMinimumLevel(Level level) { minimum_.store(level, std::memory_order_relaxed); }
  const std::filesystem::path &FilePath() const { return file_path_; }

private:
  template <typename... Args> void Log(Level level, Args &&...args)
  {
    if (level < minimum_.load(std::memory_order_relaxed))
      return;
    std::ostringstream text;
    (text << ... << std::forward<Args>(args));
    Write(level, text.str());
  }
  void Write(Level level, const std::string &text);
  struct Backend;
  std::atomic<Level> minimum_;
  std::size_t message_limit_;
  std::filesystem::path file_path_;
  std::unique_ptr<Backend> backend_;
};
} // namespace ServerLogging
