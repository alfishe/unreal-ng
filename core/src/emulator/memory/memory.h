#pragma once
#include "stdafx.h"

#include "emulator/platform.h"
#include "emulator/emulatorcontext.h"

class Z80;

// Max RAM size is 4MBytes. Each model has own limits. Max ram used for ZX-Evo / TSConf
// MAX_RAM_PAGES and PAGE defined in platform.h
constexpr size_t MAX_RAM_SIZE = MAX_RAM_PAGES * PAGE_SIZE;

/// Return code when host memory address cannot be mapped to any RAM/ROM page
const uint16_t MEMORY_UNMAPPABLE = 0xFFFF;

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

struct MemoryPageDescriptor
{
    MemoryBankModeEnum mode;
    uint8_t page;
    uint16_t addressInPage;
};

// Memory interface descriptor
//  Read callback to access memory cell (byte)
//  Write callback - to MemoryWrite data into memory cell (byte)
typedef uint8_t (Memory::* MemoryReadCallback)(uint16_t addr, bool isExecution);
typedef void (Memory::* MemoryWriteCallback)(uint16_t addr, uint8_t val);
struct MemoryInterface
{
    MemoryInterface() = delete;			                // Disable default constructor. C++ 11 feature
    MemoryInterface(const MemoryInterface&) = delete;   // Disable copy constructor. C++ 11 feature

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
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_MEMORY;
    const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_GENERIC;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    // Context passed during initialization
    EmulatorContext* _context = nullptr;
    State* _state = nullptr;

protected:
#if defined CACHE_ALIGNED
    // Continuous memory buffer to fit everything (RAM, all ROMs and General Sound ROM/RAM). Approximately 10MiB in size
    uint8_t _memory[PAGE_SIZE * MAX_PAGES] ATTR_ALIGN(4096);
#else // __declspec(align) not available, force u64 align with old method
    uint64_t memory__[PAGE * MAX_PAGES / sizeof(uint64_t)];
    uint8_t* const memory = (uint8_t*)memory__;
#endif

    // Derived addresses
    uint8_t* _ramBase = _memory;
    uint8_t* _cacheBase = _memory + MAX_RAM_PAGES * PAGE_SIZE;
    uint8_t* _miscBase = _cacheBase + MAX_CACHE_PAGES * PAGE_SIZE;
    uint8_t* _romBase = _miscBase + MAX_MISC_PAGES * PAGE_SIZE;

    MemoryBankModeEnum _bank_mode[4];	// Mode for each of four banks
    uint8_t* _bank_read[4];				// Memory pointers to RAM/ROM/Cache 16k blocks mapped to four Z80 memory windows
    uint8_t* _bank_write[4];			// Memory pointers to RAM/ROM/Cache 16k blocks mapped to four Z80 memory windows

    // Access flags / Counters
    size_t _memZ80ReadCounters[PAGE_SIZE * 4];              // Read access counters for each Z80 address
    size_t _memZ80WriteCounters[PAGE_SIZE * 4];             // Write access counters for each Z80 address
    size_t _memZ80ExecuteCounters[PAGE_SIZE * 4];           // Execute access counters for each Z80 address

    size_t _memAccessReadCounters[PAGE_SIZE * MAX_PAGES];   // Read access counters for all memory available
    size_t _memAccessWriteCounters[PAGE_SIZE * MAX_PAGES];  // Write access counters for all memory available
    size_t _memAccessExecuteCounters[PAGE_SIZE * MAX_PAGES];// Execute access counters for all memory available

    size_t _memPageReadCounters[MAX_PAGES];                 // Read access counters aggregated per each memory page
    size_t _memPageWriteCounters[MAX_PAGES];                // Write access counters aggregated per each memory page
    size_t _memPageExecuteCounters[MAX_PAGES];              // Execute access counters aggregated per each memory page

    uint8_t _memZ80BankReadMarks;                           // Per-Z80-bank read access flags
    uint8_t _memZ80BankWriteMarks;                          // Per-Z80-bank write access flags
    uint8_t _memZ80BankExecuteMarks;                        // Per-Z80-bank execute access flags

    uint8_t _memPageReadMarks[MAX_PAGES / 8];               // Per-page read access flags
    uint8_t _memPageWriteMarks[MAX_PAGES / 8];              // Per-page write access flags
    uint8_t _memPageExecuteMarks[MAX_PAGES / 8];            // Per-page execute access flags

public:
    // Base addresses for memory classes
    inline uint8_t* RAMBase() { return _ramBase; };			// Get starting address for RAM
    inline uint8_t* CacheBase() { return _cacheBase; };		// Get starting address for Cache
    inline uint8_t* MiscBase() { return _miscBase; };
    inline uint8_t* ROMBase() { return _romBase; };			// Get starting address for ROM

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

