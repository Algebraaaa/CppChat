#include "LogicSystem.h"
#include "ContactTags.h"
#include "LoginSnapshot.h"
#include "RequestContract.h"
#include "SessionService.h"

#include <algorithm>
#include <cctype>
#include <openssl/evp.h>
#include <utility>

#include <json/reader.h>

#include "CServer.h"
#include "CSession.h"
#include "ChatGrpcClient.h"
#include "ConfigMgr.h"
#include "FriendService.h"
#include "Logger.h"
#include "MessageService.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "ServicePolicy.h"
#include "StatusGrpcClient.h"
#include "UserMgr.h"
#include "UserService.h"
#include "VoiceCallService.h"
#include "const.h"
#include "utils.h"

namespace {
using RequestContract::ParseRequest;

void FillUserJson(const UserInfo &user, Json::Value &value)
{
  UserService::WriteProfile(user, value);
}

void FillChatJson(const ChatMessage &message, Json::Value &value, bool voiceMessages = true)
{
  value["message_id"] = message.message_id;
  value["thread_id"] = message.thread_id;
  value["sender"] = message.sender_id;
  value["recv_id"] = message.recv_id;
  value["unique_id"] = message.unique_id;
  value["content"] = message.content;
  value["msg_content"] = message.content;
  value["chat_time"] = message.chat_time;
  value["status"] = message.status;
  value["msg_type"] = message.msg_type;
  value["media_size"] = Json::UInt64(message.media_size);
  MessageService::PresentMessage(value, voiceMessages);
}

bool DecodeAvatar(const std::string &encoded, std::string &decoded)
{
  if (encoded.empty() || encoded.size() > 12000 || encoded.size() % 4 != 0)
    return false;
  decoded.resize(encoded.size() / 4 * 3);
  const int size = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(&decoded[0]),
                                   reinterpret_cast<const unsigned char *>(encoded.data()),
                                   static_cast<int>(encoded.size()));
  if (size < 0)
    return false;
  int padding = 0;
  if (encoded.back() == '=')
    ++padding;
  if (encoded[encoded.size() - 2] == '=')
    ++padding;
  decoded.resize(static_cast<std::size_t>(size - padding));
  return decoded.size() <= 8192 && decoded.size() >= 4 &&
         static_cast<unsigned char>(decoded[0]) == 0xff &&
         static_cast<unsigned char>(decoded[1]) == 0xd8 &&
         static_cast<unsigned char>(decoded[decoded.size() - 2]) == 0xff &&
         static_cast<unsigned char>(decoded.back()) == 0xd9;
}

std::string EncodeAvatar(const std::string &data)
{
  std::string encoded((data.size() + 2) / 3 * 4 + 1, '\0');
  const int size = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(&encoded[0]),
                                   reinterpret_cast<const unsigned char *>(data.data()),
                                   static_cast<int>(data.size()));
  encoded.resize(static_cast<std::size_t>(size));
  return encoded;
}
} // namespace

LogicSystem::LogicSystem()
{
  RegisterCallbacks();
  std::size_t workers = 4;
  const auto configured = ConfigMgr::GetInstance()["Business"].GetValue("Workers", "4");
  try {
    std::size_t consumed = 0;
    const int value = std::stoi(configured, &consumed);
    if (consumed != configured.size() || value < 1 || value > 16)
      throw std::invalid_argument("Business.Workers must be 1-16");
    workers = static_cast<std::size_t>(value);
  } catch (const std::exception &) {
    LOG_WARNING("Invalid Business.Workers; using 4 workers.");
  }
  executor_ = std::make_unique<BusinessExecutor>(workers, MAX_RECVQUE);
  LOG_INFO("Business executor started: workers=", workers, ", queue_limit=", MAX_RECVQUE);
}

LogicSystem::~LogicSystem()
{
  Stop();
  server_.reset();
}

void LogicSystem::Stop() { executor_->Stop(); }
void LogicSystem::BeginStop() { executor_->BeginStop(); }

