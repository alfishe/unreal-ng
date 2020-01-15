#pragma once
#include "stdafx.h"

#include "emulator/cpu/cputables.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/emulatorcontext.h"

class Z80;
class Memory;

class CPU
{
	friend class Z80;
	friend class Memory;

	// Ensure that all flag / decoding tables are initialized only once using static member
public:
	static CPUTables _cpuTables;

protected:
	EmulatorContext* _context = nullptr;
	Z80* _cpu = nullptr;
	Memory* _memory = nullptr;

public:
	CPU() = delete;					// Disable default constructor. C++ 11 feature
	CPU(EmulatorContext* context);	// Only constructor with context param is allowed
	virtual ~CPU();

	Z80* GetZ80Instance();
	Memory* GetMemory();

	// Configuration methods
public:
	void SetFastMemoryInterface();
	void SetDebugMemoryInterface();

	// Z80 CPU-related methods
public:
	void CPUFrameCycle();

	// Event handlers
public:
	void UpdateScreen();
};
