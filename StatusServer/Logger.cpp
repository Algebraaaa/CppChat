#include "Logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <thread>

namespace
{
	// 匿名 namespace 中的名称只在 Logger.cpp 内可见。
	constexpr const char* kServiceName = "StatusServer";

	// localtime_s 是 Windows 上的线程安全本地时间转换函数。
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
		// 日志文件按日期命名，例如 StatusServer_2026-08-30.log。
		const std::time_t now = std::time(nullptr);
		const std::tm local_time = ToLocalTime(now);
		std::ostringstream stream;
		stream << std::put_time(&local_time, "%Y-%m-%d");
		return stream.str();
	}
}

Logger& Logger::GetInstance()
{
	// 第一次调用时创建，以后始终返回同一个对象。
	static Logger logger;
	return logger;
}

Logger::Logger()
{
	try
	{
		// 日志目录跟随当前工作目录，与 ConfigMgr 查找 config.ini 的规则一致。
		const std::filesystem::path log_directory =
			std::filesystem::current_path() / "logs";
		std::filesystem::create_directories(log_directory);

		const std::filesystem::path log_path =
			log_directory / ("StatusServer_" + CurrentDate() + ".log");
		log_file_.open(log_path, std::ios::out | std::ios::app);

		if (!log_file_.is_open())
		{
			std::cerr << "[LOGGER] Failed to open log file: "
				<< log_path.string() << std::endl;
		}
	}
	catch (const std::exception& exception)
	{
		// 日志初始化失败时不能再次调用 Logger，否则会递归初始化。
		// 保留控制台输出，服务器仍可继续运行。
		std::cerr << "[LOGGER] Failed to initialize file logging: "
			<< exception.what() << std::endl;
	}
}

Logger::~Logger()
{
	// RAII：程序退出、Logger 析构时自动刷新并关闭文件。
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

	const std::chrono::system_clock::time_point now =
		std::chrono::system_clock::now();
	const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
	const std::tm local_time = ToLocalTime(now_time);
	const std::chrono::milliseconds milliseconds =
		std::chrono::duration_cast<std::chrono::milliseconds>(
		now.time_since_epoch()) % 1000;

	std::ostringstream line;
	line << '[' << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S")
		<< '.' << std::setfill('0') << std::setw(3) << milliseconds.count() << ']'
		<< " [" << kServiceName << ']'
		<< " [" << LevelName(level) << ']'
		<< " [thread " << std::this_thread::get_id() << "] "
		<< message;

	// Error 写到标准错误流，其他级别写到标准输出流。
	// 展开成 if/else，比三目运算符更便于初学者跟踪。
	if (level == LogLevel::Error)
	{
		std::cerr << line.str() << std::endl;
	}
	else
	{
		std::cout << line.str() << std::endl;
	}

	if (log_file_.is_open())
	{
		log_file_ << line.str() << '\n';
		// 每条日志立即刷新，程序异常退出时尽量保留最后的故障信息。
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
