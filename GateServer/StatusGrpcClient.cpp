#include "StatusGrpcClient.h"
#include "const.h"
#include "Logger.h"

message::GetChatServerRsp StatusGrpcClient::GetChatServer(int uid)
{
  // 创建一个本次客户端RPC调用的上下文
  grpc::ClientContext context;
  message::GetChatServerRsp reply;
  message::GetChatServerReq request;
  request.set_uid(uid);
  auto stub = pool_->BorrowStub();

  // 连接池停止后会返回空指针，必须先判断，不能继续调用 stub->GetChatServer。
  if (stub == nullptr) {
    LOG_ERROR("StatusServer request failed because no gRPC connection is available.");
    reply.set_error(ErrorCodes::RPCFailed);
    return reply;
  }

  // Defer 保证 RPC 完成或函数提前返回时，Stub 都能归还连接池。
  Defer defer([&stub, this]() {
    pool_->ReturnStub(std::move(stub));
    });
  // 它表示这次RPC的通信结果
  grpc::Status status = stub->GetChatServer(&context, request, &reply);
  if (status.ok()) {
    // 这里只记录 uid 和业务错误码，不能记录 StatusServer 返回的登录 token。
    LOG_DEBUG(
      "StatusServer GetChatServer completed: uid=", uid,
      ", business error=", reply.error());
    return reply;
  }

  LOG_ERROR(
    "StatusServer GetChatServer RPC failed: uid=", uid,
    ", grpc code=", static_cast<int>(status.error_code()),
    ", message=", status.error_message());
  reply.set_error(ErrorCodes::RPCFailed);
  return reply;
}

StatusGrpcClient::StatusGrpcClient()
{
  auto& gCfgMgr = ConfigMgr::GetInstance();
  std::string host = gCfgMgr["StatusServer"]["Host"];
  std::string port = gCfgMgr["StatusServer"]["Port"];
  pool_ = std::make_unique<StatusStubPool>(5, host, port);
  LOG_INFO(
    "StatusGrpcClient initialized: endpoint=", host, ":", port,
    ", pool size=5");
}
