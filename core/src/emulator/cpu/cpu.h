#pragma once
#include "stdafx.h"

#include "emulator/cpu/cputables.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/sound.h"
#include "emulator/io/hdd/hdd.h"
#include "emulator/emulatorcontext.h"

class Z80;
class Memory;
class Sound;
class HDD;

class CPU
{
	friend class Z80;
	friend class Memory;
	friend class Sound;
	friend class HDD;

	// Ensure that all flag / decoding tables are initialized only once using static member
public:
	static CPUTables _cpuTables;

protected:
	EmulatorContext* _context = nullptr;
	Z80* _cpu = nullptr;
	Memory* _memory = nullptr;
	Sound* _sound = nullptr;
	HDD* _hdd = nullptr;

	ROMModeEnum _mode = ROMModeEnum::RM_NOCHANGE;

public:
	CPU() = delete;					// Disable default constructor. C++ 11 feature
	CPU(EmulatorContext* context);	// Only constructor with context param is allowed
	virtual ~CPU();

	Z80* GetZ80Instance();
	Memory* GetMemory();

	// Configuration methods
public:
	void UseFastMemoryInterface();
	void UseDebugMemoryInterface();

	// Z80 CPU-related methods
public:
	void Reset();
	void SetCPUClockSpeed(uint8_t);
	void CPUFrameCycle();

	// Event handlers
public:
	void UpdateScreen();
};
