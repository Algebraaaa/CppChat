#include "Logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <thread>

namespace
{
	constexpr const char* kServiceName = "GateServer";

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
		// 日志目录跟随程序当前工作目录，与本项目查找 config.ini 的规则一致。
		const std::filesystem::path log_directory =
			std::filesystem::current_path() / "logs";
		std::filesystem::create_directories(log_directory);

		const std::filesystem::path log_path =
			log_directory / ("GateServer_" + CurrentDate() + ".log");
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

	// Logger 自己就是迈耶斯单例，不能通过 LOG_INFO 调用自己，否则会递归进入 GetInstance()。
	// 这里直接调用成员 Write；即使文件打开失败，控制台日志仍然可以使用。
	Write(
		LogLevel::Info,
		log_file_.is_open()
			? "Logger singleton initialized successfully: file output enabled."
			: "Logger singleton initialized: console output only.");
}

Logger::~Logger()
{
	// 在关闭日志文件之前写最后一条生命周期日志。
	Write(LogLevel::Info, "Logger Meyers singleton destroyed successfully.");

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

	// Error 写到标准错误流，其他级别写到标准输出流。
	std::ostream& console =
		level == LogLevel::Error ? std::cerr : std::cout;
	console << line.str() << std::endl;

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
