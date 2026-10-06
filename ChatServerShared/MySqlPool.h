#pragma once
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <mysql_connection.h>

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
  MySqlPool(const std::string &url, const std::string &user, const std::string &password,
            const std::string &schema, std::size_t pool_size);
  ~MySqlPool();

  // 连接对象具有唯一所有权，不能让两个连接池同时管理同一批连接，
  // 因此明确禁止复制构造和复制赋值。
  MySqlPool(const MySqlPool &) = delete;
  MySqlPool &operator=(const MySqlPool &) = delete;

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
