#include "MysqlDao.h"
#include "MysqlDaoSupport.h"
#include "const.h"
#include <algorithm>
#include <utility>
using MysqlDaoDetail::LogSqlError;
using MysqlDaoDetail::MySqlConnectionGuard;
using MysqlDaoDetail::MySqlTransaction;
using MysqlDaoDetail::ReadUserInfo;

bool MysqlDao::AddFriendApply(int from_uid, int to_uid, const std::string &description,
                              const std::string &remark, const std::string &tags_json)
{
  if (from_uid <= 0 || to_uid <= 0 || from_uid == to_uid || !_pool) {
    return false;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    return false;
  }

  try {
    std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
        "INSERT INTO friend_apply (from_uid, to_uid, descs, back_name, "
        "tags) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON DUPLICATE KEY UPDATE descs = VALUES(descs), "
        "back_name = VALUES(back_name), tags = VALUES(tags), status = 0"));
    statement->setInt(1, from_uid);
    statement->setInt(2, to_uid);
    statement->setString(3, description);
    statement->setString(4, remark);
    statement->setString(5, tags_json);
    return statement->executeUpdate() >= 0;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL friend application write failed", exception);
    return false;
  }
}

bool MysqlDao::AuthFriendApply(int from_uid, int to_uid)
{
  if (from_uid <= 0 || to_uid <= 0 || !_pool) {
    return false;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    return false;
  }

  try {
    std::unique_ptr<sql::PreparedStatement> statement(
        connection.Get()->prepareStatement("UPDATE friend_apply SET status = 1 "
                                           "WHERE from_uid = ? AND to_uid = ?"));
    statement->setInt(1, to_uid);
    statement->setInt(2, from_uid);
    return statement->executeUpdate() == 1;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL friend application authorization failed", exception);
    return false;
  }
}

bool MysqlDao::AddFriend(int from_uid, int to_uid, const std::string &remark,
                         const std::string &tags_json,
                         std::vector<std::shared_ptr<ChatMessage>> &initial_messages)
{
  if (from_uid <= 0 || to_uid <= 0 || from_uid == to_uid || !_pool) {
    return false;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    return false;
  }

  try {
    MySqlTransaction transaction(connection.Get());
    std::string reverse_remark;
    std::string reverse_tags = "[]";
    std::string application_description;

    {
      std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
          "SELECT back_name, descs, COALESCE(tags, '[]') AS tags FROM "
          "friend_apply "
          "WHERE from_uid = ? AND to_uid = ? FOR UPDATE"));
      statement->setInt(1, to_uid);
      statement->setInt(2, from_uid);
      std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
      if (!result->next()) {
        return false;
      }
      reverse_remark = result->getString("back_name");
      reverse_tags = result->getString("tags");
      application_description = result->getString("descs");
    }

    {
      std::unique_ptr<sql::PreparedStatement> statement(
          connection.Get()->prepareStatement("UPDATE friend_apply SET status = 1 "
                                             "WHERE from_uid = ? AND to_uid = ?"));
      statement->setInt(1, to_uid);
      statement->setInt(2, from_uid);
      if (statement->executeUpdate() != 1) {
        return false;
      }
    }

    auto add_friend_row = [&](int self_uid, int friend_uid, const std::string &back_name,
                              const std::string &own_tags) {
      std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
          "INSERT INTO friend (self_id, friend_id, back, tags) VALUES (?, "
          "?, ?, ?) "
          "ON DUPLICATE KEY UPDATE back = VALUES(back), tags = "
          "VALUES(tags)"));
      statement->setInt(1, self_uid);
      statement->setInt(2, friend_uid);
      statement->setString(3, back_name);
      statement->setString(4, own_tags);
      statement->executeUpdate();
    };
    add_friend_row(from_uid, to_uid, remark, tags_json);
    add_friend_row(to_uid, from_uid, reverse_remark, reverse_tags);

    const int first_uid = std::min(from_uid, to_uid);
    const int second_uid = std::max(from_uid, to_uid);
    int thread_id = 0;
    {
      std::unique_ptr<sql::PreparedStatement> statement(
          connection.Get()->prepareStatement("SELECT thread_id FROM private_chat "
                                             "WHERE user1_id = ? AND user2_id = ? FOR UPDATE"));
      statement->setInt(1, first_uid);
      statement->setInt(2, second_uid);
      std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
      if (result->next()) {
        thread_id = result->getInt("thread_id");
      }
    }

    if (thread_id == 0) {
      std::unique_ptr<sql::PreparedStatement> thread_statement(connection.Get()->prepareStatement(
          "INSERT INTO chat_thread (type, created_at) VALUES ('private', "
          "NOW())"));
      thread_statement->executeUpdate();
      std::unique_ptr<sql::Statement> id_statement(connection.Get()->createStatement());
      std::unique_ptr<sql::ResultSet> id_result(
          id_statement->executeQuery("SELECT LAST_INSERT_ID() AS thread_id"));
      if (!id_result->next()) {
        return false;
      }
      thread_id = id_result->getInt("thread_id");

      std::unique_ptr<sql::PreparedStatement> private_statement(
          connection.Get()->prepareStatement("INSERT INTO private_chat "
                                             "(thread_id, user1_id, user2_id, created_at) "
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
      std::unique_ptr<sql::PreparedStatement> statement(
          connection.Get()->prepareStatement("INSERT INTO chat_message "
                                             "(thread_id, sender_id, recv_id, content, created_at, "
                                             "updated_at, status) "
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
        initial_messages.push_back(std::move(message));
      }
    };

    add_initial_message(to_uid, from_uid, application_description);
    add_initial_message(from_uid, to_uid, "We are friends now!");
    transaction.Commit();
    return true;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL friend creation failed", exception);
    return false;
  }
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
        "SELECT a.from_uid, a.descs, a.status, u.username AS name, "
        "u.nick, u.sex, u.icon "
        "FROM friend_apply AS a JOIN users AS u ON a.from_uid = u.id "
        "WHERE a.to_uid = ? AND a.id > ? ORDER BY a.id ASC LIMIT ?"));
    statement->setInt(1, to_uid);
    statement->setInt(2, begin_id);
    statement->setInt(3, limit);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    while (result->next()) {
      applications.push_back(std::make_shared<ApplyInfo>(
          result->getInt("from_uid"), result->getString("name"), result->getString("descs"),
          result->getString("icon"), result->getString("nick"), result->getInt("sex"),
          result->getInt("status")));
    }
    return true;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL friend application query failed", exception);
    return false;
  }
}

