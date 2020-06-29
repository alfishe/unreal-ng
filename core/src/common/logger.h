#pragma once
#include "stdafx.h"

#include <stdarg.h>
#include <string>

using namespace std;

#ifndef _CODE_UNDER_TEST

// Write all Debug / Info logs if not under unit testing / benchmarking
#define LOGDEBUG(format, ...) Logger::Debug(format, ##__VA_ARGS__)
#define LOGINFO(format, ...) Logger::Info(format, ##__VA_ARGS__)
#define LOGWARNING(format, ...) Logger::Warning(format, ##__VA_ARGS__)
#define LOGERROR(format, ...) Logger::Error(format, ##__VA_ARGS__)
#define LOGEMPTY(...) Logger::EmptyLine(##__VA_ARGS__)

#else

// No Debug / Info for unit tests and benchmarks
#define LOGDEBUG(format, ...)
#define LOGINFO(format, ...)
#define LOGWARNING(format, ...) Logger::Warning(format, ##__VA_ARGS__)
#define LOGERROR(format, ...) Logger::Error(format, ##__VA_ARGS__)
#define LOGEMPTY(...) Logger::EmptyLine(##__VA_ARGS__)

#endif


class Logger
{
public:
	static void Debug(string fmt, ...);
	static void Info(string fmt, ...);
	static void Warning(string fmt, ...);
	static void Error(string fmt, ...);
	static void EmptyLine();

protected:
	static void Out(string fmt, va_list args);
};