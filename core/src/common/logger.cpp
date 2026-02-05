#include "logger.h"

#include "stdafx.h"

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
    // Portable empty initialization
    memset(&emptyArgs, 0, sizeof(emptyArgs));

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
    struct tm* tm_info = nullptr;  // Initialize to nullptr
    struct timeval tv;
    static char buffer[1024];

    /// region <Print timestamp>

    gettimeofday(&tv, NULL);

#if defined(_WIN32) && defined(_MSC_VER)
    // MSVC: Use localtime_s (thread-safe)
    time_t rawtime = tv.tv_sec;
    static struct tm tm_storage = {};
    errno_t err = localtime_s(&tm_storage, &rawtime);
    if (err == 0)
    {
        tm_info = &tm_storage;
    }
    else
    {
        // Fallback to zeroed struct
        tm_info = &tm_storage;
    }
#elif defined(__GNUC__)
    time_t currentTime = time(nullptr);
    tm_info = localtime(&currentTime);
#endif

    // Fallback if tm_info is still nullptr
    static struct tm fallback_tm = {};
    if (tm_info == nullptr)
    {
        tm_info = &fallback_tm;
    }

    time_len += strftime(buffer, sizeof(buffer), "[%H:%M:%S", tm_info);

#if defined _WIN32 && defined _MSC_VER
    time_len +=
        snprintf(buffer + time_len, sizeof(buffer) - time_len, ".%03ld.%03ld] ", tv.tv_usec / 1000, tv.tv_usec % 1000);
#else
    time_len += snprintf(buffer + time_len, sizeof(buffer) - time_len, ".%03d.%03d] ", (int)(tv.tv_usec / 1000),
                         (int)(tv.tv_usec % 1000));
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
    if (!g_mute)
        fprintf(stdout, "%s", value);
}