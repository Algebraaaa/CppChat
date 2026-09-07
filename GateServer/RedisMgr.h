#pragma once

#include "RedisConPool.h"
#include "Singleton.h"

#include <memory>
#include <string>

/*
 * RedisMgr：GateServer 访问 Redis 的统一业务入口。
 *
 * 上层代码不需要了解 hiredis 的 redisContext、redisReply 和内存释放规则，
 * 只需要调用 RedisMgr::GetInstance()->Get/Set/Del 等函数。
 *
 * RedisMgr 和 RedisConPool 的职责区别：
 *   RedisMgr     —— 把 C++ 参数转换成 GET、SET、HSET 等 Redis 命令；
 *   RedisConPool —— 管理这些命令使用的网络连接。
 *
 * Singleton 保证整个 GateServer 共享同一个 RedisMgr；enable_shared_from_this
 * 允许对象在确有需要时从自身取得 shared_ptr，目前这些 Redis 命令没有使用它。
 */
class RedisMgr : public Singleton<RedisMgr>,
	public std::enable_shared_from_this<RedisMgr>
{
	friend class Singleton<RedisMgr>;

public:
	// 析构时关闭连接池。
	~RedisMgr();

	// 读取字符串键。成功时把结果写入 value；键不存在或命令失败时返回 false。
	bool Get(const std::string& key, std::string& value);
	// 保存字符串键值，Redis 返回 OK 时为 true。
	bool Set(const std::string& key, const std::string& value);
	// 从列表左侧压入元素，返回值表示命令是否成功。
	bool LPush(const std::string& key, const std::string& value);
	// 从列表左侧弹出元素，成功时通过 value 返回弹出的内容。
	bool LPop(const std::string& key, std::string& value);
	// 从列表右侧压入元素。
	bool RPush(const std::string& key, const std::string& value);
	// 从列表右侧弹出元素。
	bool RPop(const std::string& key, std::string& value);
	// 设置哈希表 key 中字段 hkey 的值；这是方便 std::string 调用的重载。
	bool HSet(const std::string& key, const std::string& hkey, const std::string& value);
	// 底层 HSET 重载允许 hvalue 包含 '\0'，所以需要显式传入 hvaluelen。
	bool HSet(const char* key, const char* hkey, const char* hvalue, std::size_t hvaluelen);
	// 读取哈希字段；不存在或失败时返回空字符串。
	// 注意：这个接口无法区分“字段值本来就是空字符串”和“读取失败”。
	std::string HGet(const std::string& key, const std::string& hkey);
	// 删除键。只要 Redis 正常返回整数回复就视为命令执行成功。
	bool Del(const std::string& key);
	// 判断键是否存在。
	bool ExistsKey(const std::string& key);
	// 查询底层连接池是否仍有存活连接。
	bool IsAvailable() const;
	// 主动关闭底层连接池；重复调用是安全的。
	void Close();

private:
	// 构造函数私有，只有 Singleton<RedisMgr> 能创建实例。
	// 构造过程会读取 config.ini 的 [Redis] 配置并建立连接池。
	RedisMgr();

	// unique_ptr 表示 RedisMgr 独占连接池；RedisMgr 析构时连接池自动析构。
	std::unique_ptr<RedisConPool> _connection_pool;
};
