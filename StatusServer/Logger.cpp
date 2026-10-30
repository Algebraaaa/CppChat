#include "Logger.h"

Logger::Logger() : ServerLogging::Logger("StatusServer") {}

Logger &Logger::GetInstance()
{
  static Logger logger;
  return logger;
}
