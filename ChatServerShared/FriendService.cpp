#include "FriendService.h"
#include "CSession.h"
#include "ChatGrpcClient.h"
#include "ConfigMgr.h"
#include "ContactTags.h"
#include "Logger.h"
#include "LoginSnapshot.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "UserService.h"
#include "const.h"

int FriendService::Apply(int uid, int other, const std::string &description,
                         const std::string &remark, const Json::Value &input)
{
  if (uid <= 0 || other <= 0 || uid == other)
    return ErrorCodes::UidInvalid;
  Json::Value tags;
  if (!NormalizeContactTags(input, tags))
    return ErrorCodes::Error_Json;
  bool notify_application = false;
  if (!MysqlMgr::GetInstance()->AddFriendApply(uid, other, description, remark,
                                               SerializeContactTags(tags), &notify_application))
    return ErrorCodes::DatabaseError;
  if (!notify_application)
    return ErrorCodes::Success;
  std::string destination;
  if (!RedisMgr::GetInstance()->Get(std::string(USER_IP_PREFIX) + std::to_string(other),
                                    destination))
    return ErrorCodes::Success; // Persisted applications are fetched on next
                                // login.
  std::shared_ptr<UserInfo> applicant;
  UserService::GetById(uid, applicant);
  if (destination == ConfigMgr::GetInstance()["SelfServer"]["Name"]) {
    if (auto target = UserMgr::GetInstance()->GetSession(other)) {
      Json::Value notification;
      if (applicant)
        UserService::WriteProfile(*applicant, notification);
      notification["error"] = ErrorCodes::Success;
      notification["applyuid"] = uid;
      notification["desc"] = description;
      target->Send(notification.toStyledString(), ID_NOTIFY_ADD_FRIEND_REQ);
    }
  } else {
    message::AddFriendReq request;
    request.set_applyuid(uid);
    request.set_touid(other);
    request.set_name(applicant ? applicant->name : std::string());
    request.set_desc(description);
    const auto reply = ChatGrpcClient::GetInstance()->NotifyAddFriend(destination, request);
    if (reply.error() != ErrorCodes::Success)
      LOG_WARNING("Friend application persisted; notification failed: uid=", other);
  }
  return ErrorCodes::Success;
}

bool FriendService::Accept(int uid, int other, const std::string &remark, const Json::Value &input,
                           std::vector<std::shared_ptr<ChatMessage>> &messages)
{
  Json::Value tags;
  return uid > 0 && other > 0 && uid != other && NormalizeContactTags(input, tags) &&
         MysqlMgr::GetInstance()->AddFriend(uid, other, remark, SerializeContactTags(tags),
                                            messages);
}

void FriendService::NotifyAccepted(int from, int to, Json::Value notification)
{
  if (from <= 0 || to <= 0 || from == to)
    return;
  const auto target = UserMgr::GetInstance()->GetSession(to);
  if (!target || MysqlMgr::GetInstance()->CheckFriendship(to, from) != ErrorCodes::Success)
    return;
  notification["error"] = ErrorCodes::Success;
  std::shared_ptr<UserInfo> user;
  if (UserService::GetById(from, user))
    UserService::WriteProfile(*user, notification);
  else
    notification["error"] = ErrorCodes::UidInvalid;
  std::string back, tags;
  if (MysqlMgr::GetInstance()->GetFriendMetadata(to, from, back, tags) == ErrorCodes::Success) {
    notification["back"] = back;
    notification["tags"] = ParseContactTags(tags);
  } else {
    notification.removeMember("back");
    notification.removeMember("tags");
  }
  notification["fromuid"] = from;
  notification["touid"] = to;
  target->Send(notification.toStyledString(), ID_NOTIFY_AUTH_FRIEND_REQ);
}

void FriendService::NotifyAcceptance(int from, int to, const Json::Value &payload)
{
  std::string destination;
  if (!RedisMgr::GetInstance()->Get(std::string(USER_IP_PREFIX) + std::to_string(to), destination))
    return;
  if (destination == ConfigMgr::GetInstance()["SelfServer"]["Name"])
    NotifyAccepted(from, to, payload);
  else {
    message::AuthFriendReq request;
    request.set_fromuid(from);
    request.set_touid(to);
    const auto reply = ChatGrpcClient::GetInstance()->NotifyAuthFriend(destination, request);
    if (reply.error() != ErrorCodes::Success)
      LOG_WARNING("Friend acceptance persisted; notification failed: uid=", to);
  }
}

void FriendService::NotifyDeleted(int uid, int other)
{
  const auto target = UserMgr::GetInstance()->GetSession(uid);
  if (!target || MysqlMgr::GetInstance()->CheckFriendship(uid, other) != ErrorCodes::FriendNotFound)
    return; // Suppress an old deletion after the friendship has been re-added.
  Json::Value notification;
  notification["error"] = ErrorCodes::Success;
  notification["uid"] = uid;
  notification["friend_uid"] = other;
  target->Send(notification.toStyledString(), ID_NOTIFY_DELETE_FRIEND);
}

