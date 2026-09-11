#include "utils.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

std::string GetCurrentTimestamp()
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t value = std::chrono::system_clock::to_time_t(now);
	std::tm local_time{};
#ifdef _WIN32
	localtime_s(&local_time, &value);
#else
	localtime_r(&value, &local_time);
#endif

	std::ostringstream stream;
	stream << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
	return stream.str();
}
