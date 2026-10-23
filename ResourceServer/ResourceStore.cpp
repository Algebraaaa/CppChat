#include "ResourceStore.h"
#include "Logger.h"
#include "ResourceProtocol.h"
#include "media/pcmwave.h"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/time.h>
#endif
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <hiredis/hiredis.h>
#include <json/json.h>
#include <memory>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <stdexcept>
#include <utility>
namespace fs = std::filesystem;
namespace cppchat::resource {
namespace {
using RedisConnection = std::unique_ptr<redisContext, decltype(&redisFree)>;
using RedisReply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;

RedisConnection ConnectRedis(const Settings &settings)
{
  timeval timeout{2, 0};
  RedisConnection connection(
      redisConnectWithTimeout(settings.redisHost.c_str(), settings.redisPort, timeout), &redisFree);
  if (!connection || connection->err)
    return {nullptr, &redisFree};
  if (redisSetTimeout(connection.get(), timeout) != REDIS_OK)
    return {nullptr, &redisFree};
  if (!settings.redisPassword.empty()) {
    RedisReply reply(static_cast<redisReply *>(redisCommand(connection.get(), "AUTH %b",
                                                            settings.redisPassword.data(),
                                                            settings.redisPassword.size())),
                     &freeReplyObject);
    if (!reply || reply->type == REDIS_REPLY_ERROR)
      return {nullptr, &redisFree};
  }
  return connection;
}

// hiredis context 不能在线程间共享。每个业务线程保留自己的连接，
// 每次请求仍查 Redis 中的当前 token/TTL；断线后下一次请求重新连接并 AUTH。
RedisConnection &WorkerRedis(const Settings &settings)
{
  thread_local RedisConnection connection(nullptr, &redisFree);
  if (!connection || connection->err)
    connection = ConnectRedis(settings);
  return connection;
}

bool Authenticated(const Settings &settings, int uid, const std::string &token)
{
  if (uid <= 0 || token.empty())
    return false;
  auto &connection = WorkerRedis(settings);
  if (!connection)
    return false;
  const std::string key = "resource_token_" + std::to_string(uid);
  const char *script =
      "if redis.call('ttl',KEYS[1])>0 then return redis.call('get',KEYS[1]) end; return nil";
  RedisReply reply(static_cast<redisReply *>(redisCommand(connection.get(), "EVAL %s 1 %b", script,
                                                          key.data(), key.size())),
                   &freeReplyObject);
  return reply && reply->type == REDIS_REPLY_STRING &&
         reply->len == static_cast<int>(token.size()) &&
         CRYPTO_memcmp(reply->str, token.data(), token.size()) == 0;
}

bool MarkReady(const Settings &settings, const std::string &id, int sender, int recipient,
               std::uint64_t size, int thread = 0, int type = 1)
{
  auto &connection = WorkerRedis(settings);
  if (!connection)
    return false;
  const std::string key = "resource_ready_" + id;
  const std::string scope = thread > 0 ? "group:" + std::to_string(sender) + ":" +
                                             std::to_string(thread) + ":" + std::to_string(size)
                                       : std::to_string(sender) + ":" + std::to_string(recipient) +
                                             ":" + std::to_string(size);
  const std::string value = (type == 3 ? "audio:" : "") + scope;
  RedisReply reply(static_cast<redisReply *>(redisCommand(connection.get(), "SET %s %b",
                                                          key.c_str(), value.data(), value.size())),
                   &freeReplyObject);
  return reply && reply->type == REDIS_REPLY_STATUS && std::string(reply->str, reply->len) == "OK";
}

bool ValidId(const std::string &id)
{
  return id.size() == 32 &&
         std::all_of(id.begin(), id.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}

bool ValidWave(const fs::path &path)
{
  std::ifstream input(path, std::ios::binary);
  std::string header(cppchat::media::WaveHeaderBytes, '\0');
  return input.read(header.data(), header.size()) &&
         cppchat::media::WaveDurationMs(header, fs::file_size(path)) > 0;
}

std::string Sha256(const fs::path &path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),
                                                                  &EVP_MD_CTX_free);
  if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
    return {};
  std::array<char, 65536> buffer{};
  while (file) {
    file.read(buffer.data(), buffer.size());
    const auto count = file.gcount();
    if (count > 0 &&
        EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(count)) != 1)
      return {};
  }
  if (file.bad())
    return {};
  unsigned char digest[EVP_MAX_MD_SIZE]{};
  unsigned int digestSize = 0;
  if (EVP_DigestFinal_ex(context.get(), digest, &digestSize) != 1)
    return {};
  const char digits[] = "0123456789abcdef";
  std::string hex;
  for (unsigned int index = 0; index < digestSize; ++index) {
    hex += digits[digest[index] >> 4];
    hex += digits[digest[index] & 15];
  }
  return hex;
}

