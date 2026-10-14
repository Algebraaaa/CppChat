#include "MySqlPool.h"

#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <mysql_driver.h>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
std::string FailureMessage(const sql::SQLException &exception)
{
  std::ostringstream message;
  message << "MySQL connection unavailable: code=" << exception.getErrorCode()
          << ", state=" << exception.getSQLState() << ", message=" << exception.what();
  return message.str();
}
} // namespace

MySqlPool::MySqlPool(const std::string &url, const std::string &user, const std::string &password,
                     const std::string &schema, std::size_t pool_size, MySqlPoolOptions options,
                     ErrorReporter report_error)
    : _url(url), _user(user), _password(password), _schema(schema), _options(options),
      _report_error(std::move(report_error))
{
  const auto valid_seconds = [](int value) { return value > 0 && value <= 300; };
  if (pool_size == 0 || _options.borrow_timeout.count() <= 0 ||
      _options.borrow_timeout > std::chrono::seconds(300) ||
      !valid_seconds(_options.connect_timeout_seconds) ||
      !valid_seconds(_options.read_timeout_seconds) ||
      !valid_seconds(_options.write_timeout_seconds)) {
    throw std::invalid_argument("Invalid MySQL pool size or timeout policy.");
  }
  for (std::size_t index = 0; index < pool_size; ++index) {
    try {
      _connections.push(CreateConnection());
    } catch (const sql::SQLException &exception) {
      // 启动时数据库不可用也保留槽位，恢复后可按需建立连接。
      _connections.push(nullptr);
      ReportFailure(FailureMessage(exception));
    }
  }
}

MySqlPool::~MySqlPool() { Close(); }

std::unique_ptr<sql::Connection> MySqlPool::CreateConnection() const
{
  sql::ConnectOptionsMap options;
  options["hostName"] = sql::SQLString(_url);
  options["userName"] = sql::SQLString(_user);
  options["password"] = sql::SQLString(_password);
  options["OPT_CONNECT_TIMEOUT"] = _options.connect_timeout_seconds;
  options["OPT_READ_TIMEOUT"] = _options.read_timeout_seconds;
  options["OPT_WRITE_TIMEOUT"] = _options.write_timeout_seconds;
  // 默认关闭隐式重连；不设置已弃用的 OPT_RECONNECT，恢复由连接池显式完成。
  // 静态初始化由 C++ 同步，工作线程不并发查询全局驱动实例。
  static auto *driver = sql::mysql::get_mysql_driver_instance();
  std::unique_ptr<sql::Connection> connection(driver->connect(options));
  connection->setSchema(_schema);
  return connection;
}

void MySqlPool::EnsureConnected(std::unique_ptr<sql::Connection> &connection) const
{
  if (connection) {
    try {
      std::unique_ptr<sql::Statement> statement(connection->createStatement());
      std::unique_ptr<sql::ResultSet> result(statement->executeQuery("SELECT 1"));
      if (result->next()) {
        return;
      }
    } catch (const sql::SQLException &) {
      // 对象存在不代表服务端会话仍有效；丢弃旧连接并恢复默认库。
    }
  }
  connection.reset();
  connection = CreateConnection();
}

void MySqlPool::ReportFailure(const std::string &message) const noexcept
{
  try {
    if (_report_error) {
      _report_error(message);
    }
  } catch (...) {
    // 日志回调不能破坏连接池恢复，也不能改变槽位数量。
  }
}

std::unique_ptr<sql::Connection> MySqlPool::GetConnection()
{
  std::unique_lock<std::mutex> lock(_mutex);
  if (!_condition.wait_for(lock, _options.borrow_timeout,
                           [this] { return _stopped || !_connections.empty(); }) ||
      _stopped) {
    return nullptr;
  }
  auto connection = std::move(_connections.front());
  _connections.pop();
  lock.unlock();

  // 网络操作离开队列锁，其他线程可继续借还连接。
  try {
    EnsureConnected(connection);
  } catch (const sql::SQLException &exception) {
    lock.lock();
    if (!_stopped) {
      _connections.push(nullptr);
    }
    lock.unlock();
    _condition.notify_one();
    ReportFailure(FailureMessage(exception));
    return nullptr;
  }
  lock.lock();
  if (_stopped) {
    return nullptr;
  }
  return connection;
}

void MySqlPool::ReturnConnection(std::unique_ptr<sql::Connection> connection)
{
  if (!connection) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_stopped) {
      return;
    }
    _connections.push(std::move(connection));
  }
  _condition.notify_one();
}

void MySqlPool::Close()
{
  {
    std::lock_guard<std::mutex> lock(_mutex);
    _stopped = true;
    while (!_connections.empty()) {
      _connections.pop();
    }
  }
  _condition.notify_all();
}