void LogicSystem::ReportMetrics() const
{
  const auto m = executor_->Snapshot();
  const double divisor = m.completed ? static_cast<double>(m.completed) : 1.0;
  LOG_INFO("Business metrics: queued=", m.queued, ", active=", m.active,
           ", completed=", m.completed, ", rejected=", m.rejected, ", failed=", m.failed,
           ", high_water=", m.high_water, ", avg_wait_ms=", m.total_wait_ms / divisor,
           ", max_wait_ms=", m.max_wait_ms, ", avg_run_ms=", m.total_run_ms / divisor,
           ", max_run_ms=", m.max_run_ms);
}

bool LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> message)
{
  if (!message || !message->_session || !message->_recvnode)
    return false;
  // The UUID stays unchanged across login; UID-based routing would let requests
  // queued before login overtake the login itself.
  const auto key = message->_session->GetSessionId();
  return executor_->Submit(key, [this, message = std::move(message)] { HandleMessage(message); });
}

void LogicSystem::SetServer(std::shared_ptr<CServer> server)
{
  std::lock_guard<std::mutex> lock(server_mutex_);
  server_ = std::move(server);
}

std::shared_ptr<CServer> LogicSystem::GetServer()
{
  std::lock_guard<std::mutex> lock(server_mutex_);
  return server_;
}

void LogicSystem::HandleMessage(const std::shared_ptr<LogicNode> &message)
{
  if (message->_session->IsClosed())
    return;
  const short message_id = message->_recvnode->_msg_id;
  const auto callback = callbacks_.find(message_id);
  if (callback == callbacks_.end()) {
    LOG_WARNING("No callback registered for message id ", message_id, '.');
    return;
  }
  try {
    if (message_id != MSG_CHAT_LOGIN && message->_session->GetUserId() <= 0) {
      Json::Value response;
      response["error"] = ErrorCodes::TokenInvalid;
      Json::Value request;
      if (ParseRequest(std::string(message->_recvnode->_data, message->_recvnode->_cur_len),
                       request))
        RequestContract::EchoId(request, response);
      message->_session->Send(response.toStyledString(), static_cast<short>(message_id + 1));
      return;
    }
    callback->second(message->_session, message_id,
                     std::string(message->_recvnode->_data, message->_recvnode->_cur_len));
  } catch (const std::exception &error) {
    LOG_ERROR("Business handler failed: id=", message_id, ", error=", error.what());
    message->_session->Close();
  } catch (...) {
    LOG_ERROR("Business handler failed: id=", message_id);
    message->_session->Close();
  }
}

void LogicSystem::RegisterCallbacks()
{
  callbacks_[ID_VOICE_REQ] = VoiceCallService::Handle;
  callbacks_[ID_VOICE_FRAME] = VoiceCallService::Handle;
  callbacks_[ID_VIDEO_FRAME] = VoiceCallService::Handle;
  callbacks_[ID_GROUP_REQ] = [this](auto session, short id, const auto &data) {
    GroupRequest(session, id, data);
  };
  auto bind = [this](auto handler) {
    return std::bind(handler, this, std::placeholders::_1, std::placeholders::_2,
                     std::placeholders::_3);
  };
  callbacks_[MSG_CHAT_LOGIN] = bind(&LogicSystem::LoginHandler);
  callbacks_[ID_SEARCH_USER_REQ] = bind(&LogicSystem::SearchInfo);
  callbacks_[ID_SET_CONTACT_TAGS_REQ] = bind(&LogicSystem::SetContactTags);
  callbacks_[ID_ADD_FRIEND_REQ] = bind(&LogicSystem::AddFriendApply);
  callbacks_[ID_AUTH_FRIEND_REQ] = bind(&LogicSystem::AuthFriendApply);
  callbacks_[ID_TEXT_CHAT_MSG_REQ] = bind(&LogicSystem::DealChatTextMsg);
  callbacks_[ID_HEART_BEAT_REQ] = bind(&LogicSystem::HeartBeatHandler);
  callbacks_[ID_LOAD_CHAT_THREAD_REQ] = bind(&LogicSystem::GetUserThreadsHandler);
  callbacks_[ID_CREATE_PRIVATE_CHAT_REQ] = bind(&LogicSystem::CreatePrivateChat);
  callbacks_[ID_LOAD_CHAT_MSG_REQ] = bind(&LogicSystem::LoadChatMsg);
  callbacks_[ID_UPDATE_PROFILE_REQ] = bind(&LogicSystem::UpdateProfile);
  callbacks_[ID_GET_AVATAR_REQ] = bind(&LogicSystem::GetAvatar);
  callbacks_[ID_DELETE_FRIEND_REQ] = bind(&LogicSystem::DeleteFriend);
}

