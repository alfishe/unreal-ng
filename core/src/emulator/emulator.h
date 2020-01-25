#pragma once

#ifndef _INCLUDED_EMULATOR_H_
#define _INCLUDED_EMULATOR_H_

#include "stdafx.h"
#include "common/logger.h"

#include "emulator/config.h"
#include "emulator/mainloop.h"
#include "cpu/cpu.h"
#include "emulatorcontext.h"

class Emulator
{
protected:
	EmulatorContext* _context = nullptr;
	Config* _config = nullptr;
	CPU* _cpu = nullptr;
	MainLoop* _mainloop = nullptr;

	// Control flow
	volatile bool _stopRequested = false;

	// Emulator state
	bool _isPaused = false;
	bool _isRunning = false;
	bool _isDebug = false;

	// Constructors / destructors
public:
	Emulator();
	virtual ~Emulator();

public:
	// Initialization operations
	bool Init();
	void Release();
	void GetSystemInfo();

	// Emulator control cycle
	void Reset();
	void Start();
	void Pause();
	void Resume();
	void Stop();

	// Status methods
	bool IsRunning();
	bool IsPaused();
	bool IsDebug();

	// Debug methods

	// Counters method
	void ResetCountersAll();
	void ResetCounter();
};

#endif

