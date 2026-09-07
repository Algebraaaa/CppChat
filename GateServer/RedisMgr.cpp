#include "RedisMgr.h"

#include "ConfigMgr.h"

#include <cstring>
#include <memory>

namespace
{
	/*
	 * ReplyDeleter 是 redisReply 的自定义删除器。
	 * redisReply 来自 hiredis，不能使用普通 delete，必须调用 freeReplyObject。
	 * 它和下面的 unique_ptr 组合后，函数无论正常 return 还是提前 return，reply 都会释放。
	 */
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

	// ReplyPtr 的使用方式和普通 unique_ptr 相同，但析构时执行 ReplyDeleter。
	using ReplyPtr = std::unique_ptr<redisReply, ReplyDeleter>;

	/*
	 * RedisConnectionGuard 是连接的“作用域守卫”，体现 RAII：
	 * 构造时接管从 RedisConPool 借出的连接；析构时自动调用 ReturnConnection。
	 * 这样每个 Redis 命令不需要在所有成功/失败分支手动归还连接，也不容易遗漏。
	 * 守卫不负责 redisFree；连接是否复用、重建或释放统一由连接池决定。
	 */
	class RedisConnectionGuard
	{
	public:
		RedisConnectionGuard(RedisConPool* pool, redisContext* context)
			: _pool(pool), _context(context)
		{
		}

		~RedisConnectionGuard()
		{
			// 即使 _context 是 nullptr 也可以调用，ReturnConnection 会安全地直接返回。
			if (_pool != nullptr)
			{
				_pool->ReturnConnection(_context);
			}
		}

		redisContext* Get() const
		{
			// 这里只借出普通指针用于执行命令，连接所有权仍由守卫和连接池管理。
			return _context;
		}

	private:
		RedisConPool* _pool;
		redisContext* _context;
	};

	// config.ini 中的端口和连接池大小都是字符串。
	// 转换失败、为 0 或为负数时使用安全的默认值 fallback。
	int ParsePositiveInt(const std::string& text, int fallback)
	{
		try
		{
			const int value = std::stoi(text);
			return value > 0 ? value : fallback;
		}
		catch (...)
		{
			return fallback;
		}
	}

	// SET/AUTH 成功时需要收到 STATUS 类型的 "OK" 回复。
	bool IsStatusOk(const redisReply* reply)
	{
		return reply != nullptr && reply->type == REDIS_REPLY_STATUS &&
			reply->str != nullptr && std::string(reply->str, reply->len) == "OK";
	}
}

RedisMgr::RedisMgr()
{
	// 2026-08-12 对比：
	// 旧写法：对每个配置分别写 count() ? at() : 默认值，直接依赖 SectionInfo 内部 map。
	// 新写法：统一使用 GetSection() 和 GetValue(key, default_value)。
	// 好处：配置读取更像自然语言，RedisMgr 不再关心 map 的存储和查找细节。
	const SectionInfo redis_config = ConfigMgr::GetInstance().GetSection("Redis");
	const std::string host = redis_config.GetValue("Host", "127.0.0.1");
	const int port = ParsePositiveInt(redis_config.GetValue("Port", "6379"), 6379);
	const std::string password = redis_config.GetValue("Passwd");
	const int pool_size = ParsePositiveInt(
		redis_config.GetValue("PoolSize", "5"), 5);

	// make_unique 创建由 RedisMgr 独占的连接池。
	// 构造连接池时会立即尝试建立 pool_size 条 Redis 连接并完成认证。
	_connection_pool = std::make_unique<RedisConPool>(
		static_cast<std::size_t>(pool_size), host, port, password);

	if (_connection_pool->IsAvailable())
	{
		LOG_INFO("RedisMgr backend is ready.");
	}
	else
	{
		// RedisMgr 对象本身仍然能构造完成，但后续命令会返回失败，不能误报后端连接成功。
		LOG_WARNING("RedisMgr initialized, but the Redis backend is unavailable.");
	}
}

RedisMgr::~RedisMgr()
{
	// 主动关闭可以立即唤醒等待连接的线程；unique_ptr 随后会自动析构连接池。
	Close();
}

