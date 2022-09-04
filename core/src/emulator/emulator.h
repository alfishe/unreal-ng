#pragma once

#ifndef _INCLUDED_EMULATOR_H_
#define _INCLUDED_EMULATOR_H_

#include "stdafx.h"
#include "common/logger.h"

#include "emulatorcontext.h"
#include "state.h"
#include "emulator/config.h"
#include "emulator/mainloop.h"
#include "cpu/cpu.h"
#include "cpu/z80.h"
#include "debugger/disassembler/z80disasm.h"

class BreakpointManager;

/// region <Types>
enum EmulatorStateEnum : uint8_t
{
    StateUnknown = 0,
    StateInitialized,
    StateRun,
    StatePaused,
    StateResumed,
    StateStopped
};
/// endregion </Types>

class Emulator
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_CORE;
    const uint16_t _SUBMODULE = PlatformCoreSubmodulesEnum::SUBMODULE_CORE_GENERIC;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    bool _initialized = false;
    std::mutex _mutexInitialization;

    std::thread* _asyncThread = nullptr;

    LoggerLevel _loggerLevel = LoggerLevel::LogTrace;
	EmulatorContext* _context = nullptr;

	Config* _config = nullptr;
	CPU* _cpu = nullptr;
	Z80* _z80 = nullptr;
	Memory* _memory = nullptr;
	MainLoop* _mainloop = nullptr;
	DebugManager* _debugManager = nullptr;
    BreakpointManager* _breakpointManager = nullptr;

	// Control flow
	volatile bool _stopRequested = false;

	// Emulator state
	volatile bool _isPaused = false;
	volatile bool _isRunning = false;
	volatile bool _isDebug = false;
    /// endregion </Fields>

	/// region <Constructors / destructors>
public:
	Emulator();
	Emulator(LoggerLevel level);
	virtual ~Emulator();
    /// endregion </Constructors / destructors>

private:
    void ReleaseNoGuard();

public:
	// Initialization operations
    [[nodiscard]] bool Init();
	void Release();

	// Info methods
	void GetSystemInfo();

    // Performance management
    BaseFrequency_t GetSpeed();
    void SetSpeed(BaseFrequency_t speed);

	// Integration interfaces
	EmulatorContext* GetContext();
    ModuleLogger* GetLogger();
    MainLoop* GetMainLoop();
    Memory* GetMemory();
    DebugManager* GetDebugManager();
    BreakpointManager* GetBreakpointManager();
    FramebufferDescriptor GetFramebuffer();

	// Emulator control cycle
	void Reset();
	void Start();
	void StartAsync();
	void Pause();
	void Resume();
	void Stop();

	// File format operations
	bool LoadSnapshot(std::string& path);

	// Controlled emulator behavior
	void RunSingleCPUCycle(bool skipBreakpoints = true);
	void RunNCPUCycles(unsigned cycles, bool skipBreakpoints = false);
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

