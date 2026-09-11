#include "Logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <thread>

namespace
{
	constexpr const char* kServiceName = "ChatServer";

	std::tm ToLocalTime(std::time_t time_value)
	{
		std::tm local_time{};
#ifdef _WIN32
		localtime_s(&local_time, &time_value);
#else
		localtime_r(&time_value, &local_time);
#endif
		return local_time;
	}

	std::string CurrentDate()
	{
		const std::time_t now = std::time(nullptr);
		const std::tm local_time = ToLocalTime(now);
		std::ostringstream stream;
		stream << std::put_time(&local_time, "%Y-%m-%d");
		return stream.str();
	}
}

Logger& Logger::GetInstance()
{
	static Logger logger;
	return logger;
}

Logger::Logger()
{
	try
	{
		const std::filesystem::path log_directory =
			std::filesystem::current_path() / "logs";
		std::filesystem::create_directories(log_directory);

		const std::filesystem::path log_path =
			log_directory / ("ChatServer_" + CurrentDate() + ".log");
		log_file_.open(log_path, std::ios::out | std::ios::app);

		if (!log_file_.is_open())
		{
			std::cerr << "[LOGGER] Failed to open log file: "
				<< log_path.string() << std::endl;
		}
	}
	catch (const std::exception& exception)
	{
		std::cerr << "[LOGGER] Failed to initialize file logging: "
			<< exception.what() << std::endl;
	}
}

Logger::~Logger()
{
	// Logger 正在析构，不能通过 LOG_DEBUG 再次获取自身实例。
	Write(LogLevel::Debug, "Logger Meyers singleton destroyed.");

	std::lock_guard<std::mutex> lock(mutex_);
	if (log_file_.is_open())
	{
		log_file_.flush();
		log_file_.close();
	}
}

void Logger::SetMinimumLevel(LogLevel minimum_level)
{
	std::lock_guard<std::mutex> lock(mutex_);
	minimum_level_ = minimum_level;
}

void Logger::Write(LogLevel level, const std::string& message)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (level < minimum_level_)
	{
		return;
	}

	const auto now = std::chrono::system_clock::now();
	const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
	const std::tm local_time = ToLocalTime(now_time);
	const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()) % 1000;

	std::ostringstream line;
	line << '[' << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S")
		<< '.' << std::setfill('0') << std::setw(3) << milliseconds.count() << ']'
		<< " [" << kServiceName << ']'
		<< " [" << LevelName(level) << ']'
		<< " [thread " << std::this_thread::get_id() << "] "
		<< message;

	std::ostream& console = level == LogLevel::Error ? std::cerr : std::cout;
	console << line.str() << std::endl;

	if (log_file_.is_open())
	{
		log_file_ << line.str() << '\n';
		log_file_.flush();
	}
}

const char* Logger::LevelName(LogLevel level)
{
	switch (level)
	{
	case LogLevel::Debug:
		return "DEBUG";
	case LogLevel::Info:
		return "INFO";
	case LogLevel::Warning:
		return "WARNING";
	case LogLevel::Error:
		return "ERROR";
	default:
		return "UNKNOWN";
	}
}
