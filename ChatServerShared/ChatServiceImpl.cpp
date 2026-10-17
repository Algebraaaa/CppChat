#include "ChatServiceImpl.h"
#include "CSession.h"
#include "FriendService.h"
#include "Logger.h"
#include "MysqlMgr.h"
#include "UserMgr.h"
#include "const.h"
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>
ChatServiceImpl::ChatServiceImpl() {}

Status ChatServiceImpl::NotifyAddFriend(ServerContext *context, const AddFriendReq *request,
                                        AddFriendRsp *reply)
{
  // 查找用户是否在本服务器
  auto touid = request->touid();
  auto session = UserMgr::GetInstance()->GetSession(touid);

  Defer defer([request, reply]() {
    reply->set_error(ErrorCodes::Success);
    reply->set_applyuid(request->applyuid());
    reply->set_touid(request->touid());
  });

  // 用户不在内存中则直接返回
  if (session == nullptr) {
    return Status::OK;
  }

  // 在内存中则直接发送通知对方
  Json::Value rtvalue;
  rtvalue["error"] = ErrorCodes::Success;
  rtvalue["applyuid"] = request->applyuid();
  rtvalue["name"] = request->name();
  rtvalue["desc"] = request->desc();

  std::string return_str = rtvalue.toStyledString();

  session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ);
  return Status::OK;
}

Status ChatServiceImpl::NotifyAuthFriend(ServerContext *, const AuthFriendReq *request,
                                         AuthFriendRsp *reply)
{
  const int from = request->fromuid(), to = request->touid();
  reply->set_fromuid(from);
  reply->set_touid(to);
  reply->set_error(ErrorCodes::Success);
  if (from <= 0 || to <= 0 || from == to) {
    reply->set_error(ErrorCodes::UidInvalid);
    return Status::OK;
  }
  Json::Value notification;
  notification["error"] = ErrorCodes::Success;
  FriendService::NotifyAccepted(from, to, notification);
  return Status::OK;
}

Status ChatServiceImpl::NotifyTextChatMsg(::grpc::ServerContext *context,
                                          const TextChatMsgReq *request, TextChatMsgRsp *reply)
{
  auto touid = request->touid();
  auto session = UserMgr::GetInstance()->GetSession(touid);
  reply->set_error(ErrorCodes::Success);

  if (session == nullptr) {
    return Status::OK;
  }

  Json::Value rtvalue;
  rtvalue["error"] = ErrorCodes::Success;
  rtvalue["fromuid"] = request->fromuid();
  rtvalue["touid"] = request->touid();
  rtvalue["thread_id"] = request->thread_id();

  Json::Value chat_datas;
  for (const auto &msg : request->textmsgs()) {
    Json::Value element;
    element["message_id"] = msg.message_id();
    element["thread_id"] = request->thread_id();
    element["sender"] = request->fromuid();
    element["recv_id"] = request->touid();
    element["unique_id"] = msg.msgid();
    element["content"] = msg.msgcontent();
    element["msg_content"] = msg.msgcontent();
    element["chat_time"] = msg.chat_time();
    element["status"] = msg.status();
    element["msg_type"] = msg.msg_type();
    element["media_size"] = Json::UInt64(msg.media_size());
    chat_datas.append(element);
  }
  rtvalue["chat_datas"] = chat_datas;
  Json::Value group;
  if (MysqlMgr::GetInstance()->GetGroup(touid, request->thread_id(), group) ==
      ErrorCodes::Success) {
    if (const auto sender = MysqlMgr::GetInstance()->GetUser(request->fromuid()))
      group["member_names"][std::to_string(request->fromuid())] = sender->name;
    rtvalue["group"] = group;
    rtvalue["touid"] = 0;
  } else {
    // Do not deliver a group message to a member removed while the RPC was in flight.
    if (MysqlMgr::GetInstance()->CheckPrivateChatAccess(touid, request->thread_id()) !=
        ErrorCodes::Success)
      return Status::OK;
  }

  std::string return_str = rtvalue.toStyledString();
  session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
  return Status::OK;
}

Status ChatServiceImpl::NotifyKickUser(::grpc::ServerContext *, const KickUserReq *request,
                                       KickUserRsp *response)
{
  response->set_error(ErrorCodes::Success);
  const int uid = request->uid();
  if (uid <= 0) {
    response->set_error(ErrorCodes::UidInvalid);
    return Status::OK;
  }
  const auto session = UserMgr::GetInstance()->GetSession(uid);
  if (!session) {
    return Status::OK;
  }
  session->NotifyOffline(uid);
  return Status::OK;
}
Status ChatServiceImpl::NotifyDeleteFriend(::grpc::ServerContext *,
                                           const message::DeleteFriendReq *request,
                                           message::DeleteFriendRsp *response)
{
  const int uid = request->touid(), other = request->fromuid();
  response->set_error(ErrorCodes::Success);
  if (uid <= 0 || other <= 0 || uid == other) {
    response->set_error(ErrorCodes::UidInvalid);
    return Status::OK;
  }
  FriendService::NotifyDeleted(uid, other);
  return Status::OK;
}
