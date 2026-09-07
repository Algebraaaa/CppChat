#pragma once

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

enum class LogLevel
{
	Debug = 0,
	Info,
	Warning,
	Error
};

// ChatServer 自己的线程安全日志器。日志同时输出到控制台和 logs 目录。
class Logger final
{
public:
	static Logger& Instance();

	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;
	Logger(Logger&&) = delete;
	Logger& operator=(Logger&&) = delete;

	template <typename... Args>
	void Debug(Args&&... args)
	{
		Write(LogLevel::Debug, BuildMessage(std::forward<Args>(args)...));
	}

	template <typename... Args>
	void Info(Args&&... args)
	{
		Write(LogLevel::Info, BuildMessage(std::forward<Args>(args)...));
	}

	template <typename... Args>
	void Warning(Args&&... args)
	{
		Write(LogLevel::Warning, BuildMessage(std::forward<Args>(args)...));
	}

	template <typename... Args>
	void Error(Args&&... args)
	{
		Write(LogLevel::Error, BuildMessage(std::forward<Args>(args)...));
	}

	void SetMinimumLevel(LogLevel minimum_level);

private:
	Logger();
	~Logger();

	template <typename... Args>
	static std::string BuildMessage(Args&&... args)
	{
		std::ostringstream stream;
		(stream << ... << std::forward<Args>(args));
		return stream.str();
	}

	void Write(LogLevel level, const std::string& message);
	static const char* LevelName(LogLevel level);

	std::mutex mutex_;
	std::ofstream log_file_;
	LogLevel minimum_level_ = LogLevel::Debug;
};

#define LOG_DEBUG(...) (::Logger::Instance().Debug(__VA_ARGS__))
#define LOG_INFO(...) (::Logger::Instance().Info(__VA_ARGS__))
#define LOG_WARNING(...) (::Logger::Instance().Warning(__VA_ARGS__))
#define LOG_ERROR(...) (::Logger::Instance().Error(__VA_ARGS__))
