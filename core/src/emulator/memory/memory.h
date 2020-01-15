#pragma once
#include "stdafx.h"

#include "emulator/platform.h"
#include "emulator/emulatorcontext.h"

enum MemoryBankModeEnum : uint8_t
{
	ROM = 0,
	RAM = 1
};

enum ROMModeEnum : uint8_t
{
	RM_NOCHANGE = 0,	// Do not make any changes comparing to previous memory pages state
	RM_SOS,				// Turn on SOS ROM
	RM_DOS,				// Turn on DOS ROM
	RM_SYS,				// Turn on System / Shadow ROM
	RM_128,				// Turn on SOS128 ROM
	RM_CACHE			// Turn on ZX-Evo / TSConf cache into ROM page space
};

enum MemoryBitsEnum : uint8_t
{
	MEMBITS_R = 0x01,
	MEMBITS_W = 0x02,
	MEMBITS_X = 0x04,
	MEMBITS_BPR = 0x10,
	MEMBITS_BPW = 0x20,
	MEMBITS_BPX = 0x40,
	MEMBITS_BPC = 0x80
};


// Memory interface descriptor
//  Read callback to access memory cell (byte)
//  Write callback - to write data into memory cell (byte)
typedef uint8_t (* MemoryReadCallback)(uint16_t addr);
typedef void (* MemoryWriteCallback)(uint16_t addr, uint8_t val);
struct MemoryInterface
{
	MemoryReadCallback read;
	MemoryWriteCallback write;
};

class Memory
{
protected:
	// Context passed during initialization
	EmulatorContext* _context = nullptr;

	const struct TMemModel mem_model[N_MM_MODELS] =
	{
		{ "Pentagon", "PENTAGON",                MM_PENTAGON, 128,  RAM_128 | RAM_256 | RAM_512 | RAM_1024 },
		{ "TS-Config", "TSL",                    MM_TSL, 4096, RAM_4096 },
		{ "ZX-Evo", "ATM3",                      MM_ATM3, 4096, RAM_4096 },
		{ "ATM-Turbo 2+ v7.10", "ATM710",        MM_ATM710, 1024, RAM_128 | RAM_256 | RAM_512 | RAM_1024 },
		{ "ATM-Turbo v4.50", "ATM450",           MM_ATM450, 512,  RAM_512 | RAM_1024 },
		{ "Profi", "PROFI",                      MM_PROFI, 1024, RAM_1024 },
		{ "ZX-Spectrum +3", "PLUS3",             MM_PLUS3, 128,  RAM_128 },
		{ "ZS Scorpion", "SCORPION",             MM_SCORP, 256,  RAM_256 | RAM_1024 },
		{ "ZS Scorpion + PROF ROM", "PROFSCORP", MM_PROFSCORP, 256,  RAM_256 | RAM_1024 },
		{ "ZS Scorpion + GMX", "GMX",			 MM_GMX, 2048,  RAM_2048 },
		{ "Nemo's KAY", "KAY",                   MM_KAY, 256,  RAM_256 | RAM_1024 },
		{ "Quorum", "QUORUM",                    MM_QUORUM, 1024, RAM_128 | RAM_1024 },
		{ "Orel' BK-08 (LSY)", "LSY256",         MM_LSY256, 256, RAM_256 },
		{ "ZXM-Phoenix v1.0", "PHOENIX",         MM_PHOENIX, 1024, RAM_1024 | RAM_2048 },
	};

protected:
	uint8_t memory[PAGE * MAX_PAGES];	// Continuous memory buffer to fit everything (RAM, all ROMs and General Sound ROM/RAM). Approximately 10MiB in size.
	uint8_t membits[0x10000];			// Access counters for CPU memory address space (64KiB)

	MemoryBankModeEnum _bank_mode[4];	// Mode for each of four banks. 
	uint8_t* _bank_read[4];				// Memory pointers to RAM/ROM/Cache 16k blocks mapped to four Z80 memory windows
	uint8_t* _bank_write[4];			// Memory pointers to RAM/ROM/Cache 16k blocks mapped to four Z80 memory windows

	// Shortcuts to ROM pages
	uint8_t* base_sos_rom;
	uint8_t* base_dos_rom;
	uint8_t* base_128_rom;
	uint8_t* base_sys_rom;

public:
	Memory(EmulatorContext* context);

	void SetMode(ROMModeEnum mode);
	void SetBanks();

	__forceinline const uint8_t* GetRAM() const;					// Get starting address for RAM
	__forceinline const uint8_t* GetROM() const;					// Get starting address for ROM
	__forceinline uint8_t* GetRAMPageAddress(uint16_t page);		// Up to MAX_RAM_PAGES 256 pages
	__forceinline uint8_t* GetROMPageAddress(uint8_t page);			// Up to MAX_ROM_PAGES 64 pages

	uint8_t* RemapAddressToCurrentBank(uint32_t addr);				// Remap address to the bank. Important! inline for this method for some reason leads to MSVC linker error (not found export function)
};