    /// region <Emulation memory interface methods>
public:
    static MemoryInterface* GetFastMemoryInterface();
    static MemoryInterface* GetDebugMemoryInterface();

public:
    uint8_t MemoryReadFast(uint16_t addr, bool isExecution);
    uint8_t MemoryReadDebug(uint16_t addr, bool isExecution);
    void MemoryWriteFast(uint16_t addr, uint8_t value);
    void MemoryWriteDebug(uint16_t addr, uint8_t value);

    /// endregion </Emulation memory interface methods>

    /// region <Runtime methods>
public:
    void SetROMMode(ROMModeEnum mode);

    void SetROMPage(uint16_t page, bool updatePorts = false);
    void SetRAMPageToBank0(uint16_t page, bool updatePorts = false);
    void SetRAMPageToBank1(uint16_t page);
    void SetRAMPageToBank2(uint16_t page);
    void SetRAMPageToBank3(uint16_t page, bool updatePorts = false);

    bool IsBank0ROM();
    uint16_t GetROMPage();
    uint16_t GetRAMPageForBank0();
    uint16_t GetRAMPageForBank1();
    uint16_t GetRAMPageForBank2();
    uint16_t GetRAMPageForBank3();

    uint16_t GetROMPageForBank(uint8_t bank);
    uint16_t GetRAMPageForBank(uint8_t bank);
    uint16_t GetPageForBank(uint8_t bank);      // Returns absolute page number without distinction to RAM/Cache/Misc/ROM - since they all located in the same block

    /// endregion </Runtime methods>

    /// region <Service methods>
    void LoadContentToMemory(uint8_t* contentBuffer, size_t size, uint16_t z80address);
    void LoadRAMPageData(uint8_t page, uint8_t* fromBuffer, size_t bufferSize);
    /// endregion </Service methods>

    /// region  <Address helper methods>

    uint8_t* RAMPageAddress(uint16_t page);
    uint8_t* ROMPageAddress(uint8_t page);

    uint16_t GetRAMPageFromAddress(uint8_t* hostAddress);
    uint16_t GetROMPageFromAddress(uint8_t* hostAddress);

    size_t GetPhysicalOffsetForZ80Address(uint16_t address);
    size_t GetPhysicalOffsetForZ80Bank(uint8_t bank);

    uint8_t* MapZ80AddressToPhysicalAddress(uint16_t address);			    // Remap address to the bank. Important! inline for this method for some reason leads to MSVC linker error (not found export function)
    MemoryPageDescriptor MapZ80AddressToPhysicalPage(uint16_t address);     // Determines current bank for Z80 address specified

    MemoryBankModeEnum GetMemoryBankMode(uint8_t bank);

    uint8_t DirectReadFromZ80Memory(uint16_t address);                      // Read from Z80 memory (actual pages config) without triggering any debug logic
    void DirectWriteToZ80Memory(uint16_t address, uint8_t value);           // Write to Z80 memory (actual pages config) without triggering any debug logic

    /// endregion  </Address helper methods>

    /// region <Counters>
public:
    void ResetCounters();

    size_t GetZ80BankTotalAccessCount(uint8_t bank);
    size_t GetZ80BankReadAccessCount(uint8_t bank);
    size_t GetZ80BankWriteAccessCount(uint8_t bank);
    size_t GetZ80BankExecuteAccessCount(uint8_t bank);

    size_t GetZ80BankTotalAccessCountExclScreen(uint8_t bank);
    size_t GetZ80BankReadAccessCountExclScreen(uint8_t bank);
    size_t GetZ80BankWriteAccessCountExclScreen(uint8_t bank);
    size_t GetZ80BankExecuteAccessCountExclScreen(uint8_t bank);

    /// endregion </Counters>

    // Atomic internal methods (but accessible for testing purposes)
public:
    void DefaultBanksFor48k();

    /// region <Debug methods>
public:
    void SetROM48k(bool updatePorts = false);
    void SetROM128k(bool updatePorts = false);
    void SetROMDOS(bool updatePorts = false);
    void SetROMSystem(bool updatePorts = false);

    std::string GetCurrentBankName(uint8_t bank);
    std::string DumpMemoryBankInfo();
    std::string DumpAllMemoryRegions();
    /// endregion <Debug methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class MemoryCUT : public Memory
{
public:
    MemoryCUT(EmulatorContext* context) : Memory(context) {};

public:
    using Memory::_memory;
    using Memory::_ramBase;
    using Memory::_cacheBase;
    using Memory::_miscBase;
    using Memory::_romBase;
};
#endif // _CODE_UNDER_TEST