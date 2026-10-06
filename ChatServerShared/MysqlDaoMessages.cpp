#include "MysqlDao.h"
#include "MysqlDaoSupport.h"
#include "const.h"
#include <algorithm>
#include <utility>
using MysqlDaoDetail::LogSqlError;
using MysqlDaoDetail::MySqlConnectionGuard;
using MysqlDaoDetail::MySqlTransaction;
using MysqlDaoDetail::ReadUserInfo;

int MysqlDao::CheckPrivateChatAccess(int self_uid, int thread_id)
{
  if (self_uid <= 0 || thread_id <= 0)
    return ErrorCodes::UidInvalid;
  if (!_pool)
    return ErrorCodes::DatabaseError;
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get())
    return ErrorCodes::DatabaseError;
  try {
    std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
        "SELECT 1 FROM private_chat p JOIN friend f ON f.self_id = ? "
        "AND f.friend_id = CASE WHEN p.user1_id = f.self_id THEN "
        "p.user2_id ELSE p.user1_id END "
        "WHERE p.thread_id = ? AND (p.user1_id = f.self_id OR p.user2_id = "
        "f.self_id) LIMIT 1"));
    statement->setInt(1, self_uid);
    statement->setInt(2, thread_id);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    return result->next() ? ErrorCodes::Success : ErrorCodes::FriendNotFound;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL private-chat access query failed", exception);
    return ErrorCodes::DatabaseError;
  }
}

bool MysqlDao::GetUserThreads(std::int64_t user_id, std::int64_t last_id, int page_size,
                              std::vector<std::shared_ptr<ChatThreadInfo>> &threads,
                              bool &load_more, int &next_last_id)
{
  load_more = false;
  next_last_id = last_id;
  threads.clear();
  if (user_id <= 0 || page_size <= 0 || !_pool) {
    return false;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    return false;
  }

  try {
    std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
        "WITH all_threads AS ("
        " SELECT p.thread_id, 'private' AS type, p.user1_id, p.user2_id"
        " FROM private_chat p JOIN friend f ON f.self_id = ?"
        " AND f.friend_id = CASE WHEN p.user1_id = f.self_id THEN "
        "p.user2_id ELSE p.user1_id END"
        " WHERE (p.user1_id = f.self_id OR p.user2_id = f.self_id) AND "
        "p.thread_id > ?"
        " UNION ALL"
        " SELECT thread_id, 'group' AS type, 0 AS user1_id, 0 AS user2_id"
        " FROM group_chat_member WHERE user_id = ? AND thread_id > ?"
        "), page_threads AS ("
        " SELECT thread_id, type, user1_id, user2_id FROM all_threads"
        " ORDER BY thread_id LIMIT ?"
        ") SELECT p.thread_id, p.type, p.user1_id, p.user2_id,"
        " COALESCE(m.message_id, 0) AS last_message_id,"
        " COALESCE(DATE_FORMAT(m.created_at, '%Y-%m-%d %H:%i:%s'), '') AS "
        "last_chat_time"
        " FROM page_threads p LEFT JOIN chat_message m ON m.message_id = ("
        " SELECT MAX(cm.message_id) FROM chat_message cm WHERE "
        "cm.thread_id = p.thread_id)"
        " ORDER BY p.thread_id"));
    statement->setInt64(1, user_id);
    statement->setInt64(2, last_id);
    statement->setInt64(3, user_id);
    statement->setInt64(4, last_id);
    statement->setInt(5, page_size + 1);

    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    while (result->next()) {
      auto thread = std::make_shared<ChatThreadInfo>();
      thread->_thread_id = result->getInt("thread_id");
      thread->_type = result->getString("type");
      thread->_user1_id = result->getInt("user1_id");
      thread->_user2_id = result->getInt("user2_id");
      thread->_last_message_id = result->getInt("last_message_id");
      thread->_last_chat_time = result->getString("last_chat_time");
      threads.push_back(std::move(thread));
    }

    if (static_cast<int>(threads.size()) > page_size) {
      load_more = true;
      threads.pop_back();
    }
    if (!threads.empty()) {
      next_last_id = threads.back()->_thread_id;
    }
    return true;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL chat thread query failed", exception);
    return false;
  }
}

