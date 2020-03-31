#include "stdafx.h"

#include "logger.h"

void Logger::Debug(string fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	string format = "Debug: " + fmt + "\n";
	Out(format, args);

	va_end(args);
}

void Logger::Info(string fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	string format = "Info: " + fmt + "\n";
	Out(format, args);

	va_end(args);
}

void Logger::Warning(string fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	string format = "Warning: " + fmt + "\n";
	Out(format, args);

	va_end(args);
}

void Logger::Error(string fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	string format = "Error: " + fmt + "\n";
	Out(format, args);

	va_end(args);
}

void Logger::Out(string fmt, va_list args)
{
	//region Print timestanp
    size_t time_len = 0;
	struct tm *tm_info;
	struct timeval tv;
	static char buffer[100];

	gettimeofday(&tv, NULL);
	tm_info = localtime(&tv.tv_sec);
	time_len += strftime(buffer, sizeof(buffer), "[%H:%M:%S", tm_info);
#ifdef _WIN32
    time_len += snprintf(buffer + time_len, sizeof(buffer) - time_len,".%03lld.%03lld] ", tv.tv_usec / 1000, tv.tv_usec % 1000);
#else
	time_len += snprintf(buffer + time_len, sizeof(buffer) - time_len,".%03d.%03d] ", tv.tv_usec / 1000, tv.tv_usec % 1000);
#endif
	printf(buffer);
	//endregion

	// Quick and dirty print to stdout
	vprintf(fmt.c_str(), args);
}