int FriendService::Delete(int uid, int other)
{
  if (uid <= 0 || other <= 0 || uid == other)
    return ErrorCodes::UidInvalid;
  if (!MysqlMgr::GetInstance()->DeleteFriend(uid, other))
    return ErrorCodes::DatabaseError;
  LOG_INFO("Friend relationship deleted: uid=", uid, ", friend_uid=", other);
  if (UserMgr::GetInstance()->GetSession(other))
    NotifyDeleted(other, uid);
  else {
    std::string destination;
    if (RedisMgr::GetInstance()->Get(std::string(USER_IP_PREFIX) + std::to_string(other),
                                     destination)) {
      message::DeleteFriendReq request;
      request.set_fromuid(uid);
      request.set_touid(other);
      const auto reply = ChatGrpcClient::GetInstance()->NotifyDeleteFriend(destination, request);
      if (reply.error() != ErrorCodes::Success)
        LOG_WARNING("Friend deletion persisted; notification failed; heartbeat "
                    "will reconcile: uid=",
                    other);
    }
  }
  return ErrorCodes::Success;
}

int FriendService::SetTags(int uid, int other, const Json::Value &input, Json::Value &tags,
                           const Json::Value &remark)
{
  if (uid <= 0 || other <= 0 || uid == other)
    return ErrorCodes::UidInvalid;
  if (!NormalizeContactTags(input, tags) || (!remark.isNull() && !ValidContactRemark(remark)))
    return ErrorCodes::Error_Json;
  const auto back = remark.isNull() ? std::string() : remark.asString();
  return MysqlMgr::GetInstance()->SetContactTags(uid, other, SerializeContactTags(tags),
                                                 remark.isNull() ? nullptr : &back);
}
bool FriendService::List(int uid, std::vector<std::shared_ptr<UserInfo>> &friends, int *error)
{
  friends.clear();
  if (error)
    *error = ErrorCodes::DatabaseError;
  if (uid <= 0)
    return false;
  constexpr int page_size = 100;
  int cursor = 0;
  std::size_t bytes = 0;
  for (;;) {
    std::vector<std::shared_ptr<UserInfo>> page;
    if (!MysqlMgr::GetInstance()->GetFriendPage(uid, page, cursor, page_size)) {
      friends.clear();
      return false;
    }
    for (const auto &user : page) {
      Json::Value item;
      UserService::WriteProfile(*user, item);
      bytes += item.toStyledString().size();
      if (bytes > LoginSnapshot::MaxBytes) {
        friends.clear();
        if (error)
          *error = ErrorCodes::InternalError;
        return false;
      }
    }
    if (page.empty()) {
      if (error)
        *error = ErrorCodes::Success;
      return true;
    }
    const int next_cursor = page.back()->uid;
    if (next_cursor <= cursor) {
      friends.clear();
      return false;
    }
    friends.insert(friends.end(), page.begin(), page.end());
    cursor = next_cursor;
    if (page.size() < page_size) {
      if (error)
        *error = ErrorCodes::Success;
      return true;
    }
  }
}
bool FriendService::Ids(int uid, std::vector<int> &friend_ids)
{
  return uid > 0 && MysqlMgr::GetInstance()->GetFriendIds(uid, friend_ids);
}
void FriendService::WriteApplication(const ApplyInfo &application, Json::Value &item)
{
  item["uid"] = application._uid;
  item["name"] = application._name;
  item["desc"] = application._desc;
  item["icon"] = application._icon;
  item["nick"] = application._nick;
  item["sex"] = application._sex;
  item["status"] = application._status;
}
bool FriendService::Applications(int uid, std::vector<std::shared_ptr<ApplyInfo>> &applications,
                                 int *error)
{
  applications.clear();
  if (error)
    *error = ErrorCodes::DatabaseError;
  if (uid <= 0)
    return false;
  // Iterate the existing keyset pages, rather than truncating login at ten rows.
  constexpr int page_size = 100;
  int cursor = 0;
  std::size_t bytes = 0;
  for (;;) {
    std::vector<std::shared_ptr<ApplyInfo>> page;
    if (!MysqlMgr::GetInstance()->GetApplyList(uid, page, cursor, page_size)) {
      applications.clear();
      return false;
    }
    for (const auto &application : page) {
      Json::Value item;
      WriteApplication(*application, item);
      bytes += item.toStyledString().size();
      if (bytes > LoginSnapshot::MaxBytes) {
        applications.clear();
        if (error)
          *error = ErrorCodes::InternalError;
        return false;
      }
    }
    if (page.empty()) {
      if (error)
        *error = ErrorCodes::Success;
      return true;
    }
    const int next_cursor = page.back()->_id;
    if (next_cursor <= cursor) {
      applications.clear();
      return false;
    }
    applications.insert(applications.end(), page.begin(), page.end());
    cursor = next_cursor;
    if (page.size() < page_size) {
      if (error)
        *error = ErrorCodes::Success;
      return true;
    }
  }
}