bool MysqlDao::CreatePrivateChat(int user1_uid, int user2_uid, int &thread_id)
{
  thread_id = 0;
  if (user1_uid <= 0 || user2_uid <= 0 || user1_uid == user2_uid || !_pool) {
    return false;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    return false;
  }

  try {
    MySqlTransaction transaction(connection.Get());
    const int first_uid = std::min(user1_uid, user2_uid);
    const int second_uid = std::max(user1_uid, user2_uid);
    std::unique_ptr<sql::PreparedStatement> friendship(
        connection.Get()->prepareStatement("SELECT 1 FROM friend WHERE self_id "
                                           "= ? AND friend_id = ? FOR UPDATE"));
    friendship->setInt(1, user1_uid);
    friendship->setInt(2, user2_uid);
    std::unique_ptr<sql::ResultSet> friendship_result(friendship->executeQuery());
    if (!friendship_result->next())
      return false;
    std::unique_ptr<sql::PreparedStatement> query(
        connection.Get()->prepareStatement("SELECT thread_id FROM private_chat "
                                           "WHERE user1_id = ? AND user2_id = ? FOR UPDATE"));
    query->setInt(1, first_uid);
    query->setInt(2, second_uid);
    std::unique_ptr<sql::ResultSet> result(query->executeQuery());
    if (result->next()) {
      thread_id = result->getInt("thread_id");
      transaction.Commit();
      return true;
    }

    std::unique_ptr<sql::PreparedStatement> create_thread(connection.Get()->prepareStatement(
        "INSERT INTO chat_thread (type, created_at) VALUES ('private', "
        "NOW())"));
    create_thread->executeUpdate();
    std::unique_ptr<sql::Statement> id_statement(connection.Get()->createStatement());
    std::unique_ptr<sql::ResultSet> id_result(
        id_statement->executeQuery("SELECT LAST_INSERT_ID() AS thread_id"));
    if (!id_result->next()) {
      return false;
    }
    thread_id = id_result->getInt("thread_id");

    std::unique_ptr<sql::PreparedStatement> create_private_chat(connection.Get()->prepareStatement(
        "INSERT INTO private_chat "
        "(thread_id, user1_id, user2_id, created_at) VALUES (?, ?, ?, "
        "NOW())"));
    create_private_chat->setInt64(1, thread_id);
    create_private_chat->setInt(2, first_uid);
    create_private_chat->setInt(3, second_uid);
    create_private_chat->executeUpdate();
    transaction.Commit();
    return true;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL private chat creation failed", exception);
    return false;
  }
}

std::shared_ptr<PageResult> MysqlDao::LoadChatMsg(int thread_id, int last_message_id, int page_size)
{
  if (thread_id <= 0 || page_size <= 0 || !_pool) {
    return nullptr;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    return nullptr;
  }

  try {
    auto page = std::make_shared<PageResult>();
    page->next_cursor = last_message_id;
    std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
        "SELECT message_id, thread_id, sender_id, recv_id, unique_id, "
        "content, "
        "created_at, status, msg_type, media_size FROM chat_message "
        "WHERE thread_id = ? AND message_id > ? "
        "ORDER BY message_id ASC LIMIT ?"));
    statement->setInt64(1, thread_id);
    statement->setInt64(2, last_message_id);
    statement->setInt(3, page_size + 1);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
    while (result->next()) {
      ChatMessage message;
      message.message_id = result->getInt("message_id");
      message.thread_id = result->getInt("thread_id");
      message.sender_id = result->getInt("sender_id");
      message.recv_id = result->getInt("recv_id");
      message.unique_id = result->getString("unique_id");
      message.content = result->getString("content");
      message.chat_time = result->getString("created_at");
      message.status = result->getInt("status");
      message.msg_type = result->getInt("msg_type");
      message.media_size = static_cast<std::uint64_t>(result->getInt64("media_size"));
      page->messages.push_back(std::move(message));
    }

    if (static_cast<int>(page->messages.size()) > page_size) {
      page->load_more = true;
      page->messages.pop_back();
    }
    if (!page->messages.empty()) {
      page->next_cursor = page->messages.back().message_id;
    }
    return page;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL chat message query failed", exception);
    return nullptr;
  }
}

