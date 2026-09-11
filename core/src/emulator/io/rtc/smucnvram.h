#pragma once
#include "stdafx.h"

#include <ctime>
#include <chrono>

/// RTC / NVRAM devices for clones and addon cards. Each hardware standard
/// (the SMUC board's DS1685 CMOS + LC16 serial EEPROM below, the Dallas and
/// Rus512 CMOS variants, future GLUK-type clocks for other machines) gets its
/// own unit in this folder - they share nothing but the bus they sit behind

enum CMOSTypeEnum
{
	None = 0,
	Dallas = 1,
	Rus512 = 2
};

enum CMOSMemoryEnum
{
	Second = 0,
	Reserved_1 = 1,
	Minute = 2,
	Reserved_3 = 3,
	Hour = 4,
	Reserved_5 = 5,
	DayOfWeek = 6,
	Day = 7,
	Month = 8,
	Year = 9,
	Unknown_10 = 10,
	BitFlags = 11,
	UF = 12,
	Unknown_13 = 13
};

class SMUCNvram
{
// CMOS fields
protected:
	uint8_t _cmos[0x100];
	CMOSTypeEnum _cmos_type = None;
	uint8_t _cmos_addr = 0;

	// Deterministic clock: when set, time-register reads serve this frozen
	// instant instead of live host time (tests / replay - the ProfROM menu
	// clock drives boot-timeline code paths, profrom-service-monitor-turbo.md)
	bool _fixedTime = false;
	time_t _fixedTimeValue = 0;
	tm _lastTime = {};
	bool _updateFinished = false;

// Serial-link EEPROM fields (SMUC #FFBA)
protected:
	uint8_t _nvram[0x800];

	// LC16-style 3-wire serial EEPROM behind the SMUC system port. Behavioral
	// port of Xpeccy nvram.c - the model the ProfROM SMUC driver (page-7 helpers
	// #0E01-#0F55) is proven against: SDA out = #FFBA bit 4, SCL out = bit 6
	// (the same line reads back as data-in, open collector), WP = bit 5. Select
	// byte 1010dddW/R picks the address window; see
	// docs/inprogress/2026-09-07-scorpion-zs256-clone/profrom-smuc-not-found-and-driver-disassembly.md
	enum EEPROMMode { NV_IDLE = 0, NV_COM, NV_ADR, NV_WRITE };
	EEPROMMode _mode = NV_IDLE;
	bool _stable = false;      // SCL seen high at least once since START
	bool _tx = false;          // device -> host shift-out active
	bool _rx = false;          // host -> device shift-in active
	bool _ack = false;         // pull the data line low during the ACK clock
	uint8_t _bitCount = 0;     // bits left in the current shift (receive side)
	uint8_t _data = 0;         // shift register
	uint16_t _address = 0;     // EEPROM address (2 KB space)
	uint8_t _writeBuffer[16];  // one programming page, committed on STOP
	uint8_t _writePos = 0;
	int _sda = 1;              // last SDA level seen (nonzero = high)
	int _scl = 1;              // last SCL level seen (nonzero = high)

public:
	SMUCNvram();
	virtual ~SMUCNvram();

// Serial-link EEPROM methods (SMUC #FFBA)
public:
	/// Feed a raw #FFBA port write (bits 4/6/5 = SDA out / SCL out / WP)
	void WriteSerialLink(uint8_t val);

	/// Data line level for a #FFBA port read: true = high (bit 6 set).
	/// Low while ACKing or idle (Xpeccy LC16 convention - every ProfROM
	/// #0EDE presence check samples it low and passes)
	bool ReadSerialLink() const;

	/// Backing-store readback (debug UI / tests)
	uint8_t GetEEPROMByte(uint16_t addr) const { return _nvram[addr & 0x7FF]; }

	/// Re-arm the power-on line/shift state; battery-backed contents survive
	void ResetSerialLinkState();

// CMOS methods
public:
	void SetCMOSType(CMOSTypeEnum type);
	void SetCMOSAddress(uint8_t addr);
	void WriteCMOS(uint8_t val);
	uint8_t ReadCMOS();

	/// Freeze the wall-clock source: time-register reads return the given
	/// instant (Unix seconds, UTC) instead of live host time
	void SetFixedTime(time_t t);

	/// Back to live host time
	void UseLiveTime();

// Helper methods
protected:
	uint8_t DecodeFromBCD(uint8_t binary);
};

