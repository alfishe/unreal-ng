#include "stdafx.h"

#include "emulator.h"
#include "common/systemhelper.h"

Emulator::Emulator()
{

}

Emulator::~Emulator()
{

}

bool Emulator::Init()
{
	bool result = false;

	// Create and initialize emulator context
	_context = new EmulatorContext();
	if (_context != nullptr)
	{
		LOGDEBUG("Emulator::Init - context created");

		result = true;
	}
	else
	{
		LOGERROR("Emulator::Init - context creation failed");
	}

	// Get system info
	GetSystemInfo();

	// Load configuration and ROMs
	//config.LoadConfig();

	// Create and initialize Z80 main CPU instance
	if (result)
	{
		_cpu = new CPU(_context);
		if (_cpu != nullptr)
		{
			//_cpu->

			LOGDEBUG("Emulator::Init - main cpu created");

			result = true;
		}
		else
		{
			LOGERROR("Emulator::Init - main cpu creation failed");
		}
	}

	// Create and initialize main emulator loop
	if (result)
	{
		result = false;

		_mainloop = new MainLoop(_cpu, _context);
		if (_mainloop != nullptr)
		{
			LOGDEBUG("Emulator::Init - mainloop created");

			result = true;
		}
		else
		{
			LOGERROR("Emulator::Init - mainloop creation failed");
		}
	}


	return result;
}

void Emulator::Release()
{
	if (_cpu != nullptr)
	{
		delete _cpu;
		_cpu = nullptr;
	}

	if (_mainloop != nullptr)
	{
		_mainloop->Stop();
		delete _mainloop;
		_mainloop = nullptr;
	}

	if (_context != nullptr)
	{
		delete _context;
		_context = nullptr;
	}
}

void Emulator::GetSystemInfo()
{
	HOST& host = _context->host;

	char cpuString[49];
	cpuString[0] = '\0';

	SystemHelper::GetCPUString(cpuString);
	LOGINFO("CPU ID: %s", cpuString);

	unsigned cpuver = SystemHelper::GetCPUID(1, 0);
	unsigned features = SystemHelper::GetCPUID(1, 1);
	host.mmx = (features >> 23) & 1;
	host.sse = (features >> 25) & 1;
	host.sse2 = (features >> 26) & 1;
	LOGINFO("MMX:%s, SSE:%s, SSE2:%s", host.mmx ? "YES" : "NO", host.sse ? "YES" : "NO", host.sse2 ? "YES" : "NO");

	host.cpufq = SystemHelper::GetCPUFrequency();
	LOGINFO("CPU Frequency: %dMHz", (unsigned)(host.cpufq / 1000000));
}

void Emulator::Reset()
{
	_isPaused = false;
	_isRunning = false;
}

void Emulator::Run()
{
	_isPaused = false;
	_isRunning = true;
}

void Emulator::Pause()
{
	_isPaused = true;
	_isRunning = false;
}

bool Emulator::IsRunning()
{
	return _isRunning;
}

bool Emulator::IsPaused()
{
	return _isPaused;
}