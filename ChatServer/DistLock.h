#pragma once

#include <hiredis/hiredis.h>

#include <string>

class DistLock
{
public:
	static DistLock& GetInstance();
	~DistLock();

	std::string Acquire(
		redisContext* context,
		const std::string& lock_name,
		int lock_timeout_seconds,
		int acquire_timeout_seconds);
	bool Release(
		redisContext* context,
		const std::string& lock_name,
		const std::string& identifier);

	DistLock(const DistLock&) = delete;
	DistLock& operator=(const DistLock&) = delete;

private:
	DistLock() = default;
};
