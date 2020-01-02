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

	_cpu = new CPU();
	if (_cpu != nullptr)
	{
		//_cpu->

		result = true;
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