std::string DecodeBase64(const std::string &encoded)
{
  if (encoded.empty() || encoded.size() % 4 != 0 || encoded.size() > ((kChunkSize + 2) / 3) * 4)
    return {};
  std::string decoded(encoded.size() * 3 / 4, '\0');
  const int size = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(decoded.data()),
                                   reinterpret_cast<const unsigned char *>(encoded.data()),
                                   static_cast<int>(encoded.size()));
  if (size < 0)
    return {};
  std::size_t padding = 0;
  if (encoded.back() == '=')
    ++padding;
  if (encoded.size() > 1 && encoded[encoded.size() - 2] == '=')
    ++padding;
  decoded.resize(static_cast<std::size_t>(size) - padding);
  return decoded;
}

std::string EncodeBase64(const std::string &bytes)
{
  std::string encoded(((bytes.size() + 2) / 3) * 4, '\0');
  EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()),
                  reinterpret_cast<const unsigned char *>(bytes.data()),
                  static_cast<int>(bytes.size()));
  return encoded;
}

} // namespace

ResourceStore::ResourceStore(Settings settings)
    : settings_(std::move(settings)), authorizer_(settings_)
{
  fs::create_directories(settings_.directory);
  fs::create_directories(settings_.directory / "groups");
  fs::create_directories(settings_.directory / "audio");
}

