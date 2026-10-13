#include "ChatGrpcClient.h"
#include "Logger.h"
#include <chrono>
#include <sstream>
#include <vector>

ChatGrpcClient::ChatGrpcClient()
{
  auto &config = ConfigMgr::GetInstance();
  std::stringstream sections(config["PeerServer"]["Servers"]);
  std::string section;
  while (std::getline(sections, section, ',')) {
    const auto name = config[section]["Name"];
    if (name.empty())
      continue;
    chat_stub_pools_[name] =
        std::make_unique<ChatStubPool>(5, config[section]["Host"], config[section]["Port"]);
  }
}

AddFriendRsp ChatGrpcClient::NotifyAddFriend(std::string server_name, const AddFriendReq &request)
{
  AddFriendRsp reply;
  reply.set_error(ErrorCodes::RPCFailed);
  reply.set_applyuid(request.applyuid());
  reply.set_touid(request.touid());
  const auto found = chat_stub_pools_.find(server_name);
  if (found == chat_stub_pools_.end()) {
    LOG_WARNING("NotifyAddFriend: unknown chat server ", server_name);
    return reply;
  }
  auto &pool = found->second;
  auto stub = pool->BorrowStub();
  if (!stub) {
    LOG_WARNING("NotifyAddFriend: no available Stub for ", server_name);
    return reply;
  }
  Defer return_stub([&pool, &stub]() { pool->ReturnStub(std::move(stub)); });
  ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
  const Status status = stub->NotifyAddFriend(&context, request, &reply);
  if (!status.ok()) {
    reply.set_error(ErrorCodes::RPCFailed);
    LOG_WARNING("NotifyAddFriend RPC failed: server=", server_name,
                ", grpc_status=", status.error_message());
  }
  return reply;
}

AuthFriendRsp ChatGrpcClient::NotifyAuthFriend(std::string server_name,
                                               const AuthFriendReq &request)
{
  AuthFriendRsp reply;
  reply.set_error(ErrorCodes::RPCFailed);
  reply.set_fromuid(request.fromuid());
  reply.set_touid(request.touid());
  const auto found = chat_stub_pools_.find(server_name);
  if (found == chat_stub_pools_.end()) {
    LOG_WARNING("NotifyAuthFriend: unknown chat server ", server_name);
    return reply;
  }
  auto &pool = found->second;
  auto stub = pool->BorrowStub();
  if (!stub) {
    LOG_WARNING("NotifyAuthFriend: no available Stub for ", server_name);
    return reply;
  }
  Defer return_stub([&pool, &stub]() { pool->ReturnStub(std::move(stub)); });
  ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
  const Status status = stub->NotifyAuthFriend(&context, request, &reply);
  if (!status.ok()) {
    reply.set_error(ErrorCodes::RPCFailed);
    LOG_WARNING("NotifyAuthFriend RPC failed: server=", server_name,
                ", grpc_status=", status.error_message());
  }
  return reply;
}

TextChatMsgRsp ChatGrpcClient::NotifyTextChatMsg(std::string server_name,
                                                 const TextChatMsgReq &request, const Json::Value &)
{
  TextChatMsgRsp reply;
  reply.set_error(ErrorCodes::RPCFailed);
  reply.set_fromuid(request.fromuid());
  reply.set_touid(request.touid());
  const auto found = chat_stub_pools_.find(server_name);
  if (found == chat_stub_pools_.end()) {
    LOG_WARNING("NotifyTextChatMsg: unknown chat server ", server_name);
    return reply;
  }
  auto &pool = found->second;
  auto stub = pool->BorrowStub();
  if (!stub) {
    LOG_WARNING("NotifyTextChatMsg: no available Stub for ", server_name);
    return reply;
  }
  Defer return_stub([&pool, &stub]() { pool->ReturnStub(std::move(stub)); });
  ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
  const Status status = stub->NotifyTextChatMsg(&context, request, &reply);
  if (!status.ok()) {
    reply.set_error(ErrorCodes::RPCFailed);
    LOG_WARNING("NotifyTextChatMsg RPC failed: server=", server_name,
                ", grpc_status=", status.error_message());
  }
  return reply;
}

KickUserRsp ChatGrpcClient::NotifyKickUser(const std::string &server_name,
                                           const KickUserReq &request)
{
  KickUserRsp reply;
  reply.set_error(ErrorCodes::RPCFailed);

  const auto found = chat_stub_pools_.find(server_name);
  if (found == chat_stub_pools_.end()) {
    LOG_WARNING("NotifyKickUser: unknown chat server ", server_name);
    return reply;
  }
  auto &pool = found->second;
  auto stub = pool->BorrowStub();
  if (!stub) {
    LOG_WARNING("NotifyKickUser: no available Stub for ", server_name);
    return reply;
  }
  Defer return_stub([&pool, &stub]() { pool->ReturnStub(std::move(stub)); });
  ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
  const Status status = stub->NotifyKickUser(&context, request, &reply);
  if (!status.ok()) {
    reply.set_error(ErrorCodes::RPCFailed);
    LOG_WARNING("NotifyKickUser RPC failed: server=", server_name,
                ", grpc_status=", status.error_message());
  }
  return reply;
}

message::DeleteFriendRsp ChatGrpcClient::NotifyDeleteFriend(const std::string &server_name,
                                                            const message::DeleteFriendReq &request)
{
  message::DeleteFriendRsp reply;
  reply.set_error(ErrorCodes::RPCFailed);

  const auto found = chat_stub_pools_.find(server_name);
  if (found == chat_stub_pools_.end()) {
    LOG_WARNING("NotifyDeleteFriend: unknown chat server ", server_name);
    return reply;
  }
  auto &pool = found->second;
  auto stub = pool->BorrowStub();
  if (!stub) {
    LOG_WARNING("NotifyDeleteFriend: no available Stub for ", server_name);
    return reply;
  }
  Defer return_stub([&pool, &stub]() { pool->ReturnStub(std::move(stub)); });
  ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
  const Status status = stub->NotifyDeleteFriend(&context, request, &reply);
  if (!status.ok()) {
    reply.set_error(ErrorCodes::RPCFailed);
    LOG_WARNING("NotifyDeleteFriend RPC failed: server=", server_name,
                ", grpc_status=", status.error_message());
  }
  return reply;
}
