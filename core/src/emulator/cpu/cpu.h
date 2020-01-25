#pragma once
#include "stdafx.h"

#include "emulator/cpu/cputables.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/ports.h"
#include "emulator/memory/rom.h"
#include "emulator/sound/sound.h"
#include "emulator/io/hdd/hdd.h"
#include "emulator/video/screen.h"
#include "emulator/emulatorcontext.h"

class Z80;
class Memory;
class Ports;
class ROM;
class Sound;
class HDD;
class Screen;

class CPU
{
	friend class Z80;
	friend class Memory;
	friend class Ports;
	friend class ROM;
	friend class Sound;
	friend class HDD;

	// Ensure that all flag / decoding tables are initialized only once using static member
public:
	static CPUTables _cpuTables;

protected:
	EmulatorContext* _context = nullptr;
	Z80* _cpu = nullptr;
	Memory* _memory = nullptr;
	Ports* _ports = nullptr;
	ROM* _rom = nullptr;
	Sound* _sound = nullptr;
	HDD* _hdd = nullptr;
	VideoControl* _video = nullptr;
	Screen* _screen = nullptr;

	ROMModeEnum _mode = ROMModeEnum::RM_NOCHANGE;

public:
	CPU() = delete;					// Disable default constructor. C++ 11 feature
	CPU(EmulatorContext* context);	// Only constructor with context param is allowed
	virtual ~CPU();

	inline Z80* GetZ80() { return _cpu; }
	inline Memory* GetMemory() { return _memory; }
	inline Ports* GetPorts() { return _ports; }
	inline ROM* GetROM() { return _rom; }

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
