#pragma once

#include "ServerLogger.h"

// 保留业务层现有接口；服务身份由各入口的 Logger.cpp 提供
using LogLevel = ServerLogging::Level;
class Logger final : public ServerLogging::Logger {
public:
  static Logger &GetInstance();

private:
  Logger();
};

#define LOG_DEBUG(...) (::Logger::GetInstance().Debug(__VA_ARGS__))
#define LOG_INFO(...) (::Logger::GetInstance().Info(__VA_ARGS__))
#define LOG_WARNING(...) (::Logger::GetInstance().Warning(__VA_ARGS__))
#define LOG_ERROR(...) (::Logger::GetInstance().Error(__VA_ARGS__))
