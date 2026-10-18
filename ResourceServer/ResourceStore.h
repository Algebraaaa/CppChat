#pragma once
#include "ResourceAuthorizer.h"
#include "ResourceConfig.h"
#include <array>
#include <cstdint>
#include <json/value.h>
#include <mutex>
#include <string>

namespace cppchat::resource {
// File/Redis business state only. No sockets, listening or process lifetime here.
class ResourceStore {
public:
  explicit ResourceStore(Settings settings);
  Json::Value Handle(std::uint16_t id, const Json::Value &request);

private:
  std::filesystem::path MetaPath(const std::string &id) const;
  std::filesystem::path PartPath(const std::string &id) const;
  std::filesystem::path DonePath(const std::string &id) const;
  Json::Value ReadMeta(const std::string &id) const;
  void WriteMeta(const std::string &id, const Json::Value &meta);
  Json::Value UploadInit(int uid, const std::string &id, const Json::Value &request);
  Json::Value UploadChunk(int uid, const std::string &id, const Json::Value &request);
  Json::Value Download(int uid, const std::string &id, const Json::Value &request);
  Settings settings_;
  ResourceAuthorizer authorizer_;
  std::array<std::mutex, 64> fileLocks_;
};
} // namespace cppchat::resource
