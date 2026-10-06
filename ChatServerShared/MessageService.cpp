#include "MessageService.h"
#include "CSession.h"
#include "ChatGrpcClient.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "const.h"
#include <algorithm>
#include <cctype>

int MessageService::Store(int actor,
                          std::vector<std::shared_ptr<ChatMessage>> &messages) {
  if (actor <= 0 || messages.empty())
    return ErrorCodes::UidInvalid;
  for (const auto &message : messages) {
    if (!message || message->sender_id != actor || message->recv_id <= 0 ||
        message->thread_id <= 0)
      return ErrorCodes::UidInvalid;
    if (message->msg_type != 0 && message->msg_type != 1)
      return ErrorCodes::Error_Json;
    if (message->msg_type == 1) {
      const auto &id = message->content;
      if (id.size() != 32 ||
          !std::all_of(id.begin(), id.end(),
                       [](unsigned char c) { return std::isxdigit(c) != 0; }))
        return ErrorCodes::Error_Json;
      std::string ready;
      if (!RedisMgr::GetInstance()->Get("resource_ready_" + id, ready))
        return ErrorCodes::RedisError;
      const auto prefix =
          std::to_string(actor) + ":" + std::to_string(message->recv_id) + ":";
      if (ready.rfind(prefix, 0) != 0)
        return ErrorCodes::Error_Json;
      try {
        message->media_size = std::stoull(ready.substr(prefix.size()));
      } catch (...) {
        return ErrorCodes::Error_Json;
      }
    }
  }
  const int friendship = MysqlMgr::GetInstance()->CheckFriendship(
      actor, messages.front()->recv_id);
  if (friendship != ErrorCodes::Success)
    return friendship;
  // DAO also checks membership while holding transactional locks; this check is
  // not the authority alone.
  return MysqlMgr::GetInstance()->AddChatMsg(messages)
             ? ErrorCodes::Success
             : ErrorCodes::DatabaseError;
}

void MessageService::Deliver(
    int from, int to, int thread,
    const std::vector<std::shared_ptr<ChatMessage>> &messages,
    const Json::Value &payload) {
  std::string destination;
  if (!RedisMgr::GetInstance()->Get(
          std::string(USER_IP_PREFIX) + std::to_string(to), destination))
    return;
  if (destination == ConfigMgr::GetInstance()["SelfServer"]["Name"]) {
    if (auto target = UserMgr::GetInstance()->GetSession(to))
      target->Send(payload.toStyledString(), ID_NOTIFY_TEXT_CHAT_MSG_REQ);
    return;
  }
  message::TextChatMsgReq request;
  request.set_fromuid(from);
  request.set_touid(to);
  request.set_thread_id(thread);
  for (const auto &item : messages) {
    auto *value = request.add_textmsgs();
    value->set_msgid(item->unique_id);
    value->set_msgcontent(item->content);
    value->set_message_id(item->message_id);
    value->set_chat_time(item->chat_time);
    value->set_status(item->status);
    value->set_msg_type(item->msg_type);
    value->set_media_size(item->media_size);
  }
  const auto reply = ChatGrpcClient::GetInstance()->NotifyTextChatMsg(
      destination, request, payload);
  if (reply.error() != ErrorCodes::Success)
    LOG_WARNING("Message persisted; live delivery failed; recipient can fetch "
                "history: thread=",
                thread);
}

int MessageService::CreatePrivate(int uid, int other, int &thread) {
  const int friendship = MysqlMgr::GetInstance()->CheckFriendship(uid, other);
  if (friendship != ErrorCodes::Success)
    return friendship;
  return MysqlMgr::GetInstance()->CreatePrivateChat(uid, other, thread)
             ? ErrorCodes::Success
             : ErrorCodes::CreateChatFailed;
}
int MessageService::Load(int uid, int thread, int cursor,
                         std::shared_ptr<PageResult> &page) {
  const int access =
      MysqlMgr::GetInstance()->CheckPrivateChatAccess(uid, thread);
  if (access != ErrorCodes::Success)
    return access;
  page = MysqlMgr::GetInstance()->LoadChatMsg(thread, cursor, 10);
  return page ? ErrorCodes::Success : ErrorCodes::LoadChatFailed;
}
bool MessageService::ListThreads(
    int uid, int cursor, int limit,
    std::vector<std::shared_ptr<ChatThreadInfo>> &threads, bool &more,
    int &next) {
  return uid > 0 && MysqlMgr::GetInstance()->GetUserThreads(
                        uid, cursor, limit, threads, more, next);
}
