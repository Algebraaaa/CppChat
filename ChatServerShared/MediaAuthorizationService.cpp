#include "MediaAuthorizationService.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "ServicePolicy.h"
#include "const.h"
#include <openssl/crypto.h>

grpc::Status MediaAuthorizationService::AuthorizeGroup(grpc::ServerContext *context,
                                                       const message::GroupMediaRequest *request,
                                                       message::GroupMediaReply *reply)
{
  reply->set_error(ErrorCodes::GroupForbidden);
  if (context->IsCancelled() || request->uid() <= 0 || request->thread_id() <= 0 ||
      request->token().empty() || request->token().size() > 512)
    return grpc::Status::OK;
  std::string active;
  bool authenticated = false;
  for (const std::string device : {"desktop", "android"}) {
    if (RedisMgr::GetInstance()->Read(std::string(ServicePolicy::ActiveTokenPrefix) +
                                          ServicePolicy::DeviceKey(request->uid(), device),
                                      active, true) == RedisReadStatus::Found &&
        active.size() == request->token().size() &&
        CRYPTO_memcmp(active.data(), request->token().data(), active.size()) == 0) {
      authenticated = true;
      break;
    }
  }
  if (!authenticated)
    return grpc::Status::OK;
  Json::Value group;
  reply->set_error(MysqlMgr::GetInstance()->GetGroup(request->uid(), request->thread_id(), group));
  return grpc::Status::OK;
}
