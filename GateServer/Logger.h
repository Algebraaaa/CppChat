#pragma once

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

// 日志级别从低到高排列，便于使用大小比较过滤日志。
enum class LogLevel
{
	Debug = 0,
	Info,
	Warning,
	Error
};

// GateServer 的线程安全单例日志类。
// 业务代码通过文件末尾的 LOG_DEBUG / LOG_INFO / LOG_WARNING / LOG_ERROR 宏调用。
class Logger final
{
public:
	// C++11 保证函数内部 static 对象只初始化一次，所以这里天然是线程安全单例。
	static Logger& GetInstance();

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

	// 低于 minimum_level 的日志不会输出。默认记录 Debug 及以上所有级别。
	void SetMinimumLevel(LogLevel minimum_level);

private:
	Logger();
	~Logger();

	template <typename... Args>
	static std::string BuildMessage(Args&&... args)
	{
		std::ostringstream stream;
		// 折叠表达式把所有参数依次写入同一个字符串流。
		(stream << ... << std::forward<Args>(args));
		return stream.str();
	}

	void Write(LogLevel level, const std::string& message);
	static const char* LevelName(LogLevel level);

	// 保护控制台、日志文件和 minimum_level_，避免工作线程交叉写入。
	std::mutex mutex_;
	std::ofstream log_file_;
	LogLevel minimum_level_ = LogLevel::Debug;
};

// 业务层只使用这四个宏，Logger::GetInstance() 的单例获取细节隐藏在这里。
// 外层括号让宏可以安全地用于 if 等表达式环境；宏定义末尾不写分号。
#define LOG_DEBUG(...) (::Logger::GetInstance().Debug(__VA_ARGS__))
#define LOG_INFO(...) (::Logger::GetInstance().Info(__VA_ARGS__))
#define LOG_WARNING(...) (::Logger::GetInstance().Warning(__VA_ARGS__))
#define LOG_ERROR(...) (::Logger::GetInstance().Error(__VA_ARGS__))
