#include "SessionService.h"
#include "CSession.h"
#include "ChatGrpcClient.h"
#include "ConfigMgr.h"
#include "FriendService.h"
#include "Logger.h"
#include "RedisMgr.h"
#include "ServicePolicy.h"
#include "StatusGrpcClient.h"
#include "UserMgr.h"
#include "UserService.h"
#include "const.h"

Json::Value SessionService::Login(const std::shared_ptr<CSession> &session, int uid,
                                  const std::string &token)
{
  Json::Value response;
  // One TCP session has one authenticated identity; reconnect to log in again.
  if (session->IsClosed() || session->GetUserId() > 0) {
    response["error"] = ErrorCodes::TokenInvalid;
    return response;
  }

  const auto status_reply = StatusGrpcClient::GetInstance()->Login(uid, token);
  response["error"] = status_reply.error();
  if (status_reply.error() != ErrorCodes::Success) {
    return response;
  }

  auto user = std::make_shared<UserInfo>();
  if (!UserService::GetById(uid, user)) {
    response["error"] = ErrorCodes::UidInvalid;
    return response;
  }
  UserService::WriteProfile(*user, response);
  response["token"] = status_reply.token();
  response["group_chat"] = true;
  response["group_images"] = true;
  response["common_groups"] = true;
  response["voice_call"] = true;
  response["video_call"] = true;
  response["message_dedup"] = true;
  response["request_correlation"] = true;

  std::vector<std::shared_ptr<ApplyInfo>> applications;
  int contacts_error = ErrorCodes::Success;
  if (!FriendService::Applications(uid, applications, &contacts_error)) {
    response["error"] = contacts_error;
    return response;
  }
  for (const auto &application : applications) {
    Json::Value item;
    FriendService::WriteApplication(*application, item);
    response["apply_list"].append(item);
  }

  std::vector<std::shared_ptr<UserInfo>> friends;
  if (!FriendService::List(uid, friends, &contacts_error)) {
    response["error"] = contacts_error;
    return response;
  }
  for (const auto &friend_info : friends) {
    Json::Value item;
    UserService::WriteProfile(*friend_info, item);
    response["friend_list"].append(item);
  }

  const std::string uid_text = std::to_string(uid);
  const std::string lock_key = std::string(LOCK_PREFIX) + uid_text;
  const std::string identifier =
      RedisMgr::GetInstance()->AcquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
  if (identifier.empty()) {
    response["error"] = ErrorCodes::RPCFailed;
    return response;
  }
  Defer release_lock(
      [&identifier, &lock_key]() { RedisMgr::GetInstance()->ReleaseLock(lock_key, identifier); });

  const std::string self_name = ConfigMgr::GetInstance()["SelfServer"]["Name"];
  std::string previous_server;
  const auto previous_route =
      RedisMgr::GetInstance()->Read(std::string(USER_IP_PREFIX) + uid_text, previous_server);
  if (previous_route == RedisReadStatus::Error) {
    response["error"] = ErrorCodes::RedisError;
    return response;
  }
  if (previous_server == self_name) {
    auto old_session = UserMgr::GetInstance()->GetSession(uid);
    if (old_session && old_session->GetSessionId() != session->GetSessionId()) {
      old_session->NotifyOffline(uid);
      // Keep the retiring socket owned by CServer until physically closed.
      UserMgr::GetInstance()->RmvUserSession(uid, old_session->GetSessionId());
    }
  }

  else if (!previous_server.empty() && previous_server != self_name) {
    std::string health;
    const auto previous_health = RedisMgr::GetInstance()->Read(
        std::string(ServicePolicy::NodeHealthPrefix) + previous_server, health, true);
    if (previous_health == RedisReadStatus::Error) {
      response["error"] = ErrorCodes::RedisError;
      return response;
    }
    if (previous_health == RedisReadStatus::Found) {
      message::KickUserReq kick_request;
      kick_request.set_uid(uid);
      const auto kick_response =
          ChatGrpcClient::GetInstance()->NotifyKickUser(previous_server, kick_request);
      if (kick_response.error() != ErrorCodes::Success) {
        response["error"] = ErrorCodes::RPCFailed;
        return response;
      }
    } else {
      LOG_INFO("Taking over expired chat node route: uid=", uid,
               ", previous_node=", previous_server);
    }
  }

  const int registered = RedisMgr::GetInstance()->RegisterSession(uid, session->GetSessionId(),
                                                                  self_name, status_reply.token());
  if (registered != ErrorCodes::Success) {
    response["error"] = registered;
    return response;
  }
  if (!session->BindAuthenticatedUser(uid)) {
    // Cleanup may have finished while the database/RPC work was in flight.
    // Do not leave a closed connection registered as an active session.
    if (!RedisMgr::GetInstance()->ReleaseSession(uid, session->GetSessionId()))
      LOG_WARNING("Unable to revoke interrupted login: uid=", uid);
    response["error"] = ErrorCodes::TokenInvalid;
    return response;
  }
  LOG_INFO("Chat user logged in: uid=", uid, ", session=", session->GetSessionId(), '.');

  return response;
}

Json::Value SessionService::Heartbeat(const std::shared_ptr<CSession> &session,
                                      std::optional<int> snapshotId)
{
  session->UpdateHeartbeat();
  Json::Value response;
  response["error"] = ErrorCodes::Success;
  response["uid"] = session->GetUserId();
  if (snapshotId)
    response["friend_snapshot_id"] = *snapshotId;
  std::vector<int> friend_ids;
  if (FriendService::Ids(session->GetUserId(), friend_ids)) {
    response["friend_ids"] = Json::Value(Json::arrayValue);
    for (const int friend_uid : friend_ids)
      response["friend_ids"].append(friend_uid);
    if (response.toStyledString().size() >= MAX_LENGTH)
      response.removeMember("friend_ids");
  }
  return response;
}
