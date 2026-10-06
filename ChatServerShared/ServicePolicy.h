#pragma once

// Shared wire/storage policy for StatusServer and both chat nodes.
namespace ServicePolicy {
inline constexpr char PendingTokenPrefix[] = "utoken_";
inline constexpr char ActiveTokenPrefix[] = "resource_token_";
inline constexpr char NodeHealthPrefix[] = "chat_node_";
inline constexpr int PendingTokenSeconds = 300;
inline constexpr int ActiveSessionSeconds = 3600;
inline constexpr int NodeLeaseSeconds = 30;
} // namespace ServicePolicy
