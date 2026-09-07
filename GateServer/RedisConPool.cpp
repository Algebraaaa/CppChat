#include "RedisConPool.h"

#include "Logger.h"

namespace
{
	// AUTH、SET 等命令成功时，Redis 通常返回类型为 STATUS、内容为 "OK" 的 reply。
	// 这个小函数集中完成空指针、回复类型、字符串指针和具体内容四层检查。
	bool IsStatusOk(const redisReply* reply)
	{
		return reply != nullptr && reply->type == REDIS_REPLY_STATUS &&
			reply->str != nullptr && std::string(reply->str, reply->len) == "OK";
	}
}

RedisConPool::RedisConPool(
	std::size_t pool_size,
	std::string host,
	int port,
	std::string password)
	// host 和 password 传值进入构造函数，再 move 到成员中，避免额外复制字符串。
	// _port 是整数，直接复制即可。
	: _host(std::move(host)),
	_port(port),
	_password(std::move(password))
{
	// 尝试创建 pool_size 条连接。单条连接失败不会马上中断，剩余连接仍会继续创建。
	// 因此配置为 5 条时，即使只成功 3 条，服务器仍能使用这 3 条连接工作。
	for (std::size_t index = 0; index < pool_size; ++index)
	{
		redisContext* context = CreateConnection();
		if (context == nullptr)
		{
			continue;
		}

		// 新连接当前没有借给任何线程，所以先进入空闲队列。
		_connections.push(context);
		++_live_connections;
	}

	// 一条连接都没有创建成功时，GetConnection 不能永远等待，因此直接标记停止。
	if (_connections.empty())
	{
		_stopped = true;
		LOG_ERROR(
			"Redis connection pool has no available connections.");
	}
	else
	{
		LOG_INFO(
			"Redis connection pool initialized successfully: endpoint=",
			_host, ":", _port,
			", connections=", _live_connections.load());
	}
}

RedisConPool::~RedisConPool()
{
	// Close 可以重复调用，所以即使外部已经主动关闭，这里再次调用也安全。
	Close();
	LOG_INFO("Redis connection pool destroyed successfully.");
}

redisContext* RedisConPool::CreateConnection() const
{
	// redisConnect 同步建立到 host:port 的 TCP 连接。
	// 返回的 redisContext 由本连接池负责，最终必须使用 redisFree 释放。
	redisContext* context = redisConnect(_host.c_str(), _port);
	if (context == nullptr)
	{
		LOG_ERROR("Unable to allocate a Redis connection.");
		return nullptr;
	}

	// context 不为空只表示结构体分配成功；err 非 0 仍代表连接失败。
	if (context->err != 0)
	{
		LOG_ERROR("Redis connection failed: ", context->errstr);
		redisFree(context);
		return nullptr;
	}

	// 配置了密码才执行 AUTH。%b 接受“指针 + 长度”，不会把密码拼接进命令字符串，
	// 同时也不会在日志中输出密码。
	if (!_password.empty())
	{
		redisReply* reply = static_cast<redisReply*>(
			redisCommand(context, "AUTH %b", _password.data(), _password.size()));
		const bool authenticated = IsStatusOk(reply);
		// redisCommand 返回的 redisReply 不会自动释放，每一次都要 freeReplyObject。
		if (reply != nullptr)
		{
			freeReplyObject(reply);
		}

		if (!authenticated)
		{
			LOG_ERROR("Redis authentication failed.");
			redisFree(context);
			return nullptr;
		}
	}

	// 到这里说明 TCP 连接和可选的 AUTH 都成功，连接可以进入池中。
	return context;
}

redisContext* RedisConPool::GetConnection()
{
	// unique_lock 可以在 condition_variable::wait 内部临时解锁。
	// 等待期间不占用 _mutex，其他线程才能进入 ReturnConnection 归还连接。
	std::unique_lock<std::mutex> lock(_mutex);
	// 使用带谓词的 wait 可以正确处理“虚假唤醒”：
	// 只有连接池关闭，或者队列中真的出现连接时，线程才继续执行。
	_condition.wait(lock, [this]() {
		return _stopped || !_connections.empty();
		});

	// Close 会把 _stopped 设为 true 并唤醒全部等待者；此时不能再借出连接。
	if (_stopped)
	{
		return nullptr;
	}

	// front 取得队首指针，pop 把它从空闲队列移除。
	// 从此刻到 ReturnConnection 之前，这条连接只属于当前业务线程。
	redisContext* context = _connections.front();
	_connections.pop();
	return context;
}

void RedisConPool::ReturnConnection(redisContext* context)
{
	// 空连接没有可归还内容，直接结束。
	if (context == nullptr)
	{
		return;
	}

	// 连接池已经关闭时，归还动作变成“释放动作”，不能再放回队列。
	if (_stopped)
	{
		redisFree(context);
		--_live_connections;
		return;
	}

	// hiredis 会把网络错误记录在 context->err。
	// 坏连接不能继续复用：先释放，再尝试创建新连接补足池的容量。
	if (context->err != 0)
	{
		redisFree(context);
		--_live_connections;
		context = CreateConnection();
		if (context == nullptr)
		{
			// 最后一条连接也失效且重连失败时，连接池必须停止并唤醒等待线程，
			// 否则它们会在一个永远不会再得到连接的队列上永久等待。
			if (_live_connections == 0)
			{
				_stopped = true;
				_condition.notify_all();
			}
			return;
		}
		++_live_connections;
	}

	{
		// 再次加锁是为了安全修改共享队列。
		std::lock_guard<std::mutex> lock(_mutex);
		// 加锁前可能恰好有另一个线程调用了 Close，所以锁内必须再次检查状态。
		if (_stopped)
		{
			redisFree(context);
			--_live_connections;
			return;
		}
		_connections.push(context);
	}
	// 队列中新增一条连接，只需唤醒一个等待借连接的线程。
	_condition.notify_one();
}

void RedisConPool::Close()
{
	// 2026-08-12 对比：
	// 旧写法：Close() 只把 _stopped 设为 true，空闲连接必须等析构才释放。
	// 新写法：Close() 立即释放队列中的空闲连接；已借出的连接归还时再释放。
	// 好处：Close() 的行为真正符合“关闭连接池”，能更早释放 Redis 网络资源。
	// exchange 会以原子方式设为 true，并返回修改前的值。
	// 原值已经是 true，说明别的线程或析构函数已经关闭过，无需重复释放。
	const bool was_stopped = _stopped.exchange(true);
	if (was_stopped)
	{
		return;
	}

	{
		// 只释放当前仍在空闲队列里的连接。
		// 已经借出的连接不在队列中，将在以后 ReturnConnection 时释放。
		std::lock_guard<std::mutex> lock(_mutex);
		while (!_connections.empty())
		{
			redisFree(_connections.front());
			_connections.pop();
			--_live_connections;
		}
	}

	// 唤醒全部正在 GetConnection 中等待的线程，让它们看到 _stopped 并返回 nullptr。
	_condition.notify_all();
}

bool RedisConPool::IsAvailable() const
{
	// _live_connections 包含“空闲 + 已借出”连接，所以它大于 0 代表至少还有一条活连接。
	return !_stopped && _live_connections > 0;
}