bool MysqlDao::AddChatMsg(std::vector<std::shared_ptr<ChatMessage>> &messages)
{
  if (messages.empty() || !_pool) {
    return false;
  }

  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (connection.Get() == nullptr) {
    return false;
  }

  try {
    const auto &first = messages.front();
    if (!first || first->thread_id <= 0 || first->sender_id <= 0 || first->recv_id <= 0 ||
        first->sender_id == first->recv_id) {
      return false;
    }
    for (const auto &message : messages) {
      if (!message || message->unique_id.empty() || message->unique_id.size() > 64 ||
          message->thread_id != first->thread_id || message->sender_id != first->sender_id ||
          message->recv_id != first->recv_id) {
        return false;
      }
    }
    MySqlTransaction transaction(connection.Get());
    std::unique_ptr<sql::PreparedStatement> membership(connection.Get()->prepareStatement(
        "SELECT 1 FROM private_chat p JOIN friend f ON f.self_id = ? AND "
        "f.friend_id = ? "
        "WHERE p.thread_id = ? AND p.user1_id = ? AND p.user2_id = ? FOR "
        "UPDATE"));
    membership->setInt(1, first->sender_id);
    membership->setInt(2, first->recv_id);
    membership->setInt64(3, first->thread_id);
    membership->setInt(4, std::min(first->sender_id, first->recv_id));
    membership->setInt(5, std::max(first->sender_id, first->recv_id));
    std::unique_ptr<sql::ResultSet> membership_result(membership->executeQuery());
    if (!membership_result->next())
      return false;

    std::unique_ptr<sql::PreparedStatement> statement(connection.Get()->prepareStatement(
        "INSERT INTO chat_message "
        "(thread_id, sender_id, recv_id, unique_id, content, created_at, "
        "updated_at, status, msg_type, media_size) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const auto &message : messages) {
      if (!message) {
        continue;
      }
      // The private_chat row is locked above, including across ChatServer
      // nodes. A retry uses the original UUID and returns its existing server
      // ID.
      std::unique_ptr<sql::PreparedStatement> lookup(connection.Get()->prepareStatement(
          "SELECT message_id,recv_id,content,msg_type,created_at,status "
          "FROM chat_message "
          "WHERE thread_id=? AND sender_id=? AND unique_id=? ORDER BY "
          "message_id LIMIT 1 FOR UPDATE"));
      lookup->setInt64(1, message->thread_id);
      lookup->setInt(2, message->sender_id);
      lookup->setString(3, message->unique_id);
      std::unique_ptr<sql::ResultSet> existing(lookup->executeQuery());
      if (existing->next()) {
        const std::string persisted_content = existing->getString("content");
        if (existing->getInt("recv_id") != message->recv_id ||
            persisted_content != message->content ||
            existing->getInt("msg_type") != message->msg_type)
          return false; // A UUID cannot be reused for a different payload.
        message->message_id = existing->getInt("message_id");
        message->chat_time = existing->getString("created_at");
        message->status = existing->getInt("status");
        continue;
      }
      statement->setInt64(1, message->thread_id);
      statement->setInt(2, message->sender_id);
      statement->setInt(3, message->recv_id);
      statement->setString(4, message->unique_id);
      statement->setString(5, message->content);
      statement->setString(6, message->chat_time);
      statement->setString(7, message->chat_time);
      statement->setInt(8, message->status);
      statement->setInt(9, message->msg_type);
      statement->setInt64(10, static_cast<std::int64_t>(message->media_size));
      statement->executeUpdate();

      std::unique_ptr<sql::Statement> id_statement(connection.Get()->createStatement());
      std::unique_ptr<sql::ResultSet> id_result(
          id_statement->executeQuery("SELECT LAST_INSERT_ID() AS message_id"));
      if (id_result->next()) {
        message->message_id = id_result->getInt("message_id");
      }
    }
    transaction.Commit();
    return true;
  } catch (const sql::SQLException &exception) {
    LogSqlError("MySQL chat message write failed", exception);
    return false;
  }
}
