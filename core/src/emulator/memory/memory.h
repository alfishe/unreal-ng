#pragma once
#include "debugger/ttd/ttdphyspage.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "stdafx.h"

class MemoryAccessTracker;
class HostBusOverlay;
class Z80;
class FeatureManager;
class UlaContention;
class ScorpionRomWindow;  // ProfROM quadrant policy - owned by ScorpionMemory only
namespace ttd { class TTDDirtyTracker; }

// Max RAM size is 4MBytes. Each model has own limits. Max ram used for ZX-Evo / TSConf
// MAX_RAM_PAGES and PAGE defined in platform.h
constexpr size_t MAX_RAM_SIZE = MAX_RAM_PAGES * PAGE_SIZE;

/// Return code when host memory address cannot be mapped to any RAM/ROM page
const uint16_t MEMORY_UNMAPPABLE = 0xFFFF;

/// region <Structures>

enum MemoryBankModeEnum : uint8_t
{
    BANK_ROM = 0,
    BANK_RAM = 1,
    BANK_CACHE = 2,
    BANK_INVALID = 0xFF  // Sentinel for invalid/unknown page in snapshot loaders
};

enum ROMModeEnum : uint8_t
{
    RM_NOCHANGE = 0,  // Do not make any changes comparing to previous memory pages state
    RM_SOS,           // Turn on SOS ROM (48k)
    RM_DOS,           // Turn on DOS ROM
    RM_SYS,           // Turn on System / Shadow ROM
    RM_128,           // Turn on SOS128 ROM
    RM_CACHE          // Turn on ZX-Evo / TSConf cache into ROM page space
};

// Indicators for memory access (registered in membits array)
enum MemoryBitsEnum : uint8_t
{
    MEMBITS_R = 0x01,  // Read
    MEMBITS_W = 0x02,  // Write
    MEMBITS_X = 0x04,  // Execute
};

struct MemoryPageDescriptor
{
    MemoryBankModeEnum mode = BANK_INVALID;
    uint8_t page = 0xFF;          ///< ROM, RAM or cache (fast RAM) page; 0xFF with BANK_INVALID
    uint16_t addressInPage = 0;
};

// Memory interface descriptor
//  Read callback to access memory cell (byte)
//  Write callback - to MemoryWrite data into memory cell (byte)
typedef uint8_t (Memory::*MemoryReadCallback)(uint16_t addr, bool isExecution);
typedef void (Memory::*MemoryWriteCallback)(uint16_t addr, uint8_t val);
struct MemoryInterface
{
    MemoryInterface() = delete;                        // Disable default constructor. C++ 11 feature
    MemoryInterface(const MemoryInterface&) = delete;  // Disable copy constructor. C++ 11 feature

    /// readM1: the opcode fetch (Z80::rdM1); the plain read unless the interface does more on an M1 (ULA snow)
    MemoryInterface(MemoryReadCallback read, MemoryWriteCallback write, MemoryReadCallback readM1 = nullptr)
    {
        MemoryRead = read;
        MemoryWrite = write;
        MemoryReadM1 = readM1 ? readM1 : read;
    };

    MemoryReadCallback MemoryRead;
    MemoryWriteCallback MemoryWrite;
    MemoryReadCallback MemoryReadM1;
};

/// endregion </Structures>

// Every real RAM page must be representable, and the "no page" sentinel must
// be none of them.
static_assert(MAX_RAM_PAGES - 1 <= ttd::kPhysPageMax, "RAM page ceiling exceeds ttd::PhysPage range");
static_assert(ttd::kPhysPageNone >= MAX_RAM_PAGES, "kPhysPageNone collides with a real RAM page");

class Memory
{
    friend class PortDecoder;  // Allow PortDecoder to access _memoryAccessTracker

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

    // Used by the contended interfaces only (SetContentionDependencies)
    Z80* _contentionCpu = nullptr;
    UlaContention* _contentionUla = nullptr;

#ifdef _WIN32
    HANDLE _mappedMemoryHandle = INVALID_HANDLE_VALUE;
#else
    int _mappedMemoryFd = -1;
#endif  // _WIN32

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

    MemoryBankModeEnum _bank_mode[4];  // Mode for each of four banks
    // Memory pointers to RAM/ROM/Cache 16k blocks mapped to four Z80 memory windows. Never null
    // once the constructor ran: every mapping path stores a valid page (DirectReadFromZ80Memory
    // and the debugger read them from other threads)
    uint8_t* _bank_read[4] = {};
    uint8_t* _bank_write[4] = {};

