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
    bool _initialized = false;
    std::mutex _mutexInitialization;

    std::thread* _asyncThread = nullptr;

	EmulatorContext* _context = nullptr;
	Config* _config = nullptr;
	CPU* _cpu = nullptr;
	Z80* _z80 = nullptr;
	Memory* _memory = nullptr;
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

private:
    void ReleaseNoGuard();

public:
	// Initialization operations
    [[nodiscard]] bool Init();
	void Release();

	// Info methods
	void GetSystemInfo();

	// Integration interfaces
	EmulatorContext* GetContext();
    ModuleLogger* GetLogger();
    FramebufferDescriptor GetFramebuffer();

	// Emulator control cycle
	void Reset();
	void Start();
	void StartAsync();
	void Pause();
	void Resume();
	void Stop();

	// Controlled emulator behavior
	void RunSingleCPUCycle();
	void RunNCPUCycles(unsigned cycles);
	void RunUntilInterrupt();
	void RunUntilCondition(/* some condition descriptor */);    // TODO: revise design

	// Actions
	bool LoadROM(std::string path);


    // Debug methods
	void DebugOn();
	void DebugOff();

    Z80State* GetZ80State();


	// Status methods
	bool IsRunning();
	bool IsPaused();
	bool IsDebug();
    std::string GetStatistics();

	// Counters method
	void ResetCountersAll();
	void ResetCounter();
};

#endif

