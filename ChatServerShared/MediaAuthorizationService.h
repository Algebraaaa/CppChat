#pragma once
#include "message.grpc.pb.h"

// Read-only authority; membership policy and transactional SQL stay in MysqlMgr.
class MediaAuthorizationService final : public message::MediaAuthorization::Service {
public:
  grpc::Status AuthorizeGroup(grpc::ServerContext *context,
                              const message::GroupMediaRequest *request,
                              message::GroupMediaReply *reply) override;
};
