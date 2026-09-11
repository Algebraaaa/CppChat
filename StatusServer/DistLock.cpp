#include "DistLock.h"

#include <chrono>
#include <memory>
#include <thread>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "Logger.h"

namespace
{
	struct ReplyDeleter
	{
		void operator()(redisReply* reply) const
		{
			if (reply != nullptr)
			{
				freeReplyObject(reply);
			}
		}
	};

	using ReplyPtr = std::unique_ptr<redisReply, ReplyDeleter>;
}

DistLock& DistLock::GetInstance()
{
	static DistLock lock;
	return lock;
}

DistLock::~DistLock()
{
	LOG_DEBUG("DistLock singleton destroyed.");
}

std::string DistLock::Acquire(redisContext* context, const std::string& lock_name,
	int lock_timeout_seconds, int acquire_timeout_seconds)
{
	if (context == nullptr || lock_name.empty() ||
		lock_timeout_seconds <= 0 || acquire_timeout_seconds <= 0)
	{
		return {};
	}

	const std::string lock_key = "lock:" + lock_name;
	const std::string identifier = boost::uuids::to_string(
		boost::uuids::random_generator()());
	const std::string timeout_text = std::to_string(lock_timeout_seconds);
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(acquire_timeout_seconds);

	while (std::chrono::steady_clock::now() < deadline)
	{
		// SET key value NX EX seconds：键不存在时才写入，并设置过期时间。
		const char* arguments[] = {
			"SET", lock_key.data(), identifier.data(), "NX", "EX", timeout_text.data()
		};
		const std::size_t argument_lengths[] = {
			3, lock_key.size(), identifier.size(), 2, 2, timeout_text.size()
		};
		ReplyPtr reply(static_cast<redisReply*>(redisCommandArgv(
			context, 6, arguments, argument_lengths)));

		if (reply && reply->type == REDIS_REPLY_STATUS && reply->str != nullptr &&
			std::string(reply->str, reply->len) == "OK")
		{
			return identifier;
		}
		if (context->err != 0)
		{
			return {};
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	return {};
}

bool DistLock::Release(redisContext* context, const std::string& lock_name,
	const std::string& identifier)
{
	if (context == nullptr || lock_name.empty() || identifier.empty())
	{
		return false;
	}

	const std::string lock_key = "lock:" + lock_name;
	// Lua 脚本让“核对标识”和“删除锁”成为一次原子操作。
	const std::string script =
		"if redis.call('get', KEYS[1]) == ARGV[1] then "
		"return redis.call('del', KEYS[1]) else return 0 end";
	const char* arguments[] = {
		"EVAL", script.data(), "1", lock_key.data(), identifier.data()
	};
	const std::size_t argument_lengths[] = {
		4, script.size(), 1, lock_key.size(), identifier.size()
	};
	ReplyPtr reply(static_cast<redisReply*>(redisCommandArgv(
		context, 5, arguments, argument_lengths)));

	return reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
}
