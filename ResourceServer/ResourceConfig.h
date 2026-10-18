#pragma once
#include <filesystem>
#include <string>
#include <vector>
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
  std::vector<unsigned short> mediaAuthorizationPorts;
};
} // namespace cppchat::resource
