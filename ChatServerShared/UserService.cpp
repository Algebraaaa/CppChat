#include "UserService.h"
#include "ContactTags.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "const.h"
#include <algorithm>

void UserService::WriteProfile(const UserInfo &user, Json::Value &value) {
  value["uid"] = user.uid;
  value["name"] = user.name;
  value["email"] = user.email;
  value["nick"] = user.nick;
  value["desc"] = user.desc;
  value["sex"] = user.sex;
  value["icon"] = user.icon;
  value["back"] = user.back;
  value["tags"] = ParseContactTags(user.tags_json);
}

bool UserService::GetById(int uid, std::shared_ptr<UserInfo> &user) {
  user = uid > 0 ? MysqlMgr::GetInstance()->GetUser(uid) : nullptr;
  if (!user)
    return false;
  // MySQL remains authoritative; never return an outdated profile from another
  // node's cache.
  Json::Value value;
  WriteProfile(*user, value);
  RedisMgr::GetInstance()->Set(std::string(USER_BASE_INFO) +
                                   std::to_string(uid),
                               value.toStyledString());
  return true;
}

std::shared_ptr<UserInfo> UserService::GetByName(const std::string &name) {
  auto user = MysqlMgr::GetInstance()->GetUser(name);
  if (user) {
    Json::Value value;
    WriteProfile(*user, value);
    RedisMgr::GetInstance()->Set(std::string(NAME_INFO) + name,
                                 value.toStyledString());
  }
  return user;
}

ProfileUpdateResult UserService::UpdateProfile(int uid, const std::string &name,
                                               const std::string &nick,
                                               const std::string &description,
                                               const std::string &avatar,
                                               bool has_avatar) {
  if (uid <= 0)
    return {ErrorCodes::TokenInvalid, false};
  if (name.empty() || name.size() > 64 || nick.size() > 64 ||
      description.size() > 255 || (has_avatar && avatar.size() > 8192))
    return {ErrorCodes::Error_Json, false};
  const auto old_user = MysqlMgr::GetInstance()->GetUser(uid);
  if (!old_user)
    return {ErrorCodes::DatabaseError, false};
  if (name != old_user->name &&
      (name.size() < 3 ||
       std::any_of(name.begin(), name.end(),
                   [](unsigned char c) { return c <= 32 || c == 127; })))
    return {ErrorCodes::Error_Json, false};
  auto redis = RedisMgr::GetInstance();
  const std::string base_key =
      std::string(USER_BASE_INFO) + std::to_string(uid);
  const std::string old_key = std::string(NAME_INFO) + old_user->name;
  const std::string new_key = std::string(NAME_INFO) + name;
  const bool pre_base = redis->Del(base_key), pre_old = redis->Del(old_key),
             pre_new = redis->Del(new_key);
  if (!pre_base || !pre_old || !pre_new)
    return {ErrorCodes::RedisError, false};
  const int error = MysqlMgr::GetInstance()->UpdateProfile(
      uid, name, nick, description, avatar, has_avatar);
  if (error != ErrorCodes::Success)
    return {error, false};
  const bool base = redis->Del(base_key), old = redis->Del(old_key),
             current = redis->Del(new_key);
  return {base && old && current ? ErrorCodes::Success : ErrorCodes::RedisError,
          true};
}

bool UserService::GetAvatar(int uid, std::string &avatar) {
  return uid > 0 && MysqlMgr::GetInstance()->GetAvatar(uid, avatar) &&
         avatar.size() <= 8192;
}
