#include "stdafx.h"

#include "emulator.h"
#include "common/systemhelper.h"

Emulator::Emulator()
{

}

Emulator::~Emulator()
{
	LOGDEBUG("Emulator::~Emulator()");
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
		result = false;
	}

	// Get host system info
	GetSystemInfo();

	// Load configuration
	if (result)
	{
		_config = new Config(_context);
		if (_config != nullptr)
		{
			result = _config->LoadConfig();

			if (result)
			{
				LOGDEBUG("Emulator::Init - Config file successfully loaded");
			}
			else
			{
				LOGERROR("Emulator::Init - Config load failed");
			}
		}
		else
		{
			LOGERROR("Emulator::Init - config manager creation failed");
			result = false;
		}
	}

	// Create and initialize CPU system instance (including most peripheral devices)
	if (result)
	{
		_cpu = new CPU(_context);
		if (_cpu != nullptr)
		{
			LOGDEBUG("Emulator::Init - CPU system created");

			result = true;
		}
		else
		{
			LOGERROR("Emulator::Init - CPU system (or main peripheral devices) creation failed");
			result = false;
		}
	}

	// Load ROMs
	if (result)
	{
		ROM& rom = *_cpu->GetROM();

		result = rom.LoadROM();
		if (result)
		{
			LOGDEBUG("Emulator::Init - ROM data successfully loaded");
			result = true;
		}
		else
		{
			LOGERROR("Emulator::Init - ROM load failed");
			result = false;
		}
	}

	// Create and initialize additional peripheral devices
	;	// Tape
	;	// HDD/CD
	;	// ZiFi
	;	// GS / NGS

	// Create and initialize Debugger and related components
	;	// Debugger

	// Create and initialize Scripting support
	;	// Scripting host (Python or Lua?)


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
	// Stop and release main loop
	if (_mainloop != nullptr)
	{
		_mainloop->Stop();

		delete _mainloop;
		_mainloop = nullptr;
	}

	// Release additional peripheral devices
	// GS / NGS
	// ZiFi
	// HDD/CD
	// Tape


	// Release CPU subsystem (it will release all main peripherals)
	if (_cpu != nullptr)
	{
		delete _cpu;
		_cpu = nullptr;
	}

	// Release EmulatorContext as last step
	if (_context != nullptr)
	{
		delete _context;
		_context = nullptr;
	}
}

//
// Read CPU ID string and analyze MMX/SSE/SSE2 feature flags
// See: https://en.wikipedia.org/wiki/CPUID
//
void Emulator::GetSystemInfo()
{
	HOST& host = _context->host;

	char cpuString[49];
	cpuString[0] = '\0';

	SystemHelper::GetCPUString(cpuString);
	LOGINFO("CPU ID: %s", cpuString);

	unsigned cpuver = SystemHelper::GetCPUID(1, 0);		// Read Highest Function Parameter and ManufacturerID
	unsigned features = SystemHelper::GetCPUID(1, 1);	// Read Processor Info and Feature Bits
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

	_cpu->Reset();
}

void Emulator::Start()
{
	_cpu->Reset();

	_stopRequested = false;
	_isPaused = false;
	_isRunning = true;

	_mainloop->Run(_stopRequested);
}

void Emulator::Pause()
{
	_stopRequested = true;

	_isPaused = true;
	_isRunning = false;
}

void Emulator::Resume()
{
	_stopRequested = false;
	_isPaused = false;
	_isRunning = false;

	_mainloop->Run(_stopRequested);
}

void Emulator::Stop()
{
	_stopRequested = true;
	_isPaused = false;
	_isRunning = false;

	// TODO: handle IO shutting down
	// FDC: flush changes to disk image(s)
	// HDD: flush changes and unmount
	// Fully shut down video / sound
}

bool Emulator::IsRunning()
{
	return _isRunning;
}

bool Emulator::IsPaused()
{
	return _isPaused;
}

bool Emulator::IsDebug()
{
	return _isDebug;
}