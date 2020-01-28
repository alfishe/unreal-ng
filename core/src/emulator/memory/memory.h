#pragma once
#include "stdafx.h"

#include "emulator/platform.h"
#include "emulator/emulatorcontext.h"

class Z80;

// Max RAM size is 4MBytes. Each model has own limits. Max ram used for ZX-Evo / TSConf
// MAX_RAM_PAGES and PAGE defined in platform.h
#define MAX_RAM_SIZE (MAX_RAM_PAGES * PAGE)

enum MemoryBankModeEnum : uint8_t
{
	BANK_ROM = 0,
	BANK_RAM = 1
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

// Indicators for memory access (registered in membits array)
enum MemoryBitsEnum : uint8_t
{
	MEMBITS_R = 0x01,		// Read
	MEMBITS_W = 0x02,		// Write
	MEMBITS_X = 0x04,		// Execute
	MEMBITS_BPR = 0x10,		// Breakpoint Read
	MEMBITS_BPW = 0x20,		// Breakpoint Write
	MEMBITS_BPX = 0x40,		// Breakpoint Execute
	MEMBITS_BPC = 0x80		// Breakpoint Conditional
};


// Memory interface descriptor
//  Read callback to access memory cell (byte)
//  Write callback - to MemoryWrite data into memory cell (byte)
typedef uint8_t (Z80::* MemoryReadCallback)(uint16_t addr);
typedef void (Z80::* MemoryWriteCallback)(uint16_t addr, uint8_t val);
struct MemoryInterface
{
	MemoryInterface() = delete;			// Disable default constructor. C++ 11 feature
	MemoryInterface(MemoryReadCallback read, MemoryWriteCallback write)
	{
		MemoryRead = read;
		MemoryWrite = write;
	};

	MemoryReadCallback MemoryRead;
	MemoryWriteCallback MemoryWrite;
};

class Memory
{
	friend class Z80;
	friend class ROM;

protected:
	// Context passed during initialization
	EmulatorContext* _context = nullptr;

protected:

#ifdef CACHE_ALIGNED
	ATTR_ALIGN(4096)
	uint8_t _memory[PAGE * MAX_PAGES];	// Continuous memory buffer to fit everything (RAM, all ROMs and General Sound ROM/RAM). Approximately 10MiB in size.
#else // __declspec(align) not available, force u64 align with old method
	uint64_t memory__[PAGE * MAX_PAGES / sizeof(uint64_t)];
	uint8_t * const memory = (uint8_t*)memory__;
#endif
	
	uint8_t _membits[0x10000];			// Access counters for CPU memory address space (64KiB)

	// Derived addresses
	uint8_t* _ramBase = _memory;
	uint8_t* _cacheBase = _memory + MAX_RAM_PAGES * PAGE;
	uint8_t* _miscBase = _cacheBase + MAX_CACHE_PAGES * PAGE;
	uint8_t* _romBase = _miscBase + MAX_MISC_PAGES * PAGE;

	MemoryBankModeEnum _bank_mode[4];	// Mode for each of four banks. 
	uint8_t* _bank_read[4];				// Memory pointers to RAM/ROM/Cache 16k blocks mapped to four Z80 memory windows
	uint8_t* _bank_write[4];			// Memory pointers to RAM/ROM/Cache 16k blocks mapped to four Z80 memory windows

public:
	// Base addresses for memory classes
	inline uint8_t* RAMBase() { return _memory; };			// Get starting address for RAM
	inline uint8_t* CacheBase() { return _cacheBase; };		// Get starting address for Cache
	inline uint8_t* MiscBase() { return _miscBase; };
	inline uint8_t* ROMBase() { return _romBase; };			// Get starting address for ROM

	// Shortcuts to ROM pages
	uint8_t* base_sos_rom;
	uint8_t* base_dos_rom;
	uint8_t* base_128_rom;
	uint8_t* base_sys_rom;

public:
	Memory() = delete;	// Disable default constructor. C++ 11 feature
	Memory(EmulatorContext* context);
	virtual ~Memory();

	void SetROMMode(ROMModeEnum mode);
	void SetBanks();

	inline uint8_t* RAMPageAddress(uint16_t page) { return _ramBase + (PAGE * page); }	// Up to MAX_RAM_PAGES 256 pages
	inline uint8_t* ROMPageAddress(uint8_t page) { return _romBase + (PAGE * page); }	// Up to MAX_ROM_PAGES 64 pages

	uint8_t* RemapAddressToCurrentBank(uint16_t addr);				// Remap address to the bank. Important! inline for this method for some reason leads to MSVC linker error (not found export function)

	MemoryBankModeEnum GetMemoryBankMode(uint8_t bank);

	// Atomic internal methods (but accessible for testing purposes
public:
	void InternalSetBanks();
};

