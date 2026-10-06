#include "MySqlPool.h"
#include "Logger.h"
#include <mysql_driver.h>
#include <cppconn/exception.h>
#include <chrono>
#include <stdexcept>
#include <utility>

MySqlPool::MySqlPool(const std::string &url, const std::string &user, const std::string &password,
                     const std::string &schema, std::size_t pool_size)
    // 初始化列表先保存配置，再进入构造函数体创建连接。
    // 这些值来自构造函数参数，而不是要求外部传入成员变量本身。
    : _url(url), _user(user), _password(password), _schema(schema)
{
  // 大小为 0 的连接池永远借不到连接，因此直接拒绝这种配置。
  if (pool_size == 0) {
    throw std::invalid_argument("MySQL pool size must be greater than zero.");
  }

  // Connector/C++ 提供一个驱动实例，它负责根据地址和账号创建 Connection。
  // driver 由库管理，本类只使用它，不负责 delete。
  sql::mysql::MySQL_Driver *driver = sql::mysql::get_mysql_driver_instance();

  try {
    // 启动时一次性创建 pool_size 个连接，并全部放入“空闲连接队列”。
    for (std::size_t index = 0; index < pool_size; ++index) {
      // driver->connect 返回堆上的 Connection，立即交给 unique_ptr 管理。
      std::unique_ptr<sql::Connection> connection(driver->connect(_url, _user, _password));

      // 选择默认数据库，以后 SQL 可以直接写 users，不必写 cppchat.users。
      connection->setSchema(_schema);

      // move 后，局部变量不再拥有连接，队列成为新的唯一所有者。
      _connections.push(std::move(connection));
    }
  } catch (const sql::SQLException &exception) {
    LOG_ERROR("MySQL connection pool initialization failed: code=", exception.getErrorCode(),
              ", state=", exception.getSQLState(), ", message=", exception.what());
    // 构造阶段如果有连接创建失败，重新抛出异常，不留下“看似可用”的空连接池。
    throw;
  }

  LOG_INFO("MySQL connection pool initialized successfully: schema=", _schema,
           ", connections=", pool_size);
}

MySqlPool::~MySqlPool()
{
  // 先唤醒并停止可能仍在等待连接的线程。
  Close();

  // 清空 unique_ptr 队列时，每个 Connection 都会自动析构并断开连接。
  std::lock_guard<std::mutex> lock(_mutex);
  while (!_connections.empty()) {
    _connections.pop();
  }
  LOG_INFO("MySQL connection pool destroyed successfully.");
}

std::unique_ptr<sql::Connection> MySqlPool::GetConnection()
{
  // unique_lock 与 lock_guard 不同：condition_variable::wait
  // 等待期间需要临时解锁， 让其他线程能够进入 ReturnConnection 把连接放回队列。
  std::unique_lock<std::mutex> lock(_mutex);

  // wait 的条件为：连接池已停止，或者至少有一个空闲连接。
  // 条件不满足时线程休眠，不会一直循环浪费 CPU。
  if (!_condition.wait_for(lock, std::chrono::seconds(3),
                           [this]() { return _stopped || !_connections.empty(); }))
    return nullptr;

  // Close 会将 _stopped 设为 true 并唤醒等待者，此时不能再借出连接。
  if (_stopped) {
    return nullptr;
  }

  // 取出队首连接并从队列删除。返回 unique_ptr 后，调用者暂时拥有它。
  auto connection = std::move(_connections.front());
  _connections.pop();
  return connection;
}

void MySqlPool::ReturnConnection(std::unique_ptr<sql::Connection> connection)
{
  // 空指针不是有效连接，直接忽略，防止把 nullptr 放进队列。
  if (connection == nullptr) {
    return;
  }

  {
    // 修改共享队列和 _stopped 前加锁，防止多个线程同时操作造成数据竞争。
    std::lock_guard<std::mutex> lock(_mutex);
    if (_stopped) {
      // 连接池已经关闭时不再入队；函数结束会销毁 unique_ptr 和连接。
      return;
    }
    _connections.push(std::move(connection));
  }

  // 队列已经有连接，唤醒一个等待线程。通知放在解锁后可减少无谓竞争。
  _condition.notify_one();
}

void MySqlPool::Close()
{
  {
    // _stopped 和连接队列使用同一把锁，保证等待条件检查的一致性。
    std::lock_guard<std::mutex> lock(_mutex);
    _stopped = true;
  }

  // 必须唤醒全部等待者，否则它们可能永远睡眠，导致程序退出时卡住。
  _condition.notify_all();
}
