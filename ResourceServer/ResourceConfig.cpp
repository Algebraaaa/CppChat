#include "ResourceConfig.h"
#include <cstdlib>
#include <stdexcept>
namespace {
std::string Environment(const char *name, const std::string &fallback)
{
  const char *value = std::getenv(name);
  return value && *value ? value : fallback;
}

int BoundedSetting(const char *name, int fallback, int maximum)
{
  const std::string text = Environment(name, std::to_string(fallback));
  std::size_t consumed = 0;
  const int value = std::stoi(text, &consumed);
  if (consumed != text.size() || value < 1 || value > maximum)
    throw std::invalid_argument(std::string("Invalid setting: ") + name);
  return value;
}

} // namespace
namespace cppchat::resource {
Settings::Settings()
    : listen(Environment("CPPCHAT_RESOURCE_LISTEN", "127.0.0.1")),
      port(static_cast<unsigned short>(BoundedSetting("CPPCHAT_RESOURCE_PORT", 8093, 65535))),
      directory(Environment("CPPCHAT_RESOURCE_DIR", "resource_data")),
      redisHost(Environment("CPPCHAT_REDIS_HOST", "127.0.0.1")),
      redisPort(BoundedSetting("CPPCHAT_REDIS_PORT", 6380, 65535)),
      redisPassword(Environment("CPPCHAT_REDIS_PASSWORD", "")),
      workers(BoundedSetting("CPPCHAT_RESOURCE_WORKERS", 4, 16)),
      queueLimit(BoundedSetting("CPPCHAT_RESOURCE_QUEUE_LIMIT", 256, 10000)),
      maxConnections(BoundedSetting("CPPCHAT_RESOURCE_MAX_CONNECTIONS", 128, 10000)),
      timeoutSeconds(BoundedSetting("CPPCHAT_RESOURCE_TIMEOUT_SECONDS", 30, 3600))
{
}
} // namespace cppchat::resource
