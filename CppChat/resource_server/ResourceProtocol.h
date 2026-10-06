#pragma once
#include <cstddef>
#include <cstdint>
#include <json/value.h>
#include <string>
namespace cppchat::resource {
inline constexpr std::size_t kChunkSize = 32 * 1024;
inline constexpr std::size_t kMaximumImageSize = 20 * 1024 * 1024;
inline constexpr std::size_t kMaximumPacketSize = 60 * 1024;
inline constexpr std::uint16_t kUploadInitRequest = 1050;
inline constexpr std::uint16_t kUploadInitResponse = 1051;
inline constexpr std::uint16_t kUploadChunkRequest = 1052;
inline constexpr std::uint16_t kUploadChunkResponse = 1053;
inline constexpr std::uint16_t kDownloadRequest = 1054;
inline constexpr std::uint16_t kDownloadResponse = 1055;

inline Json::Value Error(const std::string &message)
{
  Json::Value reply;
  reply["ok"] = false;
  reply["error"] = message;
  return reply;
}

} // namespace cppchat::resource
