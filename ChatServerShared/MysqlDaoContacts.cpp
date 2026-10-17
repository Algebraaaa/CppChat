#include "MysqlDao.h"
#include "MysqlDaoSupport.h"
#include "const.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <thread>
#include <utility>
using MysqlDaoDetail::LogSqlError;
using MysqlDaoDetail::MySqlConnectionGuard;
using MysqlDaoDetail::MySqlTransaction;
using MysqlDaoDetail::ReadUserInfo;

namespace {
struct ContactApplication {
  bool exists = false;
  std::string remark;
  std::string tags = "[]";
  std::string description;
};

// Lock the two directional keys in the same order for apply, accept and delete.
// Missing records can still take gap locks, so deadlocks require a full retry.
std::array<ContactApplication, 2> LockApplications(sql::Connection *connection, int first_uid,
                                                   int second_uid)
{
  std::array<ContactApplication, 2> applications;
  for (std::size_t index = 0; index < applications.size(); ++index) {
    std::unique_ptr<sql::PreparedStatement> statement(connection->prepareStatement(
        "SELECT back_name, descs, COALESCE(tags, '[]') AS tags FROM friend_apply "
        "WHERE from_uid = ? AND to_uid = ? FOR UPDATE"));
    statement->setInt(1, index == 0 ? first_uid : second_uid);
    statement->setInt(2, index == 0 ? second_uid : first_uid);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    if (result->next()) {
      auto &application = applications[index];
      application.exists = true;
      application.remark = result->getString("back_name");
      application.tags = result->getString("tags");
      application.description = result->getString("descs");
    }
  }
  return applications;
}

std::array<bool, 2> LockFriends(sql::Connection *connection, int first_uid, int second_uid)
{
  std::array<bool, 2> friends{};
  for (std::size_t index = 0; index < friends.size(); ++index) {
    std::unique_ptr<sql::PreparedStatement> statement(connection->prepareStatement(
        "SELECT 1 FROM friend WHERE self_id = ? AND friend_id = ? FOR UPDATE"));
    statement->setInt(1, index == 0 ? first_uid : second_uid);
    statement->setInt(2, index == 0 ? second_uid : first_uid);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    friends[index] = result->next();
  }
  return friends;
}

void LogContactSqlError(const char *operation, const sql::SQLException &exception)
{
  // Connector messages may contain SQL values; never log contact data here.
  LOG_ERROR(operation, ": code=", exception.getErrorCode(), ", state=", exception.getSQLState());
}

template <typename Operation>
bool RunContactTransaction(sql::Connection *connection, const char *failure, Operation operation)
{
  constexpr int max_attempts = 3;
  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    bool retry = false;
    try {
      MySqlTransaction transaction(connection);
      bool commit_started = false;
      try {
        if (!operation()) {
          return false;
        }
        // An exception from commit can mean that the server committed already.
        // Retrying such an unknown outcome would repeat user-visible effects.
        commit_started = true;
        transaction.Commit();
        return true;
      } catch (const sql::SQLException &exception) {
        const int code = exception.getErrorCode();
        if (!commit_started && (code == 1213 || code == 1205) && attempt + 1 < max_attempts) {
          // A lock timeout rolls back only its statement by default. Retry only
          // after rollback AND autocommit restoration have both succeeded.
          transaction.Rollback();
          retry = true;
        } else {
          LogContactSqlError(failure, exception);
          return false;
        }
      }
    } catch (const sql::SQLException &exception) {
      // Transaction setup and rollback failures are not safe retry points.
      LogContactSqlError(failure, exception);
      return false;
    }
    if (retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10 * (attempt + 1)));
    }
  }
  return false;
}
} // namespace

bool MysqlDao::AddFriendApply(int from_uid, int to_uid, const std::string &description,
                              const std::string &remark, const std::string &tags_json,
                              bool *notify_application)
{
  if (notify_application) {
    *notify_application = false;
  }
  if (from_uid <= 0 || to_uid <= 0 || from_uid == to_uid || !_pool) {
    return false;
  }
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get()) {
    return false;
  }
  const int first_uid = std::min(from_uid, to_uid);
  const int second_uid = std::max(from_uid, to_uid);
  bool notify = false;
  const bool success =
      RunContactTransaction(connection.Get(), "MySQL friend application write failed", [&] {
        notify = false;
        LockApplications(connection.Get(), first_uid, second_uid);
        const auto friends = LockFriends(connection.Get(), first_uid, second_uid);
        if (friends[0] && friends[1]) {
          return true; // Existing contacts retain their accepted application and metadata.
        }
        std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
            "INSERT INTO friend_apply (from_uid, to_uid, descs, back_name, tags) "
            "VALUES (?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE descs = VALUES(descs), "
            "back_name = VALUES(back_name), tags = VALUES(tags), status = 0"));
        statement->setInt(1, from_uid);
        statement->setInt(2, to_uid);
        statement->setString(3, description);
        statement->setString(4, remark);
        statement->setString(5, tags_json);
        statement->executeUpdate();
        notify = true;
        return true;
      });
  if (success && notify_application) {
    *notify_application = notify;
  }
  return success;
}

