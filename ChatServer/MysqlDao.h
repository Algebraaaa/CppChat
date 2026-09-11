#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <mysql_connection.h>

#include "data.h"

/*
 * MySqlPool 是一个简单的 MySQL 连接池。
 *
 * 为什么需要连接池：
 * 每次收到注册请求都重新连接 MySQL，会重复进行 TCP 连接和身份验证，开销较大。
 * 连接池会提前创建若干连接，把它们放进队列；业务线程需要访问数据库时取出一个，
 * 使用结束后再归还。多个注册请求就可以复用这些连接。
 *
 * 基本流程：
 * MysqlDao 构造 -> MySqlPool 创建 N 个连接 -> GetConnection 借出连接
 * -> 执行 SQL -> ReturnConnection 归还连接 -> Close 关闭连接池。
 */
class MySqlPool
{
public:
	// url：服务器地址，例如 tcp://127.0.0.1:3306。
	// user/password：应用专用数据库账号，不应该使用 root 账号运行服务器。
	// schema：连接成功后默认使用的数据库，例如 cppchat。
	// pool_size：连接池预先创建的连接数量。
	MySqlPool(
		const std::string& url,
		const std::string& user,
		const std::string& password,
		const std::string& schema,
		std::size_t pool_size);
	~MySqlPool();

	// 连接对象具有唯一所有权，不能让两个连接池同时管理同一批连接，
	// 因此明确禁止复制构造和复制赋值。
	MySqlPool(const MySqlPool&) = delete;
	MySqlPool& operator=(const MySqlPool&) = delete;

	// 从队列借出一个连接。
	// 如果连接暂时全被占用，会等待其他线程归还；连接池关闭后返回 nullptr。
	std::unique_ptr<sql::Connection> GetConnection();

	// 把使用完的连接放回队列，并唤醒一个正在等待连接的线程。
	void ReturnConnection(std::unique_ptr<sql::Connection> connection);

	// 标记连接池停止，并唤醒所有等待线程，使程序能够正常退出。
	void Close();

private:
	// 下面四项是创建新连接所需的配置。
	std::string _url;
	std::string _user;
	std::string _password;
	std::string _schema;

	// 空闲连接队列。unique_ptr 表示每个连接在任意时刻只有一个所有者。
	std::queue<std::unique_ptr<sql::Connection>> _connections;

	// 多个网络线程可能同时借还连接，所以访问队列前必须加锁。
	std::mutex _mutex;

	// 没有空闲连接时，线程在条件变量上等待，避免 while 循环空转占用 CPU。
	std::condition_variable _condition;

	// 受 _mutex 保护。true 表示连接池已经关闭，不再借出或接收连接。
	bool _stopped = false;
};

/*
 * MysqlDao（Data Access Object，数据访问对象）负责真正执行 SQL。
 * 上层只需要调用 RegUser，不需要知道连接池、SQL 占位符和异常码等细节。
 */
class MysqlDao
{
public:
	MysqlDao();
	~MysqlDao();

	// name/email/password 来自通过基础校验后的注册请求。
	// 返回值约定：
	//   > 0：注册成功，返回数据库自动生成的用户 uid；
	//   = 0：用户名或邮箱触发唯一索引，说明用户已经存在；
	//   = -1：连接、密码哈希或 SQL 执行出现错误。
	int RegUser(const std::string& name, const std::string& email, const std::string& password);
	bool CheckEmail(const std::string& name, const std::string& email);
	bool UpdatePwd(const std::string& name, const std::string& new_password);
	bool CheckPwdByEmail(const std::string& email, const std::string& password, UserInfo& user_info);
	bool AddFriendApply(int from_uid, int to_uid, const std::string& description, const std::string& remark);
	bool AuthFriendApply(int from_uid, int to_uid);
	bool AddFriend(
		int from_uid,
		int to_uid,
		const std::string& remark,
		std::vector<std::shared_ptr<ChatMessage>>& initial_messages);
	std::shared_ptr<UserInfo> GetUser(int uid);
	std::shared_ptr<UserInfo> GetUser(const std::string& name);
	bool GetApplyList(
		int to_uid,
		std::vector<std::shared_ptr<ApplyInfo>>& applications,
		int begin_id,
		int limit = 10);
	bool GetFriendList(int self_uid, std::vector<std::shared_ptr<UserInfo>>& friends);
	bool GetUserThreads(
		std::int64_t user_id,
		std::int64_t last_id,
		int page_size,
		std::vector<std::shared_ptr<ChatThreadInfo>>& threads,
		bool& load_more,
		int& next_last_id);
	bool CreatePrivateChat(int user1_uid, int user2_uid, int& thread_id);
	std::shared_ptr<PageResult> LoadChatMsg(
		int thread_id,
		int last_message_id,
		int page_size);
	bool AddChatMsg(std::vector<std::shared_ptr<ChatMessage>>& messages);
private:
	// MysqlDao 独占连接池；Dao 析构时连接池也会自动析构。
	std::unique_ptr<MySqlPool> _pool;
};
