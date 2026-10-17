#pragma once

#include "const.h"
#include <json/json.h>
#include <string>
#include <utility>
#include <vector>

// Keep these wire limits aligned with network/loginsnapshot.h and the protocol contract.
namespace LoginSnapshot {
inline constexpr std::size_t MaxChunks = 256;
inline constexpr std::size_t MaxBytes = 4 * 1024 * 1024;

// Preflight every frame before sending any part of the authenticated snapshot.
// Small replies preserve the original single-frame response, including old clients.
inline bool BuildFrames(const Json::Value &response, bool allow_chunks,
                        std::vector<std::string> &frames)
{
  frames.clear();
  const auto single = response.toStyledString();
  if (single.size() <= MAX_LENGTH) {
    frames.push_back(single);
    return true;
  }
  if (!allow_chunks || single.size() > MaxBytes)
    return false;

  Json::Value base = response;
  base["apply_list"] = Json::Value(Json::arrayValue);
  base["friend_list"] = Json::Value(Json::arrayValue);
  base["login_chunk_more"] = false; // false is one byte longer than true.
  base["login_chunk"] = 0;
  if (base.toStyledString().size() > MAX_LENGTH)
    return false;

  auto current = base;
  std::size_t total_bytes = 0;
  const auto append_frame = [&](bool more) {
    current["login_chunk_more"] = more;
    const auto body = current.toStyledString();
    if (frames.size() >= MaxChunks || body.size() > MAX_LENGTH ||
        total_bytes + body.size() > MaxBytes)
      return false;
    frames.push_back(body);
    total_bytes += body.size();
    current = base;
    current["login_chunk"] = static_cast<int>(frames.size());
    return true;
  };

  for (const auto *list : {"apply_list", "friend_list"}) {
    for (const auto &item : response[list]) {
      Json::Value candidate = current;
      candidate[list].append(item);
      if (candidate.toStyledString().size() > MAX_LENGTH) {
        if ((current["apply_list"].empty() && current["friend_list"].empty()) ||
            !append_frame(true)) {
          frames.clear();
          return false;
        }
        candidate = current;
        candidate[list].append(item);
        if (candidate.toStyledString().size() > MAX_LENGTH) {
          frames.clear();
          return false;
        }
      }
      current = std::move(candidate);
    }
  }
  if (!append_frame(false)) {
    frames.clear();
    return false;
  }
  return true;
}
} // namespace LoginSnapshot
