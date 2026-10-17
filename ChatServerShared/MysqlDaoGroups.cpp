#include "MysqlDao.h"
#include "MysqlDaoSupport.h"
#include "const.h"
#include <algorithm>
#include <json/writer.h>
#include <set>

using namespace MysqlDaoDetail;
namespace {
using Statement = std::unique_ptr<sql::PreparedStatement>;
using Rows = std::unique_ptr<sql::ResultSet>;

Json::Value ReadGroup(sql::Connection *db, int thread, bool members)
{
  Statement query(db->prepareStatement(
      "SELECT thread_id,name,owner_uid FROM group_chat WHERE thread_id=? AND active=1"));
  query->setInt(1, thread);
  Rows rows(query->executeQuery());
  if (!rows->next())
    return Json::Value();
  Json::Value group;
  group["thread_id"] = thread;
  group["name"] = std::string(rows->getString("name"));
  group["owner_uid"] = rows->getInt("owner_uid");
  if (members) {
    group["members"] = Json::Value(Json::arrayValue);
    Statement users(db->prepareStatement("SELECT u.id AS uid,u.username AS name FROM "
                                         "group_chat_member m JOIN users u ON u.id=m.user_id "
                                         "WHERE m.thread_id=? ORDER BY u.id"));
    users->setInt(1, thread);
    Rows result(users->executeQuery());
    while (result->next()) {
      Json::Value member;
      member["uid"] = result->getInt("uid");
      member["name"] = std::string(result->getString("name"));
      group["members"].append(member);
    }
  }
  return group;
}
bool IsMember(sql::Connection *db, int uid, int thread)
{
  Statement query(
      db->prepareStatement("SELECT 1 FROM group_chat_member WHERE thread_id=? AND user_id=?"));
  query->setInt(1, thread);
  query->setInt(2, uid);
  Rows rows(query->executeQuery());
  return rows->next();
}
bool Friend(sql::Connection *db, int uid, int other)
{
  Statement query(
      db->prepareStatement("SELECT 1 FROM friend WHERE self_id=? AND friend_id=? FOR UPDATE"));
  query->setInt(1, uid);
  query->setInt(2, other);
  Rows rows(query->executeQuery());
  return rows->next();
}
bool NameValid(const Json::Value &value)
{
  if (!value.isString())
    return false;
  const auto text = value.asString();
  return !text.empty() && text.size() <= 64 &&
         std::any_of(text.begin(), text.end(), [](unsigned char c) { return c > 32; }) &&
         std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
int CreateGroup(sql::Connection *db, int uid, const Json::Value &request, int &thread)
{
  if (!NameValid(request["name"]) || !request["members"].isArray() || request["members"].empty() ||
      request["members"].size() > 31 || !request["creation_key"].isString())
    return ErrorCodes::Error_Json;
  const auto key = request["creation_key"].asString();
  if (key.empty() || key.size() > 64 || !std::all_of(key.begin(), key.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '-';
      }))
    return ErrorCodes::Error_Json;
  std::set<int> ids{uid};
  for (const auto &member : request["members"]) {
    if (!member.isInt() || member.asInt() <= 0)
      return ErrorCodes::Error_Json;
    ids.insert(member.asInt());
  }
  if (ids.size() < 2 || ids.size() > 32)
    return ErrorCodes::Error_Json;
  // Serialize retries from different nodes, without process-local locks.
  Statement actor(db->prepareStatement("SELECT id FROM users WHERE id=? FOR UPDATE"));
  actor->setInt(1, uid);
  Rows actorRow(actor->executeQuery());
  if (!actorRow->next())
    return ErrorCodes::UidInvalid;
  Json::Value spec;
  spec["name"] = request["name"];
  for (int id : ids)
    spec["members"].append(id);
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  const auto specification = Json::writeString(writer, spec);
  Statement existing(db->prepareStatement(
      "SELECT thread_id,creation_spec,active FROM group_chat WHERE creator_uid=? "
      "AND creation_key=? FOR UPDATE"));
  existing->setInt(1, uid);
  existing->setString(2, key);
  Rows previous(existing->executeQuery());
  if (previous->next()) {
    if (std::string(previous->getString("creation_spec")) != specification)
      return ErrorCodes::Error_Json;
    thread = previous->getInt("thread_id");
    if (!previous->getBoolean("active") || !IsMember(db, uid, thread))
      return ErrorCodes::GroupForbidden;
  } else {
    for (int id : ids)
      if (id != uid && !Friend(db, uid, id))
        return ErrorCodes::FriendNotFound;
    Statement insert(db->prepareStatement("INSERT INTO chat_thread(type) VALUES ('group')"));
    insert->executeUpdate();
    std::unique_ptr<sql::Statement> last(db->createStatement());
    Rows id(last->executeQuery("SELECT LAST_INSERT_ID() AS id"));
    if (!id->next())
      return ErrorCodes::DatabaseError;
    thread = id->getInt("id");
    Statement create(db->prepareStatement(
        "INSERT INTO group_chat(thread_id,name,owner_uid,creator_uid,creation_key,creation_spec) "
        "VALUES (?,?,?,?,?,?)"));
    create->setInt(1, thread);
    create->setString(2, request["name"].asString());
    create->setInt(3, uid);
    create->setInt(4, uid);
    create->setString(5, key);
    create->setString(6, specification);
    create->executeUpdate();
    Statement join(
        db->prepareStatement("INSERT INTO group_chat_member(thread_id,user_id) VALUES (?,?)"));
    for (int member : ids) {
      join->setInt(1, thread);
      join->setInt(2, member);
      join->executeUpdate();
    }
  }
  return ErrorCodes::Success;
}

int UpdateGroup(sql::Connection *db, int uid, const std::string &op, const Json::Value &request,
                int &thread)
{
  if (!request["thread_id"].isInt() || request["thread_id"].asInt() <= 0)
    return ErrorCodes::Error_Json;
  thread = request["thread_id"].asInt();
  Statement lock(db->prepareStatement(
      "SELECT owner_uid FROM group_chat WHERE thread_id=? AND active=1 FOR UPDATE"));
  lock->setInt(1, thread);
  Rows locked(lock->executeQuery());
  if (!locked->next() || !IsMember(db, uid, thread))
    return ErrorCodes::GroupForbidden;
  const int owner = locked->getInt("owner_uid");
  if (op != "leave" && uid != owner)
    return ErrorCodes::GroupForbidden;
  if (op == "leave" && uid == owner)
    return ErrorCodes::GroupOwnerMustTransfer;
  if (op == "rename") {
    if (!NameValid(request["name"]))
      return ErrorCodes::Error_Json;
    Statement update(db->prepareStatement("UPDATE group_chat SET name=? WHERE thread_id=?"));
    update->setString(1, request["name"].asString());
    update->setInt(2, thread);
    update->executeUpdate();
  } else if (op == "disband") {
    Statement update(db->prepareStatement("UPDATE group_chat SET active=0 WHERE thread_id=?"));
    update->setInt(1, thread);
    update->executeUpdate();
    Statement remove(db->prepareStatement("DELETE FROM group_chat_member WHERE thread_id=?"));
    remove->setInt(1, thread);
    remove->executeUpdate();
  } else {
    const int target =
        op == "leave" ? uid : (request["member_uid"].isInt() ? request["member_uid"].asInt() : 0);
    if (target <= 0 || target == owner)
      return ErrorCodes::Error_Json;
    if (op == "add") {
      if (!Friend(db, uid, target))
        return ErrorCodes::FriendNotFound;
      if (!IsMember(db, target, thread)) {
        Statement count(
            db->prepareStatement("SELECT COUNT(*) AS n FROM group_chat_member WHERE thread_id=?"));
        count->setInt(1, thread);
        Rows number(count->executeQuery());
        if (!number->next() || number->getInt("n") >= 32)
          return ErrorCodes::GroupFull;
        Statement join(
            db->prepareStatement("INSERT INTO group_chat_member(thread_id,user_id) VALUES (?,?)"));
        join->setInt(1, thread);
        join->setInt(2, target);
        join->executeUpdate();
      }
    } else if (op == "transfer") {
      if (!IsMember(db, target, thread))
        return ErrorCodes::GroupForbidden;
      Statement update(db->prepareStatement("UPDATE group_chat SET owner_uid=? WHERE thread_id=?"));
      update->setInt(1, target);
      update->setInt(2, thread);
      update->executeUpdate();
    } else {
      Statement remove(
          db->prepareStatement("DELETE FROM group_chat_member WHERE thread_id=? AND user_id=?"));
      remove->setInt(1, thread);
      remove->setInt(2, target);
      remove->executeUpdate();
    }
  }
  return ErrorCodes::Success;
}
} // namespace

