#include "Logger.h"

Logger::Logger() : ServerLogging::Logger("GateServer") {}

Logger &Logger::GetInstance()
{
    static Logger logger;
    return logger;
}
