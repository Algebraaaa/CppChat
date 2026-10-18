#include "ResourceAuthorizer.h"
#include <chrono>
#include <grpcpp/grpcpp.h>

namespace cppchat::resource {
ResourceAuthorizer::ResourceAuthorizer(const Settings &settings)
{
  grpc::ChannelArguments arguments;
  arguments.SetMaxReceiveMessageSize(1024);
  for (auto port : settings.mediaAuthorizationPorts)
    stubs_.push_back(message::MediaAuthorization::NewStub(grpc::CreateCustomChannel(
        "127.0.0.1:" + std::to_string(port), grpc::InsecureChannelCredentials(), arguments)));
}

bool ResourceAuthorizer::GroupMember(int uid, const std::string &token, int thread) const
{
  message::GroupMediaRequest request;
  request.set_uid(uid);
  request.set_token(token);
  request.set_thread_id(thread);
  for (const auto &stub : stubs_) {
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(1));
    message::GroupMediaReply reply;
    const auto status = stub->AuthorizeGroup(&context, request, &reply);
    if (status.ok())
      return reply.error() == 0; // Never retry a membership denial against another node.
  }
  return false;
}
} // namespace cppchat::resource
