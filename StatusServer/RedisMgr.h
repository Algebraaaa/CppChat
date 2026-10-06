#pragma once

#include "RedisConPool.h"
#include "Singleton.h"

#include <memory>
#include <string>

enum class RedisReadStatus { Found, Missing, Error };

// StatusServer only needs health/credential reads and expiring Token writes.
class RedisMgr : public Singleton<RedisMgr> {
  friend class Singleton<RedisMgr>;

public:
  ~RedisMgr();
  RedisReadStatus Read(const std::string &key, std::string &value,
                       bool require_expiry = false);
  bool SetEx(const std::string &key, const std::string &value, int seconds);

private:
  RedisMgr();
  std::unique_ptr<RedisConPool> _connection_pool;
};