void LogicSystem::LoginHandler(std::shared_ptr<CSession> session, short,
                               const std::string &message_data)
{
  // Snapshot construction and Send may allocate. Keep them inside the request
  // exception boundary instead of a noexcept scope-guard destructor.
  try {
    Json::Value request, response;
    if (!ParseRequest(message_data, request) || !request["uid"].isInt() ||
        !request["token"].isString() ||
        (request.isMember("login_chunks") && !request["login_chunks"].isBool())) {
      response["error"] = ErrorCodes::Error_Json;
    } else {
      if (session->GetUserId() == 0)
        session->SetVoiceMessages(request.get("voice_messages", false).isBool() &&
                                  request.get("voice_messages", false).asBool());
      response =
          SessionService::Login(session, request["uid"].asInt(), request["token"].asString());
    }
    RequestContract::EchoId(request, response);
    if (response["error"].asInt() != ErrorCodes::Success) {
      Json::Value failure;
      failure["error"] = response["error"];
      RequestContract::EchoId(request, failure);
      session->Send(failure.toStyledString(), MSG_CHAT_LOGIN_RSP);
      if (response["error"].asInt() == ErrorCodes::InternalError)
        session->DrainAndClose(std::chrono::seconds(3), [] {});
      return;
    }
    std::vector<std::string> frames;
    const bool allow_chunks = request["login_chunks"].isBool() && request["login_chunks"].asBool();
    if (!LoginSnapshot::BuildFrames(response, allow_chunks, frames)) {
      Json::Value failure;
      failure["error"] = ErrorCodes::InternalError;
      RequestContract::EchoId(request, failure);
      session->Send(failure.toStyledString(), MSG_CHAT_LOGIN_RSP);
      // A failed snapshot must not leave its authenticated route/token alive.
      session->DrainAndClose(std::chrono::seconds(3), [] {});
      return;
    }
    for (const auto &frame : frames)
      session->Send(frame, MSG_CHAT_LOGIN_RSP);
  } catch (...) {
    // Logging and close are best effort even if the original failure was an
    // allocation error; never terminate a worker while unwinding this request.
    try {
      LOG_ERROR("Login snapshot response failed; closing the session.");
    } catch (...) {
    }
    try {
      session->Close();
    } catch (...) {
    }
  }
}

void LogicSystem::SearchInfo(std::shared_ptr<CSession> session, short,
                             const std::string &message_data)
{
  Json::Value request;
  Json::Value response;
  Defer send_response([&request, &response, session]() {
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_SEARCH_USER_RSP);
  });
  if (!ParseRequest(message_data, request) || request["uid"].isNull()) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }

  const std::string query = request["uid"].isString() ? request["uid"].asString()
                                                      : std::to_string(request["uid"].asInt());
  if (IsPureDigit(query)) {
    GetUserByUid(query, response);
  } else {
    GetUserByName(query, response);
  }
}

void LogicSystem::AddFriendApply(std::shared_ptr<CSession> session, short,
                                 const std::string &message_data)
{
  Json::Value request;
  Json::Value response;
  Defer send_response([&request, &response, session]() {
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_ADD_FRIEND_RSP);
  });
  if (!ParseRequest(message_data, request) || !request["uid"].isInt() ||
      !request["touid"].isInt()) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }

  const int uid = request["uid"].asInt();
  const int to_uid = request["touid"].asInt();
  const std::string description = request.get("applyname", "").asString();
  const std::string remark = request.get("bakname", "").asString();
  Json::Value tags;
  if (uid != session->GetUserId() ||
      !NormalizeContactTags(request.get("tags", Json::Value(Json::arrayValue)), tags)) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }
  response["error"] = FriendService::Apply(uid, to_uid, description, remark, tags);
  if (response["error"].asInt() != ErrorCodes::Success) {
    return;
  }
}

