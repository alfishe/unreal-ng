#pragma once
#include "stdafx.h"

#include "emulator/platform.h"
#include "emulator/emulatorcontext.h"

class Z80;

// Max RAM size is 4MBytes. Each model has own limits. Max ram used for ZX-Evo / TSConf
// MAX_RAM_PAGES and PAGE defined in platform.h
#define MAX_RAM_SIZE (MAX_RAM_PAGES * PAGE_SIZE)

/// Return code when host memory address cannot be mapped to any RAM/ROM page
#define MEMORY_UNMAPPABLE 0xFF

/// region <Structures>

enum MemoryBankModeEnum : uint8_t
{
	BANK_ROM = 0,
	BANK_RAM = 1
};

enum ROMModeEnum : uint8_t
{
	RM_NOCHANGE = 0,	// Do not make any changes comparing to previous memory pages state
	RM_SOS,				// Turn on SOS ROM (48k)
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
typedef uint8_t (Memory::* MemoryReadCallback)(uint16_t addr);
typedef void (Memory::* MemoryWriteCallback)(uint16_t addr, uint8_t val);
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

/// endregion </Structures>

class Memory
{
    /// region <Fields>
	friend class Z80;
	friend class PortDecoder;
	friend class ROM;

protected:
	// Context passed during initialization
	EmulatorContext* _context = nullptr;
	State* _state = nullptr;

protected:
#if defined CACHE_ALIGNED
	ATTR_ALIGN(4096)
	uint8_t _memory[PAGE_SIZE * MAX_PAGES];	// Continuous memory buffer to fit everything (RAM, all ROMs and General Sound ROM/RAM). Approximately 10MiB in size.
#else // __declspec(align) not available, force u64 align with old method
	uint64_t memory__[PAGE * MAX_PAGES / sizeof(uint64_t)];
	uint8_t* const memory = (uint8_t*)memory__;
#endif

	uint8_t _membits[0x10000];			// Access counters for CPU memory address space (64KiB)

	// Derived addresses
	uint8_t* _ramBase = _memory;
	uint8_t* _cacheBase = _memory + MAX_RAM_PAGES * PAGE_SIZE;
	uint8_t* _miscBase = _cacheBase + MAX_CACHE_PAGES * PAGE_SIZE;
	uint8_t* _romBase = _miscBase + MAX_MISC_PAGES * PAGE_SIZE;

	MemoryBankModeEnum _bank_mode[4];	// Mode for each of four banks. 
	uint8_t* _bank_read[4];				// Memory pointers to RAM/ROM/Cache 16k blocks mapped to four Z80 memory windows
	uint8_t* _bank_write[4];			// Memory pointers to RAM/ROM/Cache 16k blocks mapped to four Z80 memory windows

public:
	// Base addresses for memory classes
	inline uint8_t* RAMBase() { return _ramBase; };			// Get starting address for RAM
	inline uint8_t* CacheBase() { return _cacheBase; };		// Get starting address for Cache
	inline uint8_t* MiscBase() { return _miscBase; };
	inline uint8_t* ROMBase() { return _romBase; };			// Get starting address for ROM

	inline uint8_t* MemoryAccessCounters() { return _membits; };

	// Shortcuts to ROM pages
	uint8_t* base_sos_rom;
	uint8_t* base_dos_rom;
	uint8_t* base_128_rom;
	uint8_t* base_sys_rom;

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
	Memory() = delete;	// Disable default constructor. C++ 11 feature
	Memory(EmulatorContext* context);
	virtual ~Memory();
	/// endregion </Constructors / Destructors>

	/// region <Initialization>
public:
    void Reset();
    void RandomizeMemoryContent();
    void RandomizeMemoryBlock(uint8_t* buffer, size_t size);
    /// endregion </Initialization>

    /// region <Memory access implementation methods>
public:
    static MemoryInterface* GetFastMemoryInterface();
    static MemoryInterface* GetDebugMemoryInterface();

public:
    uint8_t MemoryReadFast(uint16_t addr);
    uint8_t MemoryReadDebug(uint16_t addr);
    void MemoryWriteFast(uint16_t addr, uint8_t value);
    void MemoryWriteDebug(uint16_t addr, uint8_t value);


    /// endregion </Memory access implementation methods>

public:
	// Runtime methods
	void SetROMMode(ROMModeEnum mode);

    void SetROMPage(uint8_t page);
    void SetRAMPageToBank0(uint8_t page);
    void SetRAMPageToBank1(uint8_t page);
    void SetRAMPageToBank2(uint8_t page);
	void SetRAMPageToBank3(uint8_t page);

	bool IsBank0ROM();
	uint8_t GetROMPage();
	uint8_t GetRAMPageForBank0();
    uint8_t GetRAMPageForBank1();
    uint8_t GetRAMPageForBank2();
    uint8_t GetRAMPageForBank3();

	// Debug methods
	void SetROM48k();
	void SetROM128k();
	void SetROMDOS();
	void SetROMSystem();

	// Service methods
	void LoadContentToMemory(uint8_t* contentBuffer, size_t size, uint16_t z80address);

	/// region  <Address helper methods>
	inline uint8_t* RAMPageAddress(uint16_t page) { return _ramBase + (PAGE_SIZE * page); }	// Up to MAX_RAM_PAGES 256 pages
	inline uint8_t* ROMPageAddress(uint8_t page) { return _romBase + (PAGE_SIZE * page); }	// Up to MAX_ROM_PAGES 64 pages

	uint8_t GetRAMPageFromAddress(uint8_t* hostAddress);
	uint8_t GetROMPageFromAddress(uint8_t* hostAddress);

	uint8_t* RemapAddressToCurrentBank(uint16_t address);								// Remap address to the bank. Important! inline for this method for some reason leads to MSVC linker error (not found export function)

	MemoryBankModeEnum GetMemoryBankMode(uint8_t bank);

	uint8_t ReadFromMappedMemoryAddress(uint16_t address);
	void WriteByMappedMemoryAddress(uint16_t address, uint8_t value);

    /// endregion  </Address helper methods>

	// Atomic internal methods (but accessible for testing purposes)
public:
	void InternalSetBanks();

	/// region <Debug methods>
public:
    std::string DumpMemoryBankInfo();
    std::string DumpAllMemoryRegions();
	/// endregion <Debug methods>
};

