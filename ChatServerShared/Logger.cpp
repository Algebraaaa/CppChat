#include "Logger.h"

#ifndef CPPCHAT_NODE_NAME
#define CPPCHAT_NODE_NAME "ChatServer"
#endif

Logger::Logger() : ServerLogging::Logger(CPPCHAT_NODE_NAME) {}

Logger &Logger::GetInstance()
{
  static Logger logger;
  return logger;
}
