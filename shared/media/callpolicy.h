#pragma once

// One bounded liveness budget for desktop, Android and the server's Redis lease.
// A stalled frame is disposable; authentication and explicit hangup are not.
namespace cppchat::media::CallPolicy {
inline constexpr int HeartbeatMs = 3000;
inline constexpr int RecoveringMs = 6000;
inline constexpr int TimeoutMs = 30000;
inline constexpr int LeaseMs = 45000;
} // namespace cppchat::media::CallPolicy