    /// Set by a model whose windows may read differently from the mapped page (a bus redirect):
    /// DirectReadFromZ80Memory then asks ToolReadRedirect. False on every other machine
    bool _toolReadRedirect = false;
    /// What the CPU would read at `addr` given the mapped page's byte `normal`, side-effect free
    virtual uint8_t ToolReadRedirect([[maybe_unused]] uint16_t addr, uint8_t normal) const { return normal; }

    /// Cached RAM page numbers per bank for TTD hot path optimization.
    /// Updated only when bank mapping changes (SetRAMPageToBank*).
    /// Value is ttd::kPhysPageNone when bank is not RAM (ROM/Cache mode) - a
    /// value outside 0..255, so RAM page 255 of a 4 MB machine stays a page.
    /// Avoids expensive GetRAMPageForBank() pointer arithmetic on every write.
    ttd::PhysPage _bank_ram_page_cache[4] = {ttd::kPhysPageNone, ttd::kPhysPageNone,
                                             ttd::kPhysPageNone, ttd::kPhysPageNone};

    /// Windows 1/2 currently forced by the debugger (SetDebuggerRAMPageToBank);
    /// cleared whenever UpdateZ80Banks() re-derives the mapping from the latches
    bool _debugger_bank_forced[4] = {false, false, false, false};
    uint16_t _debugger_bank_base[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};  // page before the force

    // Memory access tracker
    MemoryAccessTracker* _memoryAccessTracker = nullptr;  // Flexible memory access tracking system

    // TTD dirty tracker (per TDD §6.2). Owned by Memory because the write hook
    // lives in MemoryWriteDebug and the bank->page lookup is already here.
    // Always constructed (fixed ~64 byte cost); MarkDirty is a no-op when
    // the cached _feature_ttd_enabled flag is false.
    ttd::TTDDirtyTracker* _ttdDirtyTracker = nullptr;

    // Host bus overlay (hostbusoverlay.h): set only through Core (AddBusOverlay / RemoveBusOverlay),
    // read only by the overlay memory interfaces
    HostBusOverlay* _busOverlay = nullptr;

    // Feature-gate flags
    bool _feature_memorytracking_enabled = false;
    bool _feature_breakpoints_enabled = false;
    bool _feature_sharedmemory_enabled = false;
    bool _feature_ttd_enabled = false;  // mirrors Features::kTimeTravel, cached for the write hot path
    bool _feature_hud_enabled = false;  // mirrors Features::kHUD, zero overhead when disabled

    // Frame-level page switch tracking (emit once per frame at frame end)
    struct PageSwitchTracker
    {
        uint8_t minPage = 0xFF;
        uint8_t maxPage = 0;
        uint8_t currentPage = 0;
        uint8_t switchCount = 0;

        void reset(uint8_t page) { minPage = maxPage = currentPage = page; switchCount = 0; }
        void recordSwitch(uint8_t page)
        {
            if (page < minPage) minPage = page;
            if (page > maxPage) maxPage = page;
            currentPage = page;
            if (switchCount < 255) ++switchCount;
        }
        bool hadActivity() const { return switchCount > 0; }
    };
    PageSwitchTracker _ramSwitchTracker;
    PageSwitchTracker _romSwitchTracker;

    bool _isPage0ROM48k;
    bool _isPage0ROM128k;
    bool _isPage0ROMDOS;
    bool _isPge0ROMService;

public:
    // Base addresses for memory classes
    inline uint8_t* RAMBase()
    {
        return _ramBase;
    };  // Get starting address for RAM
    inline uint8_t* CacheBase()
    {
        return _cacheBase;
    };  // Get starting address for Cache
    inline uint8_t* MiscBase()
    {
        return _miscBase;
    };
    inline uint8_t* ROMBase()
    {
        return _romBase;
    };  // Get starting address for ROM

    // Get the shared memory filepath (empty if using heap allocation)
    inline const std::string& GetMappedMemoryFilepath() const
    {
        return _mappedMemoryFilepath;
    }

    // Check if shared memory is currently enabled and active
    inline bool IsSharedMemoryEnabled() const
    {
        return _feature_sharedmemory_enabled && !_mappedMemoryFilepath.empty();
    }