int MysqlDao::GetGroup(int uid, int thread, Json::Value &group, bool members)
{
  if (uid <= 0 || thread <= 0)
    return ErrorCodes::UidInvalid;
  if (!_pool)
    return ErrorCodes::DatabaseError;
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get())
    return ErrorCodes::DatabaseError;
  try {
    // A shared row lock makes metadata/history authorization serialize with
    // removals. Sending takes the same row exclusively in AddChatMsg.
    MySqlTransaction transaction(connection.Get());
    Statement lock(connection.Get()->prepareStatement(
        "SELECT thread_id FROM group_chat WHERE thread_id=? AND active=1 FOR SHARE"));
    lock->setInt(1, thread);
    Rows rows(lock->executeQuery());
    if (!rows->next() || !IsMember(connection.Get(), uid, thread))
      return ErrorCodes::GroupForbidden;
    group = ReadGroup(connection.Get(), thread, members);
    transaction.Commit();
    return ErrorCodes::Success;
  } catch (const sql::SQLException &error) {
    LogSqlError("MySQL group read failed", error);
    return ErrorCodes::DatabaseError;
  }
}

int MysqlDao::ChangeGroup(int uid, const Json::Value &request, Json::Value &group)
{
  if (uid <= 0)
    return ErrorCodes::TokenInvalid;
  if (!request["op"].isString())
    return ErrorCodes::Error_Json;
  const auto op = request["op"].asString();
  if (op != "create" && op != "info" && op != "rename" && op != "add" && op != "remove" &&
      op != "leave" && op != "transfer" && op != "disband")
    return ErrorCodes::Error_Json;
  if (op == "info")
    return request["thread_id"].isInt() ? GetGroup(uid, request["thread_id"].asInt(), group, true)
                                        : ErrorCodes::Error_Json;
  if (!_pool)
    return ErrorCodes::DatabaseError;
  MySqlConnectionGuard connection(_pool.get(), _pool->GetConnection());
  if (!connection.Get())
    return ErrorCodes::DatabaseError;
  auto *db = connection.Get();
  try {
    MySqlTransaction transaction(db);
    int thread = 0;
    const int result = op == "create" ? CreateGroup(db, uid, request, thread)
                                      : UpdateGroup(db, uid, op, request, thread);
    if (result != ErrorCodes::Success)
      return result;
    if (op == "leave" || op == "disband") {
      group["thread_id"] = thread;
      group["removed"] = true;
    } else
      group = ReadGroup(db, thread, true);
    transaction.Commit();
    return ErrorCodes::Success;
  } catch (const sql::SQLException &error) {
    LogSqlError("MySQL group change failed", error);
    return ErrorCodes::DatabaseError;
  }
}