void LogicSystem::AuthFriendApply(std::shared_ptr<CSession> session, short,
                                  const std::string &message_data)
{
  Json::Value request;
  Json::Value response;
  Defer send_response([&request, &response, session]() {
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_AUTH_FRIEND_RSP);
  });
  if (!ParseRequest(message_data, request) || !request["fromuid"].isInt() ||
      !request["touid"].isInt()) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }

  const int from_uid = request["fromuid"].asInt();
  const int to_uid = request["touid"].asInt();
  std::vector<std::shared_ptr<ChatMessage>> initial_messages;
  if (from_uid <= 0 || from_uid != session->GetUserId() || to_uid <= 0 || from_uid == to_uid) {
    response["error"] = ErrorCodes::UidInvalid;
    return;
  }
  Json::Value tags;
  if (!NormalizeContactTags(request.get("tags", Json::Value(Json::arrayValue)), tags)) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }
  if (!FriendService::Accept(from_uid, to_uid, request.get("back", "").asString(), tags,
                             initial_messages)) {
    response["error"] = ErrorCodes::DatabaseError;
    return;
  }

  response["error"] = ErrorCodes::Success;
  response["uid"] = to_uid;
  auto accepted_user = std::make_shared<UserInfo>();
  if (UserService::GetById(to_uid, accepted_user)) {
    FillUserJson(*accepted_user, response);
  }
  if (initial_messages.empty()) {
    std::string persisted_back, persisted_tags;
    const int metadata_result = MysqlMgr::GetInstance()->GetFriendMetadata(
        from_uid, to_uid, persisted_back, persisted_tags);
    if (metadata_result != ErrorCodes::Success) {
      response["error"] = metadata_result;
      return;
    }
    response["back"] = persisted_back;
    response["tags"] = ParseContactTags(persisted_tags);
  } else {
    response["back"] = request.get("back", "");
    response["tags"] = tags;
  }
  const std::string timestamp = GetCurrentTimestamp();
  for (const auto &message : initial_messages) {
    if (message->chat_time.empty()) {
      message->chat_time = timestamp;
    }
    Json::Value value;
    FillChatJson(*message, value);
    response["chat_datas"].append(value);
  }

  if (!initial_messages.empty())
    FriendService::NotifyAcceptance(from_uid, to_uid, response);
}

void LogicSystem::DeleteFriend(std::shared_ptr<CSession> session, short,
                               const std::string &message_data)
{
  Json::Value request;
  Json::Value response;
  response["error"] = ErrorCodes::Error_Json;
  Defer send_response([&request, &response, session] {
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_DELETE_FRIEND_RSP);
  });
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  std::string errors;
  if (!reader->parse(message_data.data(), message_data.data() + message_data.size(), &request,
                     &errors) ||
      !request.isObject() || !request["friend_uid"].isInt())
    return;

  const int self_uid = session->GetUserId();
  const int friend_uid = request["friend_uid"].asInt();
  response["friend_uid"] = friend_uid;
  response["error"] = FriendService::Delete(self_uid, friend_uid);
}