    // Shortcuts to ROM pages. A model that has no such ROM leaves the
    // corresponding pointer null (see rom.cpp) — a 48K machine has neither a
    // TR-DOS nor a service ROM.
    uint8_t* base_sos_rom;
    uint8_t* base_dos_rom;
    uint8_t* base_128_rom;
    uint8_t* base_sys_rom;

    /// @brief Physical RAM page currently mapped behind a Z80 address.
    /// Returns ttd::kPhysPageNone when that bank holds ROM or cache.
    /// Cheap: reads the per-bank cache maintained by SetRAMPageToBank*.
    inline ttd::PhysPage GetPhysPageForZ80Address(uint16_t addr) const
    {
        return _bank_ram_page_cache[(addr >> 14) & 0b11];
    }

    /// @brief The page time travel names a byte of window `bank` by: its RAM page, or - when the window shows the
    /// fast RAM ("cache") - a virtual page of that space (ttdphyspage.h); kPhysPageNone for ROM
    inline ttd::PhysPage TtdPageOfBank(uint8_t bank) const
    {
        const ttd::PhysPage ram = _bank_ram_page_cache[bank & 3];
        return ram != ttd::kPhysPageNone ? ram : TtdSpacePageOfBank(bank);
    }
    /// The fast RAM's virtual page behind window `bank`, or kPhysPageNone (out of line: ROM and cache windows only)
    ttd::PhysPage TtdSpacePageOfBank(uint8_t bank) const;

    /// @brief A write or a data read of a byte the RAM-page hooks do not see - another memory space's virtual page
    /// and the offset in it, or a RAM page an engine writes outside the CPU's bus (the Sprinter's accelerator) and
    /// its Z80 address: the write journal, the access probe and the coverage index, as MemoryWriteDebug /
    /// MemoryReadDebug do for the CPU's RAM accesses. `write` false: a read
    void TtdNoteAccess(ttd::PhysPage page, uint16_t addr, uint8_t value, bool write);
    /// A watchpoint on a page of another memory space (the Sprinter's video RAM, "vramN"): the access at `offset`
    /// of that page, made through the CPU address `addr`, stops the run like a CPU-address watchpoint
    void CheckSpaceWatch(ttd::PhysPage page, uint16_t offset, uint16_t addr, bool write);

    /// @brief Does the active model have a TR-DOS (Beta Disk) ROM?
    ///
    /// TR-DOS paging is gated structurally by the CF_SETDOSROM session flags
    /// (Z80::Z80Step), which are only armed when a Beta128 is present, so this
    /// is not on that path. It remains as a cheap query for code that needs to
    /// know whether the model has the ROM at all.
    inline bool HasDosRom() const { return base_dos_rom != nullptr; }

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    Memory() = delete;  // Disable default constructor. C++ 11 feature
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

    /**
     * @brief Update all cached memory pointers after base memory reallocation.
     * 
     * This method MUST be called whenever the underlying memory buffer changes location
     * (e.g., during heap↔shared memory migration). It updates:
     *   - Derived base addresses (_ramBase, _cacheBase, _miscBase, _romBase)
     *   - All four Z80 bank pointers (_bank_read[], _bank_write[])
     *   - ROM base pointers (base_sos_rom, base_dos_rom, base_128_rom, base_sys_rom)
     *   - Screen's cached video RAM pointer (via RefreshMemoryPointers)
     * 
     * @param oldBase Previous memory base address (before migration)
     * @param newBase New memory base address (after migration)
     * 
     * @warning Failure to call this after memory reallocation causes:
     *   - Z80 reads/writes accessing freed memory (undefined behavior / crash)
     *   - Emulator UI showing stale screen content
     *   - External viewers seeing correct data but emulator frozen
     *   - Screen updating only after software triggers page switching
     * 
     * @note The offset calculation (newBase - oldBase) preserves relative positioning
     *       of all pointers within the memory region.
     */
    void MigratePointersAfterReallocation(uint8_t* oldBase, uint8_t* newBase);

    // Update feature cache (call when features change at runtime)
    void UpdateFeatureCache();
    /// endregion </Initialization>

    /// region <Frame lifecycle>
public:
    void handleFrameStart();
    void handleFrameEnd();
    /// endregion </Frame lifecycle>

