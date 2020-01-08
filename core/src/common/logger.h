#pragma once
#include "stdafx.h"

#include <stdarg.h>
#include <string>

using namespace std;

#define LOGDEBUG(format, ...) Logger::Debug(format, ##__VA_ARGS__)
#define LOGINFO(format, ...) Logger::Info(format, ##__VA_ARGS__)
#define LOGWARNING(format, ...) Logger::Warning(format, ##__VA_ARGS__)
#define LOGERROR(format, ...) Logger::Error(format, ##__VA_ARGS__)

class Logger
{
public:
	static void Debug(string fmt, ...);
	static void Info(string fmt, ...);
	static void Warning(string fmt, ...);
	static void Error(string fmt, ...);

protected:
	static void Out(string fmt, va_list args);
};