bool MysqlDao::AuthFriendApply(int from_uid, int to_uid)
{
  if (from_uid <= 0 || to_uid <= 0 || from_uid == to_uid || !_pool) {
    return false;
  }
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get()) {
    return false;
  }
  return RunContactTransaction(
      connection.Get(), "MySQL friend application authorization failed", [&] {
        const int first_uid = std::min(from_uid, to_uid);
        const int second_uid = std::max(from_uid, to_uid);
        const auto applications = LockApplications(connection.Get(), first_uid, second_uid);
        if (!applications[to_uid == first_uid ? 0 : 1].exists) {
          return false;
        }
        std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
            "UPDATE friend_apply SET status = 1 WHERE from_uid = ? AND to_uid = ?"));
        statement->setInt(1, to_uid);
        statement->setInt(2, from_uid);
        statement->executeUpdate();
        return true;
      });
}

bool MysqlDao::AddFriend(int from_uid, int to_uid, const std::string &remark,
                         const std::string &tags_json,
                         std::vector<std::shared_ptr<ChatMessage>> &initial_messages)
{
  initial_messages.clear();
  if (from_uid <= 0 || to_uid <= 0 || from_uid == to_uid || !_pool) {
    return false;
  }
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get()) {
    return false;
  }
  const int first_uid = std::min(from_uid, to_uid);
  const int second_uid = std::max(from_uid, to_uid);
  std::vector<std::shared_ptr<ChatMessage>> committed_messages;
  const bool success = RunContactTransaction(connection.Get(), "MySQL friend creation failed", [&] {
    // An aborted attempt never exposes messages or generated IDs to its caller.
    committed_messages.clear();
    const auto applications = LockApplications(connection.Get(), first_uid, second_uid);
    const auto friends = LockFriends(connection.Get(), first_uid, second_uid);
    const auto &incoming = applications[to_uid == first_uid ? 0 : 1];
    if (!(friends[0] && friends[1]) && !incoming.exists) {
      return false;
    }
    for (std::size_t index = 0; index < applications.size(); ++index) {
      if (!applications[index].exists) {
        continue;
      }
      std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
          "UPDATE friend_apply SET status = 1 WHERE from_uid = ? AND to_uid = ?"));
      statement->setInt(1, index == 0 ? first_uid : second_uid);
      statement->setInt(2, index == 0 ? second_uid : first_uid);
      statement->executeUpdate();
    }
    if (friends[0] && friends[1]) {
      return true; // Accept retries do not overwrite owner metadata or repeat initial messages.
    }
    for (std::size_t index = 0; index < friends.size(); ++index) {
      if (friends[index]) {
        continue;
      }
      const int self_uid = index == 0 ? first_uid : second_uid;
      const int friend_uid = index == 0 ? second_uid : first_uid;
      std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
          "INSERT INTO friend (self_id, friend_id, back, tags) VALUES (?, ?, ?, ?)"));
      statement->setInt(1, self_uid);
      statement->setInt(2, friend_uid);
      statement->setString(3, self_uid == from_uid ? remark : incoming.remark);
      statement->setString(4, self_uid == from_uid ? tags_json : incoming.tags);
      statement->executeUpdate();
    }
    int thread_id = 0;
    {
      std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
          "SELECT thread_id FROM private_chat WHERE user1_id = ? AND user2_id = ? FOR UPDATE"));
      statement->setInt(1, first_uid);
      statement->setInt(2, second_uid);
      std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
      if (result->next()) {
        thread_id = result->getInt("thread_id");
      }
    }
    if (thread_id == 0) {
      std::unique_ptr<sql::PreparedStatement> thread_statement(connection.Get()->prepareStatement(
          "INSERT INTO chat_thread (type, created_at) VALUES ('private', NOW())"));
      thread_statement->executeUpdate();
      std::unique_ptr<sql::Statement> id_statement(connection.Get()->createStatement());
      std::unique_ptr<sql::ResultSet> id_result(
          id_statement->executeQuery("SELECT LAST_INSERT_ID() AS thread_id"));
      if (!id_result->next()) {
        return false;
      }
      thread_id = id_result->getInt("thread_id");
      std::unique_ptr<sql::PreparedStatement> private_statement(connection.Get()->prepareStatement(
          "INSERT INTO private_chat (thread_id, user1_id, user2_id, created_at) "
          "VALUES (?, ?, ?, NOW())"));
      private_statement->setInt64(1, thread_id);
      private_statement->setInt(2, first_uid);
      private_statement->setInt(3, second_uid);
      private_statement->executeUpdate();
    }
    auto add_initial_message = [&](int sender_uid, int receiver_uid, const std::string &content) {
      if (content.empty()) {
        return;
      }
      std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
          "INSERT INTO chat_message (thread_id, sender_id, recv_id, content, "
          "created_at, updated_at, status) "
          "VALUES (?, ?, ?, ?, NOW(), NOW(), 2)"));
      statement->setInt64(1, thread_id);
      statement->setInt(2, sender_uid);
      statement->setInt(3, receiver_uid);
      statement->setString(4, content);
      statement->executeUpdate();
      std::unique_ptr<sql::Statement> id_statement(connection.Get()->createStatement());
      std::unique_ptr<sql::ResultSet> id_result(
          id_statement->executeQuery("SELECT LAST_INSERT_ID() AS message_id"));
      if (id_result->next()) {
        auto message = std::make_shared<ChatMessage>();
        message->message_id = id_result->getInt("message_id");
        message->thread_id = thread_id;
        message->sender_id = sender_uid;
        message->recv_id = receiver_uid;
        message->content = content;
        message->status = 2;
        committed_messages.push_back(std::move(message));
      }
    };
    add_initial_message(to_uid, from_uid, incoming.description);
    add_initial_message(from_uid, to_uid, "We are friends now!");
    return true;
  });
  if (success) {
    initial_messages = std::move(committed_messages);
  }
  return success;
}

