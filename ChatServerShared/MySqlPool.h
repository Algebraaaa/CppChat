#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <mysql_connection.h>
#include <queue>
#include <string>

// 连接池策略由服务配置层传入，共享实现不依赖 ConfigMgr 或协议错误码。
struct MySqlPoolOptions {
  std::chrono::milliseconds borrow_timeout{3000};
  int connect_timeout_seconds = 3;
  int read_timeout_seconds = 5;
  int write_timeout_seconds = 5;
};

// GateServer 与聊天节点共用实现；各进程独立持有连接及运行状态。
class MySqlPool {
public:
  using ErrorReporter = std::function<void(const std::string &)>;

  MySqlPool(const std::string &url, const std::string &user, const std::string &password,
            const std::string &schema, std::size_t pool_size, MySqlPoolOptions options = {},
            ErrorReporter report_error = {});
  ~MySqlPool();
  MySqlPool(const MySqlPool &) = delete;
  MySqlPool &operator=(const MySqlPool &) = delete;

  // 借出前检查并重建失效连接；超时、关闭或重建失败时返回空指针。
  std::unique_ptr<sql::Connection> GetConnection();
  void ReturnConnection(std::unique_ptr<sql::Connection> connection);
  void Close();

private:
  std::unique_ptr<sql::Connection> CreateConnection() const;
  void EnsureConnected(std::unique_ptr<sql::Connection> &connection) const;
  void ReportFailure(const std::string &message) const noexcept;

  std::string _url;
  std::string _user;
  std::string _password;
  std::string _schema;
  MySqlPoolOptions _options;
  ErrorReporter _report_error;

  // 空指针是重建失败的槽位，保留容量供后续请求重试。
  std::queue<std::unique_ptr<sql::Connection>> _connections;
  std::mutex _mutex;
  std::condition_variable _condition;
  bool _stopped = false; // 由 _mutex 保护。
};
