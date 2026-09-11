#pragma once

#include <hiredis/hiredis.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>

/*
 * RedisConPool：GateServer 的 Redis 连接池。
 *
 * hiredis 的 redisContext 可以理解成“一条已经连接到 Redis 的 TCP 会话”。
 * 如果每次执行 GET/SET 都重新连接、认证、断开，会浪费大量时间，所以这里在程序
 * 启动时创建多条连接，放进队列中供不同业务线程重复使用。
 *
 * 一次完整的借还流程：
 *   RedisMgr 调用 GetConnection()
 *       -> 从 _connections 队列取出一个 redisContext*
 *       -> 执行 Redis 命令
 *       -> RedisConnectionGuard 析构
 *       -> 调用 ReturnConnection() 把连接放回队列
 *
 * 连接池只负责连接的创建、借出、归还和销毁，不负责 GET/SET 等具体业务命令。
 */
class RedisConPool
{
public:
	// pool_size：计划创建的连接数量。
	// host/port：Redis 服务地址；password：AUTH 使用的密码，可以为空。
	RedisConPool(std::size_t pool_size, std::string host, int port, std::string password);
	// 析构时调用 Close，释放所有仍在连接池中的空闲连接。
	~RedisConPool();

	// 连接池包含互斥量、条件变量和一组原始连接，不能安全地复制所有权。
	RedisConPool(const RedisConPool&) = delete;
	RedisConPool& operator=(const RedisConPool&) = delete;

	// 借出一条连接。没有空闲连接时会等待；连接池关闭后返回 nullptr。
	redisContext* GetConnection();
	// 归还连接。坏连接会被释放，并尝试创建一条新连接补回池中。
	void ReturnConnection(redisContext* context);
	// 停止继续借出连接，释放空闲连接，并唤醒所有正在等待的线程。
	void Close();
	// 至少还有一条存活连接且连接池没有关闭时返回 true。
	bool IsAvailable() const;

private:
	// 创建 TCP 连接并执行 AUTH。成功返回连接指针，失败返回 nullptr。
	redisContext* CreateConnection() const;

	// 当前没有被业务线程借走的空闲连接队列。
	// hiredis 使用 C 接口，所以队列保存原始指针；最终必须调用 redisFree 释放。
	std::queue<redisContext*> _connections;
	// 保护 _connections。const 成员函数 IsAvailable 目前不加锁，但保留 mutable
	// 使以后在 const 查询中加锁时不需要修改函数签名。
	mutable std::mutex _mutex;
	// 所有连接都被借走时，GetConnection 在这里休眠；归还连接时唤醒一个线程。
	std::condition_variable _condition;
	// atomic 允许多个线程安全读取/修改关闭状态，不需要仅为了读状态就加队列锁。
	std::atomic<bool> _stopped{ false };
	// 统计空闲连接和已借出连接的总数，用来判断连接池是否还有可用连接。
	std::atomic<std::size_t> _live_connections{ 0 };
	// 下面三项是创建或重建连接时需要的固定配置，构造后不再改变。
	const std::string _host;
	const int _port;
	const std::string _password;
};
