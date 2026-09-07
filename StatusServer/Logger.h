#pragma once

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

// 日志级别从低到高排列。
// minimum_level_ 设为 Info 后，Debug 会被过滤，Info/Warning/Error 会保留。
enum class LogLevel
{
	Debug = 0,
	Info,
	Warning,
	Error
};

// StatusServer 的线程安全日志类。
// 业务代码不需要自己创建 Logger，直接使用文件末尾的 LOG_INFO(...) 等宏即可。
class Logger final
{
public:
	// 返回全项目唯一的 Logger。
	// C++11 保证函数内部 static 对象只初始化一次，多个线程同时首次调用也安全。
	static Logger& Instance();

	// Logger 持有文件流和互斥锁，不能被复制或移动。
	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;
	Logger(Logger&&) = delete;
	Logger& operator=(Logger&&) = delete;

	// Args... 表示可以传入任意数量、任意可输出类型的参数。
	// 例如：LOG_INFO("port=", 50052, ", started=", true)。
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

	// 低于 minimum_level 的日志不会输出。默认记录 Debug 及以上所有级别。
	void SetMinimumLevel(LogLevel minimum_level);

private:
	Logger();
	~Logger();

	template <typename... Args>
	static std::string BuildMessage(Args&&... args)
	{
		std::ostringstream stream;
		// 折叠表达式会在编译期展开成 stream << 参数1 << 参数2 ...。
		// std::forward 保留参数原来的类型，避免没有必要的中间复制。
		(stream << ... << std::forward<Args>(args));
		return stream.str();
	}

	void Write(LogLevel level, const std::string& message);
	static const char* LevelName(LogLevel level);

	// gRPC 会使用多个工作线程。mutex_ 保证一条日志不会和另一条日志写乱。
	std::mutex mutex_;
	std::ofstream log_file_;
	LogLevel minimum_level_ = LogLevel::Debug;
};

// __VA_ARGS__ 代表调用宏时传入的全部参数。
// 宏末尾不写分号，调用方按普通函数的样子写 LOG_INFO(...);。
#define LOG_DEBUG(...) (::Logger::Instance().Debug(__VA_ARGS__))
#define LOG_INFO(...) (::Logger::Instance().Info(__VA_ARGS__))
#define LOG_WARNING(...) (::Logger::Instance().Warning(__VA_ARGS__))
#define LOG_ERROR(...) (::Logger::Instance().Error(__VA_ARGS__))
