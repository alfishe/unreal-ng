#include "logger.h"

#include "stdafx.h"

/// region <Fields>

std::atomic<bool> Logger::g_mute{false};
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
    // Every thread logs (the emulator threads, the WebAPI, the main one): the time and the text in locals. A static
    // tm shared by threads let one read it while another's localtime_s had set every field to -1 first (the UCRT
    // does that before it converts), and MSVC's strftime takes an hour of -1 as an invalid parameter: a fast fail,
    // 0xC0000409, of the whole process
    struct tm tm_storage = {};
    struct timeval tv;
    char buffer[1024];

    /// region <Print timestamp>

    gettimeofday(&tv, NULL);

    time_t rawtime = tv.tv_sec;
#if defined(_WIN32) && defined(_MSC_VER)
    if (localtime_s(&tm_storage, &rawtime) != 0)
        tm_storage = {};
#elif defined(_WIN32)
    // MinGW: the CRT's localtime keeps its result per thread
    if (const struct tm* local = localtime(&rawtime))
        tm_storage = *local;
#else
    localtime_r(&rawtime, &tm_storage);
#endif
    const struct tm* tm_info = &tm_storage;

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