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

// NVRAM fields
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
	NVRAM();
	virtual ~NVRAM();

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

// Helper methods
protected:
	uint8_t DecodeFromBCD(uint8_t binary);
};

