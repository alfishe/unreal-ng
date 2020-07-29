#pragma once
#include "stdafx.h"

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/cpu/cputables.h"
#include "emulator/cpu/z80.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/ports.h"
#include "emulator/memory/rom.h"
#include "emulator/sound/sound.h"
#include "emulator/io/hdd/hdd.h"
#include "emulator/video/screen.h"
#include "emulator/emulatorcontext.h"

class MessageCenter;
class Z80;
class PortDecoder;

class CPU
{
    friend class Emulator;
	friend class Z80;
	friend class Memory;
	friend class Ports;
	friend class PortDecoder;
	friend class ROM;
	friend class Sound;
	friend class HDD;

	// Ensure that all flag / decoding tables are initialized only once using static member
public:
	static CPUTables _cpuTables;

	/// region <Fields>
protected:
	EmulatorContext* _context = nullptr;
    State* _state = nullptr;
    const CONFIG* _config = nullptr;

    MessageCenter* _messageCenter = nullptr;

	Z80* _cpu = nullptr;
	Memory* _memory = nullptr;
	Ports* _ports = nullptr;
	PortDecoder* _portDecoder = nullptr;
	ROM* _rom = nullptr;
	Keyboard* _keyboard = nullptr;
	Sound* _sound = nullptr;
	HDD* _hdd = nullptr;
	VideoControl* _video = nullptr;
	Screen* _screen = nullptr;

	ROMModeEnum _mode = ROMModeEnum::RM_NOCHANGE;
    /// endregion </Fields>

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