bool MysqlDao::GetApplyList(int to_uid, std::vector<std::shared_ptr<ApplyInfo>> &applications,
                            int begin_id, int limit)
{
  if (to_uid <= 0 || limit <= 0 || !_pool) {
    return false;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    return false;
  }

  try {
    std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
        "SELECT a.id AS application_id, a.from_uid, a.descs, a.status, u.username AS name, "
        "u.nick, u.sex, u.icon "
        "FROM friend_apply AS a JOIN users AS u ON a.from_uid = u.id "
        "WHERE a.to_uid = ? AND a.id > ? ORDER BY a.id ASC LIMIT ?"));
    statement->setInt(1, to_uid);
    statement->setInt(2, begin_id);
    statement->setInt(3, limit);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    while (result->next()) {
      auto application = std::make_shared<ApplyInfo>(
          result->getInt("from_uid"), result->getString("name"), result->getString("descs"),
          result->getString("icon"), result->getString("nick"), result->getInt("sex"),
          result->getInt("status"));
      application->_id = result->getInt("application_id");
      applications.push_back(std::move(application));
    }
    return true;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL friend application query failed", exception);
    return false;
  }
}

bool MysqlDao::GetFriendList(int self_uid, std::vector<std::shared_ptr<UserInfo>> &friends)
{
  return GetFriendPage(self_uid, friends, 0, std::numeric_limits<int>::max());
}

bool MysqlDao::GetFriendPage(int self_uid, std::vector<std::shared_ptr<UserInfo>> &friends,
                             int begin_uid, int limit)
{
  friends.clear();
  if (self_uid <= 0 || begin_uid < 0 || limit <= 0 || !_pool) {
    return false;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    return false;
  }

  try {
    std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
        "SELECT u.id AS uid, u.username AS name, u.email, u.nick, "
        "u.profile_description AS descs, "
        "u.sex, u.icon, f.back, COALESCE(f.tags, '[]') AS tags FROM friend "
        "AS f "
        "JOIN users AS u ON f.friend_id = u.id "
        "WHERE f.self_id = ? AND f.friend_id > ? ORDER BY f.friend_id ASC LIMIT ?"));
    statement->setInt(1, self_uid);
    statement->setInt(2, begin_uid);
    statement->setInt(3, limit);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    while (result->next()) {
      auto user = ReadUserInfo(*result);
      user->back = result->getString("back");
      user->tags_json = result->getString("tags");
      friends.push_back(std::move(user));
    }
    return true;
  } catch (const sql::SQLException &exception) {
    friends.clear();
    LogContactSqlError("MySQL friend list query failed", exception);
    return false;
  }
}

bool MysqlDao::GetFriendIds(int self_uid, std::vector<int> &friends)
{
  friends.clear();
  if (self_uid <= 0 || !_pool) {
    return false;
  }
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get()) {
    return false;
  }
  try {
    std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
        "SELECT friend_id FROM friend WHERE self_id = ? ORDER BY friend_id ASC"));
    statement->setInt(1, self_uid);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    while (result->next()) {
      friends.push_back(result->getInt("friend_id"));
    }
    return true;
  } catch (const sql::SQLException &exception) {
    friends.clear();
    LogContactSqlError("MySQL friend ID query failed", exception);
    return false;
  }
}