    /// region <Emulation memory interface methods>
public:
    static MemoryInterface* GetFastMemoryInterface();
    static MemoryInterface* GetDebugMemoryInterface();
    static MemoryInterface* GetFastContendedMemoryInterface();   // Fast + video memory contention
    static MemoryInterface* GetDebugContendedMemoryInterface();  // Debug + video memory contention
    /// The fast / debug interface, contended or not, plus the installed bus
    /// overlay (neogs-zxdma-design.md §5.2); selected only while one is installed
    static MemoryInterface* GetOverlayMemoryInterface(bool debug, bool contended);

    /// Contended interfaces: the plain access (Plain = Fast / Debug) with the video logic's wait in front of
    /// accesses to a contended slot, every MREQ cycle alike (opcode fetch, operand, data). Selected only
    /// while the machine's contention is in effect (Core::SelectMemoryInterface), so machines without
    /// contention never run this code. Defined in memorycontended.cpp
    /// Stats: count the accesses and waits (UlaContention::CountAccess) - the Debug instantiation only
    template <MemoryReadCallback Plain, bool Stats>
    uint8_t MemoryReadContended(uint16_t addr, bool isExecution);
    template <MemoryWriteCallback Plain, bool Stats>
    void MemoryWriteContended(uint16_t addr, uint8_t value);

    /// The opcode fetch of the contended interfaces: the read, then the refresh that follows it, which snows
    /// on the Ferranti ULA while I points into slow memory (docs/inprogress/2026-09-29-ula-snow/tdd.md). Only
    /// the contended interfaces carry it, so the machines without contention pay nothing
    template <MemoryReadCallback Read>
    uint8_t MemoryReadM1Snow(uint16_t addr, bool isExecution);

    /// The CPU and contention component the contended interfaces use (Core::Init)
    void SetContentionDependencies(Z80* cpu, UlaContention* ula)
    {
        _contentionCpu = cpu;
        _contentionUla = ula;
    }

    /// Read pair is virtual: model derivatives whose silicon reacts to bus
    /// cycles themselves (ScorpionMemory - ProfROM plane strobes / magic-
    /// button release) override it and run their effects before the byte is
    /// served. The write pair stays non-virtual - no model reacts to writes.
    /// The fast read ignores isExecution (no per-access tracking there), so
    /// the parameter is marked maybe_unused
    virtual uint8_t MemoryReadFast(uint16_t addr, [[maybe_unused]] bool isExecution);
    /// A halted CPU's idle opcode fetch at `addr` through the fast read, repeated: true when it reads a byte and
    /// nothing else (an engine may then run such fetches in one go, Z84C15Engine). A model whose read has a side
    /// effect in some window (a bus cycle on a card) answers false there
    virtual bool RepeatFetchIsPure(uint16_t addr) const
    {
        (void)addr;
        return true;
    }
    virtual uint8_t MemoryReadDebug(uint16_t addr, bool isExecution);
    void MemoryWriteFast(uint16_t addr, uint8_t value);
    void MemoryWriteDebug(uint16_t addr, uint8_t value);

    /// A model whose memory is not four 16K windows supplies its own plain (Fast / Debug) interface here;
    /// Core::SelectMemoryInterface uses it instead of the stock one while no host bus overlay is installed.
    /// The default - no interface of its own
    virtual MemoryInterface* ModelMemoryInterface([[maybe_unused]] bool debug, [[maybe_unused]] bool contended) { return nullptr; }

protected:
    /// The debug access bookkeeping that follows the byte (tracking, TTD, breakpoints): MemoryReadDebug /
    /// MemoryWriteDebug call these, and so does a derivative with its own mapping
    void ReadDebugEffects(uint16_t addr, uint8_t result, bool isExecution, ttd::PhysPage physPage);
    void WriteDebugEffects(uint16_t addr, uint8_t value, ttd::PhysPage physPage);

public:

    /// The inner access (Fast / Debug, contended or not), then the bus overlay
    /// for addresses in its window: the video logic's wait comes first, as on
    /// the bus, then the overlay decides what the CPU gets. Only reachable
    /// while an overlay is installed
    template <MemoryReadCallback Inner>
    uint8_t MemoryReadOverlay(uint16_t addr, bool isExecution);
    /// The overlay interfaces' opcode fetch (MemoryInterface::MemoryReadM1): the overlay's onReadM1
    template <MemoryReadCallback Inner>
    uint8_t MemoryReadOverlayM1(uint16_t addr, bool isExecution);
    template <MemoryWriteCallback Inner>
    void MemoryWriteOverlay(uint16_t addr, uint8_t value);

