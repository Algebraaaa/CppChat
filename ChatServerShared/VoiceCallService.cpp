#include "VoiceCallService.h"
#include "CSession.h"
#include "ChatGrpcClient.h"
#include "ConfigMgr.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "const.h"
#include <algorithm>
#include <json/json.h>
#include <regex>

namespace {
bool Deliver(int peer, const std::string &session, const Json::Value &body, bool audio, bool video)
{
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  const auto data = Json::writeString(writer, body);
  if (auto target = UserMgr::GetInstance()->GetSession(peer)) {
    if (target->GetSessionId() != session || target->IsClosed())
      return false;
    target->Send(data, video ? ID_VIDEO_IMAGE : audio ? ID_VOICE_AUDIO : ID_VOICE_EVENT);
    return true;
  }
  std::string node;
  if (!RedisMgr::GetInstance()->Get(std::string(USER_IP_PREFIX) + std::to_string(peer), node))
    return false;
  return ChatGrpcClient::GetInstance()->RelayVoice(node, peer, session, data, audio, video);
}
} // namespace

void VoiceCallService::Handle(const std::shared_ptr<CSession> &session, short id,
                              const std::string &data)
{
  Json::Value request, reply;
  Json::CharReaderBuilder reader;
  std::string errors;
  const bool picture = id == ID_VIDEO_FRAME;
  const bool media = picture || id == ID_VOICE_FRAME;
  const bool parsed = data.size() <= (picture ? 9000 : 2000) &&
                      std::unique_ptr<Json::CharReader>(reader.newCharReader())
                          ->parse(data.data(), data.data() + data.size(), &request, &errors) &&
                      request.isObject();
  const Json::Value &payload = request;
  const int uid = session->GetUserId();
  const int peer = parsed && payload["peer"].isInt() ? payload["peer"].asInt() : 0;
  const auto call = parsed && payload["call_id"].isString() ? payload["call_id"].asString() : "";
  const auto action =
      media ? (picture ? "video" : "frame")
            : (parsed && payload["action"].isString() ? payload["action"].asString() : "");
  static const std::regex call_pattern("^[a-f0-9]{32}$");
  const bool video = parsed && payload["video"].isBool() && payload["video"].asBool();
  int error = ErrorCodes::Error_Json;
  const std::string actions[] = {"invite", "accept", "reject", "cancel", "end",
                                 "ping",   "frame",  "video",  "camera"};
  bool valid = parsed && uid > 0 && peer > 0 && peer != uid &&
               std::regex_match(call, call_pattern) &&
               std::find(std::begin(actions), std::end(actions), action) != std::end(actions) &&
               (media || (action != "frame" && action != "video")) &&
               (!request.isMember("video") || payload["video"].isBool()) &&
               (action != "camera" || payload["enabled"].isBool()) && (!picture || video);
  int sequence = 0;
  if (media) {
    valid = valid && payload["seq"].isInt() && payload["seq"].asInt() > 0 &&
            payload[picture ? "image" : "audio"].isString();
    if (valid) {
      sequence = payload["seq"].asInt();
      const auto bytes = payload[picture ? "image" : "audio"].asString();
      valid = bytes.size() >= 4 && bytes.size() <= (picture ? 8000 : 684) && bytes.size() % 4 == 0;
      const auto padding = bytes.find('=');
      const auto end = padding == std::string::npos ? bytes.size() : padding;
      valid = valid &&
              (padding == std::string::npos ||
               (padding >= bytes.size() - 2 &&
                bytes.find_first_not_of('=', padding) == std::string::npos)) &&
              std::all_of(bytes.begin(), bytes.begin() + end,
                          [](unsigned char c) {
                            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                   (c >= '0' && c <= '9') || c == '+' || c == '/';
                          }) &&
              (!picture || bytes.rfind("/9j/", 0) == 0);
    }
  }
  std::string target_session;
  if (valid) {
    auto redis = RedisMgr::GetInstance();
    if (action == "invite" || action == "accept" || action == "ping")
      error = MysqlMgr::GetInstance()->CheckFriendship(uid, peer);
    else
      error = ErrorCodes::Success;
    if (error == ErrorCodes::Success &&
        !redis->Get(std::string(USER_SESSION_PREFIX) + std::to_string(peer), target_session))
      error = ErrorCodes::VoiceOffline;
    if (error == ErrorCodes::Success)
      error = redis->VoiceTransition(uid, peer, session->GetSessionId(), call, action, sequence,
                                     video, action != "camera" || payload["enabled"].asBool());
  }
  reply["error"] = error;
  reply["call_id"] = call;
  reply["peer"] = peer;
  reply["action"] = action;
  reply["video"] = video;
  if (action == "camera" && payload["enabled"].isBool())
    reply["enabled"] = payload["enabled"];
  if (request.isObject() && payload["request_id"].isString() &&
      payload["request_id"].asString().size() <= 64)
    reply["request_id"] = payload["request_id"];
  // Audio has no success ACK, keeping both queues bounded to actual media.
  if (error == ErrorCodes::Success) {
    if (action != "ping") {
      auto event = reply;
      event["peer"] = uid;
      event["incoming"] = true;
      if (media) {
        event["seq"] = sequence;
        event[picture ? "image" : "audio"] = payload[picture ? "image" : "audio"];
      }
      if (!Deliver(peer, target_session, event, id == ID_VOICE_FRAME, picture)) {
        RedisMgr::GetInstance()->VoiceTransition(uid, peer, session->GetSessionId(), call, "end");
        reply["error"] = ErrorCodes::VoiceOffline;
      }
    }
  }
  if (!media || reply["error"].asInt() != ErrorCodes::Success) {
    // A token-bucket drop is expected under a short capture burst.
    if (!(media && reply["error"].asInt() == ErrorCodes::VoiceRateLimited))
      session->Send(reply.toStyledString(), ID_VOICE_EVENT);
  }
}
