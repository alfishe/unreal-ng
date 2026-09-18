#pragma once
#include "stdafx.h"

#include <ctime>
#include <chrono>
#include "emulator/io/rtc/ds12885.h"

// DS12885-style RTC/CMOS (ATM3 / ZX-Evo BaseConf config storage).
// Ported from original Unreal Speccy memory.cpp (cmos_read / cmos_write).
//
// Lives under memory/atm/ because the ATM3 port decoder is its only user:
// machine-specific storage belongs beside the machine, not in the shared
// memory root. It was previously nvram.{h,cpp} while declaring class CMOS,
// one directory above a completely unrelated `struct NVRAM` in platform.h
// (EmulatorState.nvram) - the file now matches the class it declares.
class CMOS
{
// CMOS fields
protected:
	uint8_t _cmos[0x100];
	CMOSTypeEnum _cmos_type = None;
	uint8_t _cmos_addr = 0;

	// Time/date register state. Previously function-local statics inside
	// ReadCMOS() - that made every CMOS instance in the process share ONE
	// clock: a boot in one test could read the UF flag / seconds value left
	// behind by a completely unrelated CMOS instance's read moments earlier
	// (SmucNvram had - and already fixed - the identical bug; see its header
	// comment). Per-instance members close that leak.
	tm _lastTime = {};
	bool _updateFinished = false;
	unsigned _seconds = 0;
	std::chrono::steady_clock::time_point _lastSample{};
	bool _timeValid = false;

	// Deterministic clock: when set, time-register reads serve this frozen
	// instant instead of live host time (tests / replay), and the UF flag
	// never flips - nothing "ticks" while time is frozen.
	bool _fixedTime = false;
	time_t _fixedTimeValue = 0;

// NVRAM (I2C EEPROM) fields
protected:
	uint8_t _nvram[0x800];
	enum EEPROM_STATE { IDLE = 0, RCV_CMD, RCV_ADDR, RCV_DATA, SEND_DATA, RD_ACK };
	uint32_t _address;
	uint8_t _datain;
	uint8_t dataout;
	uint8_t bitsin;
	uint8_t bitsout;
	uint8_t state;
	uint8_t prev;
	uint8_t out;
	uint8_t out_z;

public:
	CMOS();
	virtual ~CMOS();

// NVRAM methods
public:
	void SetNVRAMAddress(uint32_t addr);
	void WriteNVRAM(uint8_t val);
	uint8_t ReadNVRAM();

// CMOS methods
public:
	void SetCMOSType(CMOSTypeEnum type);
	void SetCMOSAddress(uint8_t addr);
	void WriteCMOS(uint8_t val);
	uint8_t ReadCMOS();

	/// Serve a frozen instant instead of live host time (tests / replay -
	/// mirrors SMUCNvram::SetFixedTime, same rationale).
	void SetFixedTime(time_t t);

	/// Back to live host time.
	void UseLiveTime();

// Helper methods
protected:
	uint8_t DecodeFromBCD(uint8_t binary);
};

