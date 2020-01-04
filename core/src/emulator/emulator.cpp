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
		DEBUG("Emulator::Init - context created");

		result = true;
	}
	else
	{
		ERROR("Emulator::Init - context creation failed");
	}

	// Create and initialize Z80 main CPU instance
	if (result)
	{
		_cpu = new CPU();
		if (_cpu != nullptr)
		{
			//_cpu->

			DEBUG("Emulator::Init - cpu created");

			result = true;
		}
		else
		{
			ERROR("Emulator::Init - cpu creation failed");
		}
	}

	// Create and initialize main emulator loop
	if (result)
	{
		result = false;

		_mainloop = new MainLoop(_cpu);
		if (_mainloop != nullptr)
		{
			DEBUG("Emulator::Init - mainloop created");

			result = true;
		}
		else
		{
			ERROR("Emulator::Init - mainloop creation failed");
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