    /// Core::SelectMemoryInterface keeps this in step with the Z80's interface
    void SetBusOverlay(HostBusOverlay* overlay) { _busOverlay = overlay; }
    HostBusOverlay* GetBusOverlay() const { return _busOverlay; }

    /// endregion </Emulation memory interface methods>

    /// region <Runtime methods>
public:
    /// Switch bank 0 to the requested ROM section and refresh the paging state
    /// (port of the original UnrealSpeccy set_mode(); used for RESET= boot modes)
    void SetROMMode(ROMModeEnum mode);

    void UpdateZ80Banks();

protected:
    /// Model-specific latch-to-bank translation. A derivative that owns the
    /// whole rebuild (ScorpionMemory for MM_SCORP / MM_PROFSCORP, design §3)
    /// overrides this to return true, and UpdateZ80Banks() skips its generic
    /// body - every base model stays byte-identical
    virtual bool UpdateModelBanks() { return false; }

public:
    /// RAM bank mask from config.ramsize (KB): 256 KB → 0x0F, 1024 KB → 0x3F
    uint8_t GetRamMask() const;

    /// ROM loader completion hook: derivatives with derived ROM geometry
    /// (ScorpionMemory, ProfROM image masks) configure themselves from the
    /// validated bank count; the default is a no-op
    virtual void OnRomLoaded(uint16_t imageBanks) { (void)imageBanks; }

    /// ProfROM quadrant window - null unless the model derivative owns the
    /// silicon (ScorpionMemory); quadrant state itself lives in
    /// EmulatorState / TEMP
    virtual ScorpionRomWindow* GetScorpionRomWindow() { return nullptr; }

    void SetROMPage(uint16_t page, bool updatePorts = false);
    void SetROMPageToBank(uint8_t bank, uint16_t page);  // Map any of the 4 Z80 banks to a ROM page (bank writes -> trash)
    /// A RAM bank that reads normally and drops its writes (ZX-Evo `#xBF7` write protect): the write pointer goes to
    /// the trash page like a ROM bank's. Call after the bank's page is mapped
    void SetBankWriteProtected(uint8_t bank);

private:
    uint16_t MappedRAMPage(uint8_t bank);

public:
    void SetRAMPageToBank0(uint16_t page, bool updatePorts = false);
    void SetRAMPageToBank1(uint16_t page);
    void SetRAMPageToBank2(uint16_t page);
    void SetRAMPageToBank3(uint16_t page, bool updatePorts = false);

    /// Debugger-forced mapping of RAM window 1 or 2 (0x4000 / 0x8000). No port latch
    /// describes it, so the next UpdateZ80Banks() (any paging write, any TTD restore)
    /// derives the window from the latches again and drops it - exactly like hardware.
    /// The forced state is remembered so TTD checkpoints can carry it (see
    /// GetDebuggerBankOverride) instead of silently losing it on restore.
    void SetDebuggerRAMPageToBank(uint8_t bank, uint16_t page);
    /// Forced RAM page of window 1 or 2, or MEMORY_UNMAPPABLE when it follows the latches
    uint16_t GetDebuggerBankOverride(uint8_t bank) const;
    /// Undo a debugger-forced window: back to the page it had before the first force
    void RevertDebuggerBankOverride(uint8_t bank);

    bool IsBank0ROM();
    uint16_t GetROMPage();
    uint16_t GetRAMPageForBank0();
    uint16_t GetRAMPageForBank1();
    uint16_t GetRAMPageForBank2();
    uint16_t GetRAMPageForBank3();

    uint16_t GetROMPageForBank(uint8_t bank);
    uint16_t GetRAMPageForBank(uint8_t bank);
    uint16_t GetPageForBank(uint8_t bank);  // Returns absolute page number without distinction to RAM/Cache/Misc/ROM -
                                            // since they all located in the same block

    /// endregion </Runtime methods>

    /// region <Service methods>
    void LoadContentToMemory(uint8_t* contentBuffer, size_t size, uint16_t z80address);
    void LoadRAMPageData(uint8_t page, uint8_t* fromBuffer, size_t bufferSize);
    void SetROMPageFlags();

