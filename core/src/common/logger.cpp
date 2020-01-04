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
	// Quick and dirty print to stdout
	vprintf(fmt.c_str(), args);
}