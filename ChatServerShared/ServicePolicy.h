#pragma once
#include <string>

// Shared wire/storage policy for StatusServer and both chat nodes.
namespace ServicePolicy {
inline constexpr char PendingTokenPrefix[] = "utoken_";
inline constexpr char ActiveTokenPrefix[] = "resource_token_";
inline constexpr char NodeHealthPrefix[] = "chat_node_";
inline constexpr int PendingTokenSeconds = 300;
inline constexpr int ActiveSessionSeconds = 3600;
inline constexpr int NodeLeaseSeconds = 30;
// Existing clients occupy the desktop slot; Android has an independent lease.
inline bool ValidDevice(const std::string &device)
{
  return device == "desktop" || device == "android";
}
inline std::string DeviceKey(int uid, const std::string &device)
{
  return std::to_string(uid) + (device == "android" ? ":android" : "");
}
} // namespace ServicePolicy
