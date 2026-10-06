#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "Logger.h"

#include <memory>

namespace {
struct ReplyDeleter {
  void operator()(redisReply *reply) const {
    if (reply)
      freeReplyObject(reply);
  }
};
using ReplyPtr = std::unique_ptr<redisReply, ReplyDeleter>;

class RedisConnectionGuard {
public:
  RedisConnectionGuard(RedisConPool *pool, redisContext *context)
      : pool_(pool), context_(context) {}
  ~RedisConnectionGuard() { pool_->ReturnConnection(context_); }
  redisContext *Get() const { return context_; }

private:
  RedisConPool *pool_;
  redisContext *context_;
};
bool IsStatusOk(const redisReply *reply) {
  return reply && reply->type == REDIS_REPLY_STATUS && reply->str &&
         std::string(reply->str, reply->len) == "OK";
}
int PositiveNumber(const std::string &text, int fallback) {
  try {
    std::size_t consumed = 0;
    const int value = std::stoi(text, &consumed);
    return consumed == text.size() && value > 0 ? value : fallback;
  } catch (...) {
    return fallback;
  }
}
} // namespace

RedisMgr::RedisMgr() {
  const auto config = ConfigMgr::GetInstance()["Redis"];
  _connection_pool = std::make_unique<RedisConPool>(
      PositiveNumber(config.GetValue("PoolSize", "5"), 5),
      config.GetValue("Host", "127.0.0.1"),
      PositiveNumber(config.GetValue("Port", "6379"), 6379),
      config.GetValue("Passwd"));
  if (!_connection_pool->IsAvailable())
    LOG_WARNING("StatusServer Redis backend is unavailable.");
}
RedisMgr::~RedisMgr() { _connection_pool->Close(); }

RedisReadStatus RedisMgr::Read(const std::string &key, std::string &value,
                               bool require_expiry) {
  value.clear();
  RedisConnectionGuard connection(_connection_pool.get(),
                                  _connection_pool->GetConnection());
  if (!connection.Get())
    return RedisReadStatus::Error;
  const char *script = "if redis.call('ttl',KEYS[1])>0 then return "
                       "redis.call('get',KEYS[1]) end; return nil";
  ReplyPtr reply(static_cast<redisReply *>(
      require_expiry
          ? redisCommand(connection.Get(), "EVAL %s 1 %b", script, key.data(),
                         key.size())
          : redisCommand(connection.Get(), "GET %b", key.data(), key.size())));
  if (!reply)
    return RedisReadStatus::Error;
  if (reply->type == REDIS_REPLY_NIL)
    return RedisReadStatus::Missing;
  if (reply->type != REDIS_REPLY_STRING || !reply->str)
    return RedisReadStatus::Error;
  value.assign(reply->str, reply->len);
  return RedisReadStatus::Found;
}

bool RedisMgr::SetEx(const std::string &key, const std::string &value,
                     int seconds) {
  if (seconds <= 0)
    return false;
  RedisConnectionGuard connection(_connection_pool.get(),
                                  _connection_pool->GetConnection());
  if (!connection.Get())
    return false;
  ReplyPtr reply(static_cast<redisReply *>(
      redisCommand(connection.Get(), "SET %b %b EX %d", key.data(), key.size(),
                   value.data(), value.size(), seconds)));
  return IsStatusOk(reply.get());
}