Json::Value ResourceStore::Handle(std::uint16_t id, const Json::Value &request)
{
  const int uid = request.get("uid", 0).asInt();
  const std::string token = request.get("token", "").asString();
  if (!Authenticated(settings_, uid, token)) {
    LOG_WARNING("Resource authentication rejected: request=", id, ", uid=", uid);
    return Error("invalid token");
  }
  const std::string fileId = request.get("file_id", "").asString();
  if (!ValidId(fileId)) {
    LOG_WARNING("Resource request has invalid file id: request=", id, ", uid=", uid);
    return Error("invalid file id");
  }
  // Different images may progress concurrently, but chunks/metadata for the
  // same file stay serialized, including case aliases on Windows.
  std::string lockKey = fileId;
  std::transform(lockKey.begin(), lockKey.end(), lockKey.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::lock_guard<std::mutex> lock(
      fileLocks_[std::hash<std::string>{}(lockKey) % fileLocks_.size()]);
  Json::Value reply;
  if (id == kUploadInitRequest)
    reply = UploadInit(uid, fileId, request);
  else if (id == kUploadChunkRequest)
    reply = UploadChunk(uid, fileId, request);
  else if (id == kDownloadRequest)
    reply = Download(uid, fileId, request);
  else
    reply = Error("unknown request");
  if (!reply.get("ok", false).asBool()) {
    const auto reason = reply.get("error", "unknown error").asString();
    if (reason == "offset mismatch")
      LOG_DEBUG("Resource offset mismatch: request=", id, ", uid=", uid,
                ", file=", fileId.substr(0, 8));
    else
      LOG_WARNING("Resource request rejected: request=", id, ", uid=", uid,
                  ", file=", fileId.substr(0, 8), ", reason=", reason);
  }
  return reply;
}

fs::path ResourceStore::MetaPath(const std::string &id) const
{
  const auto audio = settings_.directory / "audio" / (id + ".json");
  if (fs::exists(audio))
    return audio;
  const auto group = settings_.directory / "groups" / (id + ".json");
  if (fs::exists(group))
    return group;
  return settings_.directory / (id + ".json");
}

fs::path ResourceStore::PartPath(const std::string &id) const
{
  return MetaPath(id).parent_path() / (id + ".part");
}

fs::path ResourceStore::DonePath(const std::string &id) const
{
  return MetaPath(id).parent_path() / (id + ".bin");
}

Json::Value ResourceStore::ReadMeta(const std::string &id) const
{
  std::ifstream file(MetaPath(id), std::ios::binary);
  Json::Value meta;
  if (file)
    file >> meta;
  return meta;
}

void ResourceStore::WriteMeta(const std::string &id, const Json::Value &meta)
{
  const auto directory = meta.get("media_type", 1).asInt() == 3 ? settings_.directory / "audio"
                         : meta.get("thread_id", 0).asInt() > 0 ? settings_.directory / "groups"
                                                                : settings_.directory;
  std::ofstream file(directory / (id + ".json"), std::ios::binary | std::ios::trunc);
  if (!file)
    throw std::runtime_error("cannot write metadata");
  file << meta;
  if (!file)
    throw std::runtime_error("metadata write failed");
}

Json::Value ResourceStore::UploadInit(int uid, const std::string &id, const Json::Value &request)
{
  const int recipient = request.get("recipient", 0).asInt();
  const int thread = request.get("thread_id", 0).asInt();
  const int type = request.get("media_type", 1).asInt();
  const auto size = request.get("size", Json::UInt64(0)).asUInt64();
  const std::string sha = request.get("sha256", "").asString();
  if (thread < 0 || (thread > 0 ? recipient != 0 : (recipient <= 0 || recipient == uid)) ||
      (type != 1 && type != 3) || size == 0 || size > kMaximumImageSize ||
      (type == 3 && (size < cppchat::media::WaveHeaderBytes + cppchat::media::MinPcmBytes ||
                     size > cppchat::media::WaveHeaderBytes + cppchat::media::MaxPcmBytes)) ||
      sha.size() != 64 ||
      !std::all_of(sha.begin(), sha.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; }))
    return Error("invalid image metadata");
  if (thread > 0 && !authorizer_.GroupMember(uid, request["token"].asString(), thread))
    return Error("group access denied");

  Json::Value meta = ReadMeta(id);
  if (meta.isNull()) {
    meta["uid"] = uid;
    meta["recipient"] = recipient;
    meta["thread_id"] = thread;
    meta["media_type"] = type;
    meta["size"] = Json::UInt64(size);
    meta["sha256"] = sha;
    WriteMeta(id, meta);
  } else if (meta.get("uid", 0).asInt() != uid || meta.get("recipient", 0).asInt() != recipient ||
             meta.get("thread_id", 0).asInt() != thread ||
             meta.get("media_type", 1).asInt() != type ||
             meta.get("size", Json::UInt64(0)).asUInt64() != size ||
             meta.get("sha256", "").asString() != sha) {
    return Error("file id belongs to another upload");
  }

  bool complete = fs::exists(DonePath(id));
  if (!complete && fs::exists(PartPath(id)) && fs::file_size(PartPath(id)) >= size) {
    // 写完最后一块、改名前退出时，恢复初始化仍能确认完整文件。
    if (fs::file_size(PartPath(id)) == size && Sha256(PartPath(id)) == sha &&
        (type != 3 || ValidWave(PartPath(id)))) {
      fs::rename(PartPath(id), DonePath(id));
      complete = true;
    } else {
      fs::remove(PartPath(id));
    }
  }
  if (complete && type == 3 && !ValidWave(DonePath(id)))
    return Error("invalid PCM wave");
  if (complete && !MarkReady(settings_, id, uid, recipient, size, thread, type))
    return Error("Redis is unavailable");
  const std::uint64_t offset =
      complete ? size : (fs::exists(PartPath(id)) ? fs::file_size(PartPath(id)) : 0);
  LOG_INFO("Resource upload ready: uid=", uid, ", recipient=", recipient,
           ", file=", id.substr(0, 8), ", offset=", offset, ", size=", size,
           ", complete=", complete);
  Json::Value reply;
  reply["ok"] = true;
  reply["offset"] = Json::UInt64(offset);
  reply["complete"] = complete;
  reply["upload_window"] = 4;
  return reply;
}

