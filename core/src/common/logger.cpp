#include "stdafx.h"

#include "logger.h"

/// region <Fields>

volatile bool Logger::g_mute = false;
int Logger::_stdout = -1;
int Logger::_stderr = -1;
int Logger::_outFile = -1;
int Logger::_errFile = -1;

/// endregion </Fields>


void Logger::Debug(string fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	string format = "Debug: " + fmt + "\n";
    OutEnriched(format, args);

	va_end(args);
}

void Logger::Info(string fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	string format = "Info: " + fmt + "\n";
    OutEnriched(format, args);

	va_end(args);
}

void Logger::Warning(string fmt, ...)
{
    // Ensure log is not muted
    Logger::UnmuteSilent();

	va_list args;
	va_start(args, fmt);

	string format = "Warning: " + fmt + "\n";
    OutEnriched(format, args);

	va_end(args);
}

void Logger::Error(string fmt, ...)
{
    // Ensure log is not muted
    Logger::UnmuteSilent();

	va_list args;
	va_start(args, fmt);

	string format = "Error: " + fmt + "\n";
    OutEnriched(format, args);

	va_end(args);
}

void Logger::EmptyLine()
{
    static const char linefeed[] = "\n";
    Out(linefeed);
}

void Logger::OutEnriched(const char* fmt)
{
    va_list emptyArgs;

    string format = fmt;
    OutEnriched(format, emptyArgs);
}

void Logger::OutEnriched(const char* fmt, va_list args)
{
    string format = fmt;
    OutEnriched(format, args);
}

void Logger::OutEnriched(string fmt, va_list args)
{
    size_t time_len = 0;
	struct tm *tm_info;
	struct timeval tv;
	static char buffer[1024];

    /// region <Print timestamp>

	gettimeofday(&tv, NULL);

#if defined __GNUC__
    time_t time;
    tm_info = localtime(&time);
#elif defined __APPLE__
    tm_info = localtime(&tv);
#endif

	time_len += strftime(buffer, sizeof(buffer), "[%H:%M:%S", tm_info);

#if defined _WIN32 && defined MSVC
    time_len += snprintf(buffer + time_len, sizeof(buffer) - time_len,".%03lld.%03lld] ", tv.tv_usec / 1000, tv.tv_usec % 1000);
#else
	time_len += snprintf(buffer + time_len, sizeof(buffer) - time_len,".%03d.%03d] ", tv.tv_usec / 1000, tv.tv_usec % 1000);
#endif

	Out(buffer);

	/// endregion </Print timestamp>

	/// region <Print formatted value>

	vsnprintf(buffer, sizeof(buffer), fmt.c_str(), args);
	Out(buffer);

	/// endregion </Print formatted value>
}

/// Print to stdout
/// \param value
void Logger::Out(const char* value)
{
    // If unmuted - pass value to stdout
    //if (!g_mute)
    //    fprintf(stdout, "%s", value);
}