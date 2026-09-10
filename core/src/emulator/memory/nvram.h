#pragma once
#include "stdafx.h"

#include <ctime>
#include <chrono>

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

class NVRAM
{
// CMOS fields
protected:
	uint8_t _cmos[0x100];
	CMOSTypeEnum _cmos_type = None;
	uint8_t _cmos_addr = 0;

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
	NVRAM();
	virtual ~NVRAM();

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

// Helper methods
protected:
	uint8_t DecodeFromBCD(uint8_t binary);
};