Json::Value ResourceStore::UploadChunk(int uid, const std::string &id, const Json::Value &request)
{
  const Json::Value meta = ReadMeta(id);
  if (meta.isNull() || meta.get("uid", 0).asInt() != uid)
    return Error("upload not initialized");
  const int thread = meta.get("thread_id", 0).asInt();
  if (thread > 0 && !authorizer_.GroupMember(uid, request["token"].asString(), thread))
    return Error("group access denied");
  const std::uint64_t size = meta["size"].asUInt64();
  const int type = meta.get("media_type", 1).asInt();
  if (fs::exists(DonePath(id))) {
    if (!MarkReady(settings_, id, uid, meta["recipient"].asInt(), size, thread, type))
      return Error("Redis is unavailable");
    Json::Value reply;
    reply["ok"] = true;
    reply["offset"] = Json::UInt64(size);
    reply["complete"] = true;
    return reply;
  }
  const std::uint64_t offset = fs::exists(PartPath(id)) ? fs::file_size(PartPath(id)) : 0;
  if (request.get("offset", Json::UInt64(0)).asUInt64() != offset) {
    Json::Value reply = Error("offset mismatch");
    reply["offset"] = Json::UInt64(offset);
    return reply;
  }
  const std::string bytes = DecodeBase64(request.get("data", "").asString());
  if (bytes.empty() || bytes.size() > kChunkSize || offset + bytes.size() > size)
    return Error("invalid chunk");
  std::ofstream file(PartPath(id), std::ios::binary | std::ios::app);
  if (!file)
    return Error("cannot open upload file");
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  file.close();
  if (!file)
    return Error("upload write failed");

  const std::uint64_t nextOffset = offset + bytes.size();
  bool complete = nextOffset == size;
  if (complete) {
    if (Sha256(PartPath(id)) != meta["sha256"].asString()) {
      // 校验失败不能保留满长度的坏断点，否则后续重试永远无法重新发送。
      fs::remove(PartPath(id));
      return Error("image checksum mismatch");
    }
    if (type == 3 && !ValidWave(PartPath(id))) {
      fs::remove(PartPath(id));
      return Error("invalid PCM wave");
    }
    fs::rename(PartPath(id), DonePath(id));
    if (!MarkReady(settings_, id, uid, meta["recipient"].asInt(), size, thread, type))
      return Error("Redis is unavailable");
    LOG_INFO("Resource upload completed: uid=", uid, ", recipient=", meta["recipient"].asInt(),
             ", file=", id.substr(0, 8), ", bytes=", size);
  }
  Json::Value reply;
  reply["ok"] = true;
  reply["offset"] = Json::UInt64(nextOffset);
  reply["complete"] = complete;
  return reply;
}

Json::Value ResourceStore::Download(int uid, const std::string &id, const Json::Value &request)
{
  const Json::Value meta = ReadMeta(id);
  if (meta.isNull())
    return Error("access denied");
  const int thread = meta.get("thread_id", 0).asInt();
  if (thread > 0 ? !authorizer_.GroupMember(uid, request["token"].asString(), thread)
                 : (meta.get("uid", 0).asInt() != uid && meta.get("recipient", 0).asInt() != uid))
    return Error("access denied");
  if (!fs::exists(DonePath(id)))
    return Error("image is not ready");
  const std::uint64_t size = meta["size"].asUInt64();
  const std::uint64_t offset = request.get("offset", Json::UInt64(0)).asUInt64();
  if (offset > size)
    return Error("invalid download offset");
  std::ifstream file(DonePath(id), std::ios::binary);
  if (!file)
    return Error("cannot read image");
  file.seekg(static_cast<std::streamoff>(offset));
  std::string bytes(static_cast<std::size_t>(std::min<std::uint64_t>(kChunkSize, size - offset)),
                    '\0');
  file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  bytes.resize(static_cast<std::size_t>(file.gcount()));
  Json::Value reply;
  reply["ok"] = true;
  reply["offset"] = Json::UInt64(offset + bytes.size());
  reply["size"] = Json::UInt64(size);
  reply["sha256"] = meta["sha256"];
  reply["data"] = EncodeBase64(bytes);
  reply["complete"] = offset + bytes.size() == size;
  if (reply["complete"].asBool())
    LOG_INFO("Resource download completed: uid=", uid, ", file=", id.substr(0, 8),
             ", bytes=", size);
  return reply;
}

} // namespace cppchat::resource