    /// Contention cache of a slot / of all four (after the machine's contention rule changed)
    void UpdateSlotContention(uint8_t slot);
    virtual void RefreshSlotContention();
    void RecordROMPageSwitch();
    /// endregion </Service methods>

    /// region <Bank / Page identification helpers>
public:
    bool IsCurrentROM48k()
    {
        return _isPage0ROM48k;
    };
    bool isCurrentROM128k()
    {
        return _isPage0ROM128k;
    };
    bool isCurrentROMDOS()
    {
        return _isPage0ROMDOS;
    };
    bool isCurrentROMService()
    {
        return _isPge0ROMService;
    };
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

    uint8_t* MapZ80AddressToPhysicalAddress(
        uint16_t address);  // Remap address to the bank. Important! inline for this method for some reason leads to
                            // MSVC linker error (not found export function)
    MemoryPageDescriptor MapZ80AddressToPhysicalPage(
        uint16_t address);  // Determines current bank for Z80 address specified

    MemoryBankModeEnum GetMemoryBankMode(uint8_t bank);
    /// Window 0-3 maps ROM: the hot-path form for bus logic that tells ROM from RAM (EvoTurboOverlay)
    bool IsWindowRom(uint8_t window) const { return _bank_mode[window & 0x03] == BANK_ROM; }
    /// A CPU write to the window reaches the mapped page (false: ROM, or RAM a mapper keeps write-protected, such as
    /// TS-Conf window 0 without W0_WE - those writes go to the trash page). The debugger's "read_write" / "writable"
    bool IsWindowWritable(uint8_t window) const { return _bank_write[window & 0x03] != _memory + TRASH_MEMORY_OFFSET; }

    uint8_t DirectReadFromZ80Memory(
        uint16_t address);  // Read from Z80 memory (actual pages config) without triggering any debug logic
    void DirectWriteToZ80Memory(
        uint16_t address,
        uint8_t value);  // Write to Z80 memory (actual pages config) without triggering any debug logic

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
    inline MemoryAccessTracker& GetAccessTracker()
    {
        return *_memoryAccessTracker;
    }

    /// region <TTD dirty tracker access>
    // Exposed so the per-frame capture orchestrator (lands in P1 Item 4)
    // can call CollectAndClear at OnFrameEnd, and so session lifecycle
    // (ttd clear / invalidation hooks, lands in P1 Item 6) can call
    // ResetSession. Tests also reach in to verify the hook is firing.
    inline ttd::TTDDirtyTracker* GetTTDDirtyTracker() { return _ttdDirtyTracker; }

    /// @brief A tool (script) write with the CPU's view of the address: RAM is
    /// written and reaches TTD like DirectWriteToZ80Memory, ROM stays
    /// write-protected as it is for a CPU write.
    void ToolWriteToZ80Memory(uint16_t address, uint8_t value);

    /// @brief Tell TTD a physical RAM page was written behind the CPU's back
    /// (a tool editing the page directly), so the next checkpoint captures it.
    void MarkRamPageEdited(uint16_t page);
    /// endregion </TTD dirty tracker access>
    /// endregion </Memory access tracking>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing /
// benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class MemoryCUT : public Memory
{
public:
    MemoryCUT(EmulatorContext* context) : Memory(context) {};

public:
    using Memory::_cacheBase;
    using Memory::_memory;
    using Memory::_miscBase;
    using Memory::_ramBase;
    using Memory::_romBase;

    using Memory::_bank_mode;
    using Memory::_bank_read;
    using Memory::_bank_write;
    using Memory::_isPage0ROM128k;
    using Memory::_isPage0ROM48k;
    using Memory::_isPage0ROMDOS;
    using Memory::_isPge0ROMService;
    using Memory::_memoryAccessTracker;
    using Memory::_ttdDirtyTracker;
    using Memory::_feature_ttd_enabled;
    using Memory::_bank_ram_page_cache;
    
    // HUD page-switch tracking
    using Memory::_feature_hud_enabled;
    using Memory::_ramSwitchTracker;
    using Memory::_romSwitchTracker;

    // ROM base pointers for testing ROM switching
    using Memory::base_dos_rom;
    using Memory::base_sos_rom;
    using Memory::base_128_rom;
    using Memory::base_sys_rom;
};
#endif  // _CODE_UNDER_TEST