void LogicSystem::DealChatTextMsg(std::shared_ptr<CSession> session, short,
                                  const std::string &message_data)
{
  Json::Value request;
  Json::Value response;
  bool acknowledged = false;
  Defer send_response([&request, &response, &acknowledged, session]() {
    if (!acknowledged) {
      RequestContract::EchoId(request, response);
      session->Send(response.toStyledString(), ID_TEXT_CHAT_MSG_RSP);
    }
  });
  if (!ParseRequest(message_data, request) || !request["fromuid"].isInt() ||
      !request["touid"].isInt() || !request["thread_id"].isInt() ||
      !request["text_array"].isArray()) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }

  const int from_uid = request["fromuid"].asInt();
  const int to_uid = request["touid"].asInt();
  const int thread_id = request["thread_id"].asInt();
  if (from_uid <= 0 || from_uid != session->GetUserId() || to_uid < 0 || to_uid == from_uid ||
      thread_id <= 0) {
    response["error"] = ErrorCodes::UidInvalid;
    return;
  }
  response["fromuid"] = from_uid;
  response["touid"] = to_uid;
  response["thread_id"] = thread_id;

  std::vector<std::shared_ptr<ChatMessage>> messages;
  const std::string timestamp = GetCurrentTimestamp();
  for (const auto &item : request["text_array"]) {
    if (!item["content"].isString()) {
      continue;
    }
    auto message = std::make_shared<ChatMessage>();
    message->thread_id = thread_id;
    message->sender_id = from_uid;
    message->recv_id = to_uid == 0 ? from_uid : to_uid;
    message->unique_id = item.get("unique_id", item.get("msgid", "")).asString();
    message->content = item["content"].asString();
    message->chat_time = timestamp;
    message->status = 2;
    message->msg_type = item.get("msg_type", 0).asInt();
    if (message->msg_type == 3 && !session->SupportsVoiceMessages()) {
      response["error"] = ErrorCodes::Error_Json;
      return;
    }
    messages.push_back(std::move(message));
  }
  const int stored = MessageService::Store(from_uid, messages);
  if (stored != ErrorCodes::Success) {
    response["error"] = stored;
    return;
  }

  response["error"] = ErrorCodes::Success;
  for (const auto &message : messages) {
    Json::Value value;
    FillChatJson(*message, value);
    response["chat_datas"].append(value);
  }

  if (to_uid == 0) {
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_TEXT_CHAT_MSG_RSP);
    acknowledged = true;
    Json::Value group;
    if (MysqlMgr::GetInstance()->GetGroup(from_uid, thread_id, group, true) ==
        ErrorCodes::Success) {
      response["group"] = group;
      response["group"].removeMember("members");
      for (const auto &member : group["members"])
        if (member["uid"].asInt() == from_uid)
          response["group"]["member_names"][std::to_string(from_uid)] = member["name"];
      for (const auto &member : group["members"]) {
        const int recipient = member["uid"].asInt();
        if (recipient != from_uid)
          MessageService::Deliver(from_uid, recipient, thread_id, messages, response);
      }
    }
  } else
    MessageService::Deliver(from_uid, to_uid, thread_id, messages, response);
}

void LogicSystem::HeartBeatHandler(std::shared_ptr<CSession> session, short,
                                   const std::string &message_data)
{
  Json::Value request;
  const bool valid = ParseRequest(message_data, request);
  if (!valid) {
    Json::Value response;
    response["error"] = ErrorCodes::Error_Json;
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_HEARTBEAT_RSP);
    return;
  }
  const std::optional<int> snapshot =
      valid && request["friend_snapshot_id"].isInt()
          ? std::optional<int>(request["friend_snapshot_id"].asInt())
          : std::nullopt;
  auto response = SessionService::Heartbeat(session, snapshot);
  RequestContract::EchoId(request, response);
  if (response.toStyledString().size() >= MAX_LENGTH)
    response.removeMember("friend_ids");
  session->Send(response.toStyledString(), ID_HEARTBEAT_RSP);
}

bool LogicSystem::IsPureDigit(const std::string &text) const
{
  return !text.empty() && std::all_of(text.begin(), text.end(),
                                      [](unsigned char value) { return std::isdigit(value) != 0; });
}

void LogicSystem::UpdateProfile(std::shared_ptr<CSession> session, short,
                                const std::string &message_data)
{
  Json::Value request;
  Json::Value response;
  Defer send_response([&request, &response, session]() {
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_UPDATE_PROFILE_RSP);
  });
  const int uid = session->GetUserId();
  if (uid <= 0) {
    response["error"] = ErrorCodes::TokenInvalid;
    return;
  }
  if (!ParseRequest(message_data, request) || !request["name"].isString() ||
      !request["nick"].isString() || !request["desc"].isString() ||
      (request.isMember("avatar") && !request["avatar"].isString())) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }
  const std::string name = request["name"].asString();
  const std::string nick = request["nick"].asString();
  const std::string description = request["desc"].asString();
  if (name.empty() || name.size() > 64 || nick.size() > 64 || description.size() > 255) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }
  std::string avatar;
  const bool has_avatar = request.isMember("avatar");
  if (has_avatar && !DecodeAvatar(request["avatar"].asString(), avatar)) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }
  const auto result = UserService::UpdateProfile(uid, name, nick, description, avatar, has_avatar);
  response["error"] = result.error;
  if (result.committed) {
    response["uid"] = uid;
    response["name"] = name;
    response["nick"] = nick;
    response["desc"] = description;
    response["avatar_changed"] = has_avatar;
    response["committed"] = true;
  }
}

