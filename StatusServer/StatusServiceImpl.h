#pragma once

#include <cstddef>
#include "message.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <mutex>
#include <string>
#include <unordered_map>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginReq;
using message::LoginRsp;
using message::StatusService;

struct ChatServer {
  std::string host;
  std::string port;
  std::string name;
  int con_count = 0;
};
class StatusServiceImpl final : public StatusService::Service {
public:
  StatusServiceImpl();
  Status GetChatServer(ServerContext *context, const GetChatServerReq *request,
                       GetChatServerRsp *reply) override;
  Status Login(ServerContext *context, const LoginReq *request,
               LoginRsp *reply) override;

private:
  ChatServer getChatServer(int &error);
  std::unordered_map<std::string, ChatServer> _servers;
  std::mutex _server_mtx;
  std::size_t _next_choice = 0;
};
