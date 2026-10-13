#pragma once
// Internal SQL tools shared by domain implementation units; not a service API.
#include "Logger.h"
#include "MySqlPool.h"
#include "MysqlDao.h"
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <utility>
namespace MysqlDaoDetail {
/*
 * MySqlConnectionGuard 是“作用域连接守卫”，体现 RAII 思想：
 * 构造时接管从连接池借来的 connection；离开当前作用域时析构，自动归还连接。
 *
 * 因此 RegUser 中无论正常 return，还是 SQL 抛出异常，都不需要手写多次
 * ReturnConnection，也不会因为遗漏某个 return 分支而永久丢失连接。
 */
class MySqlConnectionGuard {
public:
  MySqlConnectionGuard(MySqlPool *pool, std::unique_ptr<sql::Connection> connection)
      : _pool(pool), _connection(std::move(connection))
  {
  }

  ~MySqlConnectionGuard()
  {
    // 只有连接池和连接都有效时才归还；std::move 转移 unique_ptr 所有权。
    if (_pool != nullptr && _connection != nullptr) {
      _pool->ReturnConnection(std::move(_connection));
    }
  }

  // 对外返回普通指针只是为了调用 Connection 方法，所有权仍属于守卫。
  sql::Connection *Get() const { return _connection.get(); }

private:
  MySqlPool *_pool;
  std::unique_ptr<sql::Connection> _connection;
};

class MySqlTransaction {
public:
  explicit MySqlTransaction(sql::Connection *connection) : _connection(connection)
  {
    _connection->setAutoCommit(false);
  }

  ~MySqlTransaction()
  {
    if (_connection == nullptr) {
      return;
    }

    try {
      if (!_finished) {
        _connection->rollback();
      }
      _connection->setAutoCommit(true);
    } catch (...) {
    }
  }

  void Commit()
  {
    _connection->commit();
    _finished = true;
    _connection->setAutoCommit(true);
  }

private:
  sql::Connection *_connection;
  bool _finished = false;
};

inline void LogSqlError(const char *operation, const sql::SQLException &exception)
{
  LOG_ERROR(operation, ": code=", exception.getErrorCode(), ", state=", exception.getSQLState(),
            ", message=", exception.what());
}

inline std::shared_ptr<UserInfo> ReadUserInfo(sql::ResultSet &result)
{
  auto user = std::make_shared<UserInfo>();
  user->uid = result.getInt("uid");
  user->name = result.getString("name");
  user->email = result.getString("email");
  user->nick = result.getString("nick");
  user->desc = result.getString("descs");
  user->sex = result.getInt("sex");
  user->icon = result.getString("icon");
  return user;
}

} // namespace MysqlDaoDetail