bool MysqlDao::DeleteFriend(int self_uid, int friend_uid)
{
  if (self_uid <= 0 || friend_uid <= 0 || self_uid == friend_uid || !_pool)
    return false;
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get())
    return false;
  return RunContactTransaction(connection.Get(), "MySQL friend deletion failed", [&] {
    const int first_uid = std::min(self_uid, friend_uid);
    const int second_uid = std::max(self_uid, friend_uid);
    LockApplications(connection.Get(), first_uid, second_uid);
    LockFriends(connection.Get(), first_uid, second_uid);
    for (const char *query : {"DELETE FROM friend_apply WHERE from_uid = ? AND to_uid = ?",
                              "DELETE FROM friend WHERE self_id = ? AND friend_id = ?"}) {
      for (int direction = 0; direction < 2; ++direction) {
        std::unique_ptr<sql::PreparedStatement> statement(
            connection.Get()->prepareStatement(query));
        statement->setInt(1, direction == 0 ? first_uid : second_uid);
        statement->setInt(2, direction == 0 ? second_uid : first_uid);
        statement->executeUpdate();
      }
    }
    // Retain the original conversation and history when contacts are re-added.
    return true;
  });
}

int MysqlDao::CheckFriendship(int self_uid, int friend_uid)
{
  if (self_uid <= 0 || friend_uid <= 0 || self_uid == friend_uid)
    return ErrorCodes::UidInvalid;
  if (!_pool)
    return ErrorCodes::DatabaseError;
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get())
    return ErrorCodes::DatabaseError;
  try {
    std::unique_ptr<sql::PreparedStatement> statement(
        connection.Get()->prepareStatement("SELECT 1 FROM friend WHERE self_id "
                                           "= ? AND friend_id = ? LIMIT 1"));
    statement->setInt(1, self_uid);
    statement->setInt(2, friend_uid);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    return result->next() ? ErrorCodes::Success : ErrorCodes::FriendNotFound;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL friendship query failed", exception);
    return ErrorCodes::DatabaseError;
  }
}

int MysqlDao::GetFriendMetadata(int self_uid, int friend_uid, std::string &back,
                                std::string &tags_json)
{
  if (self_uid <= 0 || friend_uid <= 0)
    return ErrorCodes::UidInvalid;
  if (!_pool)
    return ErrorCodes::DatabaseError;
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get())
    return ErrorCodes::DatabaseError;
  try {
    std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
        "SELECT back, COALESCE(tags, '[]') AS tags FROM friend WHERE "
        "self_id = ? AND friend_id = ?"));
    statement->setInt(1, self_uid);
    statement->setInt(2, friend_uid);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    if (!result->next())
      return ErrorCodes::FriendNotFound;
    back = result->getString("back");
    tags_json = result->getString("tags");
    return ErrorCodes::Success;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL friend metadata read failed", exception);
    return ErrorCodes::DatabaseError;
  }
}

int MysqlDao::SetContactTags(int self_uid, int friend_uid, const std::string &tags_json,
                             const std::string *back)
{
  if (self_uid <= 0 || friend_uid <= 0 || self_uid == friend_uid)
    return ErrorCodes::UidInvalid;
  if (!_pool)
    return ErrorCodes::DatabaseError;
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get())
    return ErrorCodes::DatabaseError;
  try {
    MySqlTransaction transaction(connection.Get());
    std::unique_ptr<sql::PreparedStatement> check(
        connection.Get()->prepareStatement("SELECT 1 FROM friend WHERE self_id "
                                           "= ? AND friend_id = ? FOR UPDATE"));
    check->setInt(1, self_uid);
    check->setInt(2, friend_uid);
    std::unique_ptr<sql::ResultSet> result(check->executeQuery());
    if (!result->next())
      return ErrorCodes::FriendNotFound;
    std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
        back ? "UPDATE friend SET tags = ?, back = ? WHERE self_id = ? AND friend_id = ?"
             : "UPDATE friend SET tags = ? WHERE self_id = ? AND friend_id = ?"));
    statement->setString(1, tags_json);
    int parameter = 2;
    if (back)
      statement->setString(parameter++, *back);
    statement->setInt(parameter++, self_uid);
    statement->setInt(parameter, friend_uid);
    statement->executeUpdate(); // Zero affected rows also means an unchanged
                                // value was saved.
    transaction.Commit();
    return ErrorCodes::Success;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL contact tags write failed", exception);
    return ErrorCodes::DatabaseError;
  }
}
