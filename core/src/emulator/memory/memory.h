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

class Memory
{
protected:
	// Context passed during initialization
	EmulatorContext* _context = nullptr;

protected:
	uint8_t memory[0x10000];			// Full 4MB memory space buffer
	uint8_t membits[0x10000];			// Access counters for each memory byte

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
};


void set_banks();
void set_scorp_profrom(unsigned read_address);

/*
Z80INLINE uint8_t *am_r(unsigned addr)
{
#ifdef MOD_VID_VD
   if (comp.vdbase && (unsigned)((addr & 0xFFFF) - 0x4000) < 0x1800) return comp.vdbase + (addr & 0x1FFF);
#endif
   // return bankr[(addr >> 14) & 3] + (addr & (PAGE-1));
}
//u8 rmdbg(u32 addr);
//void wmdbg(u32 addr, u8 val);
//u8 *MemDbg(u32 addr);
*/
