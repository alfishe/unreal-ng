#pragma once
#include "stdafx.h"

#include <ostream>
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
    /// region <Fields>
protected:
    static volatile bool g_mute;
    /// endregion </Fields>

public:
	static void Debug(string fmt, ...);
	static void Info(string fmt, ...);
	static void Warning(string fmt, ...);
	static void Error(string fmt, ...);
	static void EmptyLine();

	// Flow control
	static void MuteSilent() { g_mute = true; }
	static void UnmuteSilent() { g_mute = false; }

	static void Mute()
	{
	    if (!g_mute)
	    {
            fflush(stdout);
            EmptyLine();
            OutEnriched("Log muted");
            EmptyLine();
            fflush(stdout);

            g_mute = true;
        }
	}

	static void Unmute()
	{
	    if (g_mute)
	    {
            g_mute = false;

            Out("...");
            EmptyLine();
            OutEnriched("Log unmuted");
            EmptyLine();
            EmptyLine();
            fflush(stdout);
        }
	}

protected:
    static void OutEnriched(const char* fmt);
    static void OutEnriched(const char* fmt, va_list args);
	static void OutEnriched(string fmt, va_list args);
	static void Out(const char* value);
};