void LogicSystem::GetAvatar(std::shared_ptr<CSession> session, short,
                            const std::string &message_data)
{
  Json::Value request;
  Json::Value response;
  Defer send_response([&request, &response, session]() {
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_GET_AVATAR_RSP);
  });
  if (session->GetUserId() <= 0) {
    response["error"] = ErrorCodes::TokenInvalid;
    return;
  }
  if (!ParseRequest(message_data, request) || !request["uid"].isInt() ||
      request["uid"].asInt() <= 0) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }
  const int uid = request["uid"].asInt();
  response["uid"] = uid;
  std::string avatar;
  if (!UserService::GetAvatar(uid, avatar) || avatar.size() > 8192) {
    response["error"] = ErrorCodes::DatabaseError;
    return;
  }
  response["error"] = ErrorCodes::Success;
  response["avatar"] = avatar.empty() ? "" : EncodeAvatar(avatar);
}

void LogicSystem::GetUserByUid(const std::string &uid_text, Json::Value &response)
{
  try {
    const int uid = std::stoi(uid_text);
    auto user = std::make_shared<UserInfo>();
    if (!UserService::GetById(uid, user)) {
      response["error"] = ErrorCodes::UidInvalid;
      return;
    }
    response["error"] = ErrorCodes::Success;
    FillUserJson(*user, response);
  } catch (const std::exception &) {
    response["error"] = ErrorCodes::UidInvalid;
  }
}

void LogicSystem::GetUserByName(const std::string &name, Json::Value &response)
{
  const auto user = UserService::GetByName(name);
  if (!user) {
    response["error"] = ErrorCodes::UidInvalid;
    return;
  }
  response["error"] = ErrorCodes::Success;
  FillUserJson(*user, response);
}

void LogicSystem::GetUserThreadsHandler(std::shared_ptr<CSession> session, short,
                                        const std::string &message_data)
{
  Json::Value request;
  Json::Value response;
  Defer send_response([&request, &response, session]() {
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_LOAD_CHAT_THREAD_RSP);
  });
  if (!ParseRequest(message_data, request) || !request["uid"].isInt()) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }

  const int uid = request["uid"].asInt();
  const int last_id = request.get("thread_id", 0).asInt();
  if (uid <= 0 || uid != session->GetUserId()) {
    response["error"] = ErrorCodes::UidInvalid;
    return;
  }
  std::vector<std::shared_ptr<ChatThreadInfo>> threads;
  bool load_more = false;
  int next_last_id = 0;
  if (!MessageService::ListThreads(uid, last_id, 10, threads, load_more, next_last_id)) {
    response["error"] = ErrorCodes::UidInvalid;
    return;
  }

  response["error"] = ErrorCodes::Success;
  response["uid"] = uid;
  response["load_more"] = load_more;
  response["next_last_id"] = next_last_id;
  for (const auto &thread : threads) {
    Json::Value value;
    value["thread_id"] = thread->_thread_id;
    value["type"] = thread->_type;
    value["user1_id"] = thread->_user1_id;
    value["user2_id"] = thread->_user2_id;
    value["last_message_id"] = thread->_last_message_id;
    value["last_chat_time"] = thread->_last_chat_time;
    if (thread->_type == "group") {
      Json::Value group;
      if (MysqlMgr::GetInstance()->GetGroup(uid, thread->_thread_id, group) != ErrorCodes::Success)
        continue;
      value["group"] = group;
    }
    response["threads"].append(value);
  }
}

