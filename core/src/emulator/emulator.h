#pragma once

#ifndef _INCLUDED_EMULATOR_H_
#define _INCLUDED_EMULATOR_H_

#include "stdafx.h"
#include "common/logger.h"

#include "emulator/config.h"
#include "emulator/mainloop.h"
#include "cpu/cpu.h"
#include "cpu/z80.h"
#include "emulatorcontext.h"

class Emulator
{
protected:
	EmulatorContext* _context = nullptr;
	Config* _config = nullptr;
	CPU* _cpu = nullptr;
	Z80* _z80 = nullptr;
	MainLoop* _mainloop = nullptr;

	// Control flow
	volatile bool _stopRequested = false;

	// Emulator state
	volatile bool _isPaused = false;
	volatile bool _isRunning = false;
	volatile bool _isDebug = false;

	// Constructors / destructors
public:
	Emulator();
	virtual ~Emulator();

public:
	// Initialization operations
	bool Init();
	void Release();

	// Info methods
	void GetSystemInfo();

	// Emulator control cycle
	void Reset();
	void Start();
	void Pause();
	void Resume();
	void Stop();

	// Controlled emulator behavior
	void RunSingleCPUCycle();
	void RunNCPUCycles(unsigned cycles);
	void RunUntilCondition(/* some condition descriptor */);    // TODO: revise design

    // Debug methods
	void DebugOn();
	void DebugOff();


	// Status methods
	bool IsRunning();
	bool IsPaused();
	bool IsDebug();

	// Counters method
	void ResetCountersAll();
	void ResetCounter();
};

#endif

