#include "Logger.h"

Logger::Logger() : ServerLogging::Logger("ResourceServer") {}

Logger &Logger::GetInstance()
{
  static Logger logger;
  return logger;
}
