#pragma once
#include "stdafx.h"

#include "emulator/platform.h"
#include "emulator/emulatorcontext.h"

class MemoryAccessTracker;
class Z80;
class FeatureManager;

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
    friend class PortDecoder; // Allow PortDecoder to access _memoryAccessTracker

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
    EmulatorState* _state = nullptr;

#ifdef _WIN32
    HANDLE _mappedMemoryHandle = INVALID_HANDLE_VALUE;
#else
    int _mappedMemoryFd = -1;
#endif // _WIN32

    std::string _mappedMemoryFilepath;

protected:
    // Whole system memory
    uint8_t* _memory = nullptr;
    const size_t _memorySize = PAGE_SIZE * MAX_PAGES;

    // Derived addresses
    uint8_t* _ramBase = nullptr;
    uint8_t* _cacheBase = nullptr;
    uint8_t* _miscBase = nullptr;
    uint8_t* _romBase = nullptr;

    MemoryBankModeEnum _bank_mode[4];	// Mode for each of four banks
    uint8_t* _bank_read[4];				// Memory pointers to RAM/ROM/Cache 16k blocks mapped to four Z80 memory windows
    uint8_t* _bank_write[4];			// Memory pointers to RAM/ROM/Cache 16k blocks mapped to four Z80 memory windows

    // Memory access tracker
    MemoryAccessTracker* _memoryAccessTracker = nullptr;     // Flexible memory access tracking system
    
    // Feature-gate flags
    bool _feature_memorytracking_enabled = false;
    bool _feature_breakpoints_enabled = false;

    bool _isPage0ROM48k;
    bool _isPage0ROM128k;
    bool _isPage0ROMDOS;
    bool _isPge0ROMService;

public:
    // Base addresses for memory classes
    inline uint8_t* RAMBase() { return _ramBase; };		// Get starting address for RAM
    inline uint8_t* CacheBase() { return _cacheBase; };		// Get starting address for Cache
    inline uint8_t* MiscBase() { return _miscBase; };
    inline uint8_t* ROMBase() { return _romBase; };		// Get starting address for ROM

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

    /// Map ZX-Spectrum memory to a filesystem path for external access
    void AllocateAndExportMemoryToMmap();
    /// Unmap the memory from the filesystem
    void UnmapMemory();
    void SyncToDisk();
    
    // Update feature cache (call when features change at runtime)
    void UpdateFeatureCache();
    /// endregion </Initialization>

    /// region <Emulation memory interface methods>
public:
    static MemoryInterface* GetFastMemoryInterface();
    static MemoryInterface* GetDebugMemoryInterface();

    uint8_t MemoryReadFast(uint16_t addr, bool isExecution);
    uint8_t MemoryReadDebug(uint16_t addr, bool isExecution);
    void MemoryWriteFast(uint16_t addr, uint8_t value);
    void MemoryWriteDebug(uint16_t addr, uint8_t value);

    /// endregion </Emulation memory interface methods>

    /// region <Runtime methods>
public:
    void SetROMMode(ROMModeEnum mode);

    void UpdateZ80Banks();
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
    void SetROMPageFlags();
    /// endregion </Service methods>

    /// region <Bank / Page identification helpers>
public:
    bool IsCurrentROM48k() { return _isPage0ROM48k; };
    bool isCurrentROM128k() { return _isPage0ROM128k; };
    bool isCurrentROMDOS() { return _isPage0ROMDOS; };
    bool isCurrentROMService() { return _isPge0ROMService; };
    /// endregion <Bank / Page identification helpers>

    /// region  <Address helper methods>

    uint8_t GetZ80BankFromAddress(uint16_t address);

    uint8_t* RAMPageAddress(uint16_t page);
    uint8_t* ROMPageHostAddress(uint8_t page);

    uint16_t GetRAMPageFromAddress(uint8_t* hostAddress);
    uint16_t GetROMPageFromAddress(uint8_t* hostAddress);

    size_t GetPhysicalOffsetForZ80Address(uint16_t address);
    size_t GetPhysicalOffsetForZ80Bank(uint8_t bank);

    uint8_t* GetPhysicalAddressForZ80Page(uint8_t bank);

    uint8_t* MapZ80AddressToPhysicalAddress(uint16_t address);			    // Remap address to the bank. Important! inline for this method for some reason leads to MSVC linker error (not found export function)
    MemoryPageDescriptor MapZ80AddressToPhysicalPage(uint16_t address);     // Determines current bank for Z80 address specified

    MemoryBankModeEnum GetMemoryBankMode(uint8_t bank);

    uint8_t DirectReadFromZ80Memory(uint16_t address);                      // Read from Z80 memory (actual pages config) without triggering any debug logic
    void DirectWriteToZ80Memory(uint16_t address, uint8_t value);           // Write to Z80 memory (actual pages config) without triggering any debug logic

    /// endregion  </Address helper methods>

    // Atomic internal methods (but accessible for testing purposes)
public:
    void DefaultBanksFor48k();

    /// region <Debug methods>
public:
    void SetROM48k(bool updatePorts = false);
    void SetROM128k(bool updatePorts = false);
    void SetROMDOS(bool updatePorts = false);
    void SetROMSystem(bool updatePorts = false);

    std::string GetBankNameForAddress(uint16_t address);
    std::string GetCurrentBankName(uint8_t bank);
    std::string DumpMemoryBankInfo();
    std::string DumpAllMemoryRegions();
    /// endregion <Debug methods>

    /// region <Memory access tracking>
    inline MemoryAccessTracker& GetAccessTracker() { return *_memoryAccessTracker; }
    /// endregion </Memory access tracking>
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

    using Memory::_memoryAccessTracker;
    using Memory::_isPage0ROM48k;
    using Memory::_isPage0ROM128k;
    using Memory::_isPage0ROMDOS;
    using Memory::_isPge0ROMService;
    using Memory::_bank_mode;
    using Memory::_bank_read;
    using Memory::_bank_write;
};
#endif // _CODE_UNDER_TEST