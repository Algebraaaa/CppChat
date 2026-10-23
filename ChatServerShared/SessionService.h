#pragma once
#include <json/value.h>
#include <memory>
#include <optional>
#include <string>
class CSession;

// Complete authentication/session workflow; no TCP parsing or response writes.
class SessionService {
public:
  static Json::Value Login(const std::shared_ptr<CSession> &session, int uid,
                           const std::string &token, const std::string &device = "desktop");
  static Json::Value Heartbeat(const std::shared_ptr<CSession> &session,
                               std::optional<int> snapshotId);
};
