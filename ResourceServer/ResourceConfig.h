#pragma once
#include <filesystem>
#include <string>
namespace cppchat::resource {
struct Settings {
  Settings();
  std::string listen;
  unsigned short port;
  std::filesystem::path directory;
  std::string redisHost;
  int redisPort;
  std::string redisPassword;
  int workers;
  int queueLimit;
  int maxConnections;
  int timeoutSeconds;
};
} // namespace cppchat::resource
