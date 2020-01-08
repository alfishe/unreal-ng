#include "stdafx.h"

#include "Emulator.h"

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

	// Create and initialize Z80 main CPU instance
	if (result)
	{
		_cpu = new CPU();
		if (_cpu != nullptr)
		{
			//_cpu->

			LOGDEBUG("Emulator::Init - cpu created");

			result = true;
		}
		else
		{
			LOGERROR("Emulator::Init - cpu creation failed");
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