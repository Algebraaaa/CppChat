#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "Logger.h"
#include "RedisMgr.h"
#include "ServicePolicy.h"
#include "const.h"
#include <algorithm>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <json/json.h>
#include <limits>
#include <vector>

StatusServiceImpl::StatusServiceImpl()
{
  auto &config = ConfigMgr::GetInstance();
  for (const char *section_name : {"ChatServer1", "ChatServer2"}) {
    const auto section = config[section_name];
    if (section.GetValue("Enabled", "true") != "true")
      continue;
    ChatServer server;
    server.name = section.GetValue("Name", section_name);
    server.host = section["Host"];
    server.port = section["Port"];
    if (server.name.empty() || server.host.empty() || server.port.empty()) {
      LOG_WARNING("Ignoring incomplete chat node: ", section_name);
      continue;
    }
    _servers[server.name] = server;
  }
}

ChatServer StatusServiceImpl::getChatServer(int &error)
{
  std::lock_guard<std::mutex> guard(_server_mtx);
  std::vector<ChatServer> candidates;
  int minimum = std::numeric_limits<int>::max();
  error = ErrorCodes::RPCFailed;
  for (const auto &entry : _servers) {
    std::string value;
    const auto read = RedisMgr::GetInstance()->Read(
        std::string(ServicePolicy::NodeHealthPrefix) + entry.first, value);
    if (read == RedisReadStatus::Error) {
      error = ErrorCodes::RedisError;
      return {};
    }
    if (read != RedisReadStatus::Found)
      continue;
    Json::Value health;
    Json::Reader reader;
    if (!reader.parse(value, health) || !health.isObject() || !health["instance_id"].isString() ||
        health["instance_id"].asString().empty() || !health["online"].isUInt64() ||
        health["online"].asUInt64() > static_cast<Json::UInt64>(std::numeric_limits<int>::max()))
      continue;
    auto server = entry.second;
    server.con_count = static_cast<int>(health["online"].asUInt64());
    if (server.con_count < minimum) {
      minimum = server.con_count;
      candidates.clear();
    }
    if (server.con_count == minimum)
      candidates.push_back(server);
  }
  if (candidates.empty())
    return {};
  std::sort(candidates.begin(), candidates.end(),
            [](const ChatServer &left, const ChatServer &right) { return left.name < right.name; });
  error = ErrorCodes::Success;
  // Rotate equal measured loads; an allocation is not an online session.
  return candidates[_next_choice++ % candidates.size()];
}

Status StatusServiceImpl::GetChatServer(ServerContext *, const GetChatServerReq *request,
                                        GetChatServerRsp *reply)
{
  if (!request || !reply || request->uid() <= 0) {
    if (reply)
      reply->set_error(ErrorCodes::UidInvalid);
    return Status::OK;
  }
  int error = ErrorCodes::RPCFailed;
  const auto server = getChatServer(error);
  reply->set_error(error);
  if (error != ErrorCodes::Success)
    return Status::OK;
  const std::string token = boost::uuids::to_string(boost::uuids::random_generator()());
  if (!RedisMgr::GetInstance()->SetEx(std::string(ServicePolicy::PendingTokenPrefix) +
                                          std::to_string(request->uid()) + ":" + token,
                                      token, ServicePolicy::PendingTokenSeconds)) {
    reply->set_error(ErrorCodes::RedisError);
    return Status::OK;
  }
  reply->set_host(server.host);
  reply->set_port(server.port);
  reply->set_token(token);
  LOG_INFO("Chat node allocated: uid=", request->uid(), ", node=", server.name,
           ", reported_online=", server.con_count);
  return Status::OK;
}

Status StatusServiceImpl::Login(ServerContext *, const LoginReq *request, LoginRsp *reply)
{
  if (!request || !reply || request->uid() <= 0) {
    if (reply)
      reply->set_error(ErrorCodes::UidInvalid);
    return Status::OK;
  }
  reply->set_error(ErrorCodes::TokenInvalid);
  if (request->token().empty())
    return Status::OK;
  const std::string uid = std::to_string(request->uid());
  std::string token;
  auto read = RedisMgr::GetInstance()->Read(
      std::string(ServicePolicy::PendingTokenPrefix) + uid + ":" + request->token(), token, true);
  if (read == RedisReadStatus::Error) {
    reply->set_error(ErrorCodes::RedisError);
    return Status::OK;
  }
  if (read != RedisReadStatus::Found || token != request->token()) {
    read = RedisMgr::GetInstance()->Read(std::string(ServicePolicy::PendingTokenPrefix) + uid,
                                         token, true);
    if (read == RedisReadStatus::Error) {
      reply->set_error(ErrorCodes::RedisError);
      return Status::OK;
    }
  }
  if (read != RedisReadStatus::Found || token != request->token()) {
    read = RedisMgr::GetInstance()->Read(std::string(ServicePolicy::ActiveTokenPrefix) + uid, token,
                                         true);
    if (read == RedisReadStatus::Error) {
      reply->set_error(ErrorCodes::RedisError);
      return Status::OK;
    }
    if (read != RedisReadStatus::Found || token != request->token()) {
      read = RedisMgr::GetInstance()->Read(
          std::string(ServicePolicy::ActiveTokenPrefix) + uid + ":android", token, true);
      if (read == RedisReadStatus::Error) {
        reply->set_error(ErrorCodes::RedisError);
        return Status::OK;
      }
      if (read != RedisReadStatus::Found || token != request->token())
        return Status::OK;
    }
  }
  reply->set_error(ErrorCodes::Success);
  reply->set_uid(request->uid());
  reply->set_token(request->token());
  return Status::OK;
}