void LogicSystem::CreatePrivateChat(std::shared_ptr<CSession> session, short,
                                    const std::string &message_data)
{
  Json::Value request;
  Json::Value response;
  Defer send_response([&request, &response, session]() {
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_CREATE_PRIVATE_CHAT_RSP);
  });
  if (!ParseRequest(message_data, request) || !request["uid"].isInt() ||
      !request["other_id"].isInt()) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }

  const int uid = request["uid"].asInt();
  const int other_uid = request["other_id"].asInt();
  int thread_id = 0;
  response["uid"] = uid;
  response["other_id"] = other_uid;
  if (uid <= 0 || uid != session->GetUserId()) {
    response["error"] = ErrorCodes::UidInvalid;
    return;
  }
  response["error"] = MessageService::CreatePrivate(uid, other_uid, thread_id);
  if (response["error"].asInt() == ErrorCodes::Success) {
    response["thread_id"] = thread_id;
  }
}

void LogicSystem::LoadChatMsg(std::shared_ptr<CSession> session, short,
                              const std::string &message_data)
{
  Json::Value request;
  Json::Value response;
  Defer send_response([&request, &response, session]() {
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_LOAD_CHAT_MSG_RSP);
  });
  if (!ParseRequest(message_data, request) || !request["thread_id"].isInt()) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }

  const int thread_id = request["thread_id"].asInt();
  const int message_id = request.get("message_id", 0).asInt();
  // Include the envelope before measuring each candidate history page.
  RequestContract::EchoId(request, response);
  response["thread_id"] = thread_id;
  std::shared_ptr<PageResult> page;
  const int result = MessageService::Load(session->GetUserId(), thread_id, message_id, page);
  if (result != ErrorCodes::Success) {
    response["error"] = result;
    return;
  }

  response["error"] = ErrorCodes::Success;
  response["thread_id"] = thread_id;
  response["last_message_id"] = message_id;
  response["load_more"] = page->load_more;
  response["chat_datas"] = Json::Value(Json::arrayValue);
  for (const auto &message : page->messages) {
    Json::Value value;
    FillChatJson(message, value, session->SupportsVoiceMessages());
    Json::Value candidate = response;
    candidate["chat_datas"].append(value);
    candidate["last_message_id"] = message.message_id;
    // Reserve space for load_more changing from true to false.
    if (candidate.toStyledString().size() > MAX_LENGTH - 16) {
      if (response["chat_datas"].empty()) {
        response["error"] = ErrorCodes::LoadChatFailed;
        response["load_more"] = false;
      } else {
        response["load_more"] = true;
      }
      break;
    }
    response = std::move(candidate);
  }
}

void LogicSystem::SetContactTags(std::shared_ptr<CSession> session, short,
                                 const std::string &message_data)
{
  Json::Value request, response;
  Defer send_response([&request, &response, session]() {
    RequestContract::EchoId(request, response);
    session->Send(response.toStyledString(), ID_SET_CONTACT_TAGS_RSP);
  });
  Json::Value tags;
  if (!ParseRequest(message_data, request)) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }
  response["request_id"] = request.get("request_id", "");
  if (!request["friend_uid"].isInt() || !NormalizeContactTags(request["tags"], tags) ||
      (request.isMember("back") && !ValidContactRemark(request["back"]))) {
    response["error"] = ErrorCodes::Error_Json;
    return;
  }
  response["friend_uid"] = request["friend_uid"];
  response["error"] =
      FriendService::SetTags(session->GetUserId(), request["friend_uid"].asInt(), request["tags"],
                             tags, request.get("back", Json::Value()));
  if (response["error"].asInt() == ErrorCodes::Success) {
    response["tags"] = tags;
    if (request.isMember("back"))
      response["back"] = request["back"];
  }
}

void LogicSystem::GroupRequest(std::shared_ptr<CSession> session, short, const std::string &data)
{
  Json::Value request, response;
  response["error"] = ErrorCodes::Error_Json;
  if (ParseRequest(data, request)) {
    response["op"] = request.get("op", "");
    response["error"] =
        MysqlMgr::GetInstance()->ChangeGroup(session->GetUserId(), request, response["group"]);
  }
  RequestContract::EchoId(request, response);
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  session->Send(Json::writeString(writer, response), ID_GROUP_RSP);
}