bool MysqlDao::GetFriendList(int self_uid, std::vector<std::shared_ptr<UserInfo>> &friends)
{
  if (self_uid <= 0 || !_pool) {
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
        "JOIN users AS u ON f.friend_id = u.id WHERE f.self_id = ?"));
    statement->setInt(1, self_uid);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    while (result->next()) {
      auto user = ReadUserInfo(*result);
      user->back = result->getString("back");
      user->tags_json = result->getString("tags");
      friends.push_back(std::move(user));
    }
    return true;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL friend list query failed", exception);
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
  try {
    MySqlTransaction transaction(connection.Get());
    // Lock/remove applications before friendship rows, matching AddFriend's
    // lock order.
    std::unique_ptr<sql::PreparedStatement> applications(connection.Get()->prepareStatement(
        "DELETE FROM friend_apply WHERE (from_uid = ? AND to_uid = ?) "
        "OR (from_uid = ? AND to_uid = ?)"));
    applications->setInt(1, self_uid);
    applications->setInt(2, friend_uid);
    applications->setInt(3, friend_uid);
    applications->setInt(4, self_uid);
    applications->executeUpdate();
    std::unique_ptr<sql::PreparedStatement> friends(connection.Get()->prepareStatement(
        "DELETE FROM friend WHERE (self_id = ? AND friend_id = ?) "
        "OR (self_id = ? AND friend_id = ?)"));
    friends->setInt(1, self_uid);
    friends->setInt(2, friend_uid);
    friends->setInt(3, friend_uid);
    friends->setInt(4, self_uid);
    friends->executeUpdate();
    // Keep private_chat/chat_message so re-adding can recover the same
    // conversation.
    transaction.Commit();
    return true;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL friend deletion failed", exception);
    return false;
  }
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

int MysqlDao::SetContactTags(int self_uid, int friend_uid, const std::string &tags_json)
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
        "UPDATE friend SET tags = ? WHERE self_id = ? AND friend_id = ?"));
    statement->setString(1, tags_json);
    statement->setInt(2, self_uid);
    statement->setInt(3, friend_uid);
    statement->executeUpdate(); // Zero affected rows also means an unchanged
                                // value was saved.
    transaction.Commit();
    return ErrorCodes::Success;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL contact tags write failed", exception);
    return ErrorCodes::DatabaseError;
  }
}
