#pragma once
#include "ResourceConfig.h"
#include "message.grpc.pb.h"
#include <memory>
#include <vector>

namespace cppchat::resource {
class ResourceAuthorizer {
public:
  explicit ResourceAuthorizer(const Settings &settings);
  bool GroupMember(int uid, const std::string &token, int thread) const;

private:
  std::vector<std::unique_ptr<message::MediaAuthorization::Stub>> stubs_;
};
} // namespace cppchat::resource