bool RedisMgr::Get(const std::string& key, std::string& value)
{
	// 1. 从连接池借连接，并交给守卫；本函数结束时守卫自动归还连接。
	RedisConnectionGuard connection(_connection_pool.get(), _connection_pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	// 2. %b 表示二进制安全字符串，后面必须依次传数据指针和字节长度。
	//    因此 key 即使不是以 '\0' 结尾也能被 hiredis 正确读取。
	ReplyPtr reply(static_cast<redisReply*>(redisCommand(
		connection.Get(), "GET %b", key.data(), key.size())));
	// 3. GET 命中字符串键时返回 STRING；键不存在时通常返回 NIL，也会在这里失败。
	if (!reply || reply->type != REDIS_REPLY_STRING || reply->str == nullptr)
	{
		return false;
	}

	// 4. 使用 reply->len 构造结果，不能假设 Redis 返回内容以 '\0' 结尾。
	value.assign(reply->str, reply->len);
	return true;
}

bool RedisMgr::Set(const std::string& key, const std::string& value)
{
	// 每个命令函数都遵循同样流程：借连接 -> 执行命令 -> 检查 reply -> 自动归还。
	RedisConnectionGuard connection(_connection_pool.get(), _connection_pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	ReplyPtr reply(static_cast<redisReply*>(redisCommand(
		connection.Get(), "SET %b %b",
		key.data(), key.size(), value.data(), value.size())));
	// SET 成功时 Redis 返回 STATUS "OK"。
	return IsStatusOk(reply.get());
}

bool RedisMgr::LPush(const std::string& key, const std::string& value)
{
	RedisConnectionGuard connection(_connection_pool.get(), _connection_pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	ReplyPtr reply(static_cast<redisReply*>(redisCommand(
		connection.Get(), "LPUSH %b %b",
		key.data(), key.size(), value.data(), value.size())));
	// LPUSH/RPUSH 返回插入后列表的元素数量，因此应当是大于 0 的 INTEGER。
	return reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
}

bool RedisMgr::LPop(const std::string& key, std::string& value)
{
	RedisConnectionGuard connection(_connection_pool.get(), _connection_pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	ReplyPtr reply(static_cast<redisReply*>(redisCommand(
		connection.Get(), "LPOP %b", key.data(), key.size())));
	// 列表为空时 Redis 返回 NIL；只有 STRING 才代表真的弹出了一个元素。
	if (!reply || reply->type != REDIS_REPLY_STRING || reply->str == nullptr)
	{
		return false;
	}

	value.assign(reply->str, reply->len);
	return true;
}

bool RedisMgr::RPush(const std::string& key, const std::string& value)
{
	RedisConnectionGuard connection(_connection_pool.get(), _connection_pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	ReplyPtr reply(static_cast<redisReply*>(redisCommand(
		connection.Get(), "RPUSH %b %b",
		key.data(), key.size(), value.data(), value.size())));
	// 返回值是插入后列表长度。
	return reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
}

bool RedisMgr::RPop(const std::string& key, std::string& value)
{
	RedisConnectionGuard connection(_connection_pool.get(), _connection_pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	ReplyPtr reply(static_cast<redisReply*>(redisCommand(
		connection.Get(), "RPOP %b", key.data(), key.size())));
	// RPOP 与 LPOP 仅弹出方向不同，回复判断规则相同。
	if (!reply || reply->type != REDIS_REPLY_STRING || reply->str == nullptr)
	{
		return false;
	}

	value.assign(reply->str, reply->len);
	return true;
}

bool RedisMgr::HSet(
	const std::string& key,
	const std::string& hkey,
	const std::string& value)
{
	// std::string 重载只负责转换参数，真正发送命令的逻辑集中在下一个重载。
	return HSet(key.c_str(), hkey.c_str(), value.data(), value.size());
}

bool RedisMgr::HSet(
	const char* key,
	const char* hkey,
	const char* hvalue,
	std::size_t hvaluelen)
{
	if (key == nullptr || hkey == nullptr || hvalue == nullptr)
	{
		return false;
	}

	RedisConnectionGuard connection(_connection_pool.get(), _connection_pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	// redisCommandArgv 把命令拆成参数数组，不需要自己拼接带空格的命令字符串。
	const char* arguments[] = { "HSET", key, hkey, hvalue };
	// 每个参数都有明确长度，所以 hvalue 可以安全保存二进制内容和中间的 '\0'。
	const std::size_t argument_lengths[] = {
			4, std::strlen(key), std::strlen(hkey), hvaluelen
	};
	ReplyPtr reply(static_cast<redisReply*>(redisCommandArgv(
		connection.Get(), 4, arguments, argument_lengths)));
	// HSET 返回 INTEGER：1 表示新增字段，0 表示覆盖字段；两种都属于执行成功。
	return reply && reply->type == REDIS_REPLY_INTEGER;
}

std::string RedisMgr::HGet(const std::string& key, const std::string& hkey)
{
	RedisConnectionGuard connection(_connection_pool.get(), _connection_pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return "";
	}

	ReplyPtr reply(static_cast<redisReply*>(redisCommand(
		connection.Get(), "HGET %b %b",
		key.data(), key.size(), hkey.data(), hkey.size())));
	if (!reply || reply->type != REDIS_REPLY_STRING || reply->str == nullptr)
	{
		return "";
	}

	// 显式使用 len，避免字段值含 '\0' 时被提前截断。
	return std::string(reply->str, reply->len);
}

bool RedisMgr::Del(const std::string& key)
{
	RedisConnectionGuard connection(_connection_pool.get(), _connection_pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	ReplyPtr reply(static_cast<redisReply*>(redisCommand(
		connection.Get(), "DEL %b", key.data(), key.size())));
	// DEL 返回删除数量：0 表示键原本不存在，仍说明命令已经被 Redis 正常执行。
	return reply && reply->type == REDIS_REPLY_INTEGER;
}

bool RedisMgr::ExistsKey(const std::string& key)
{
	RedisConnectionGuard connection(_connection_pool.get(), _connection_pool->GetConnection());
	if (connection.Get() == nullptr)
	{
		return false;
	}

	ReplyPtr reply(static_cast<redisReply*>(redisCommand(
		connection.Get(), "EXISTS %b", key.data(), key.size())));
	// EXISTS 返回 1 表示存在，0 表示不存在。
	return reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
}

bool RedisMgr::IsAvailable() const
{
	// 先判断 unique_ptr，避免构造过程失败或析构阶段访问空连接池。
	return _connection_pool && _connection_pool->IsAvailable();
}

void RedisMgr::Close()
{
	// Close 是幂等操作：多次调用不会重复释放同一条连接。
	if (_connection_pool)
	{
		_connection_pool->Close();
	}
}
