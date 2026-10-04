// logger-stub.cpp - ModuleLogger out-of-line members the core module references
// (it never logs here: the drivers pass no logger). Keeps the drivers free of the
// whole core library.
#include "common/modulelogger.h"

bool ModuleLogger::IsLoggingEnabledForLogLevel(PlatformModulesEnum, uint16_t, LoggerLevel)
{
    return false;
}
void ModuleLogger::LogMessage(LoggerLevel, PlatformModulesEnum, uint16_t, const char*, ...) {}
void ModuleLogger::LogMessage(LoggerLevel, PlatformModulesEnum, uint16_t, const std::string, ...) {}
