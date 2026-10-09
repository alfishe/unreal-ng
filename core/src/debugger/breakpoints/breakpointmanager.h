#pragma once
#include <array>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "stdafx.h"

/// region <Types>

constexpr uint16_t BRK_INVALID = 0xFFFF;

///
/// Type of breakpoint. Types can be combined as a bitmask
///

enum BreakpointTypeEnum : uint8_t
{
    BRK_MEMORY = 0,  // Memory access (Read | Write | Execution)
    BRK_IO,          // I/O port access (Read | Write)
    BRK_KEYBOARD,    // Key press event
    // BRK_INTERRUPT     // Interrupt event
};

constexpr uint8_t BRK_MEM_NONE = 0x00;
constexpr uint8_t BRK_MEM_EXECUTE = 0x01;
constexpr uint8_t BRK_MEM_READ = 0x02;
constexpr uint8_t BRK_MEM_WRITE = 0x04;
constexpr uint8_t BRK_MEM_ALL = 0xFF;

constexpr uint8_t BRK_IO_NONE = 0x00;
constexpr uint8_t BRK_IO_IN = 0x01;
constexpr uint8_t BRK_IO_OUT = 0x02;
constexpr uint8_t BRK_IO_ALL = 0xFF;

constexpr uint8_t BRK_KEY_NONE = 0x00;
constexpr uint8_t BRK_KEY_PRESS = 0x01;
constexpr uint8_t BRK_KEY_RELEASE = 0x02;
constexpr uint8_t BRK_KEY_ALL = 0xFF;

enum BreakpointAddressMatchEnum : uint8_t
{
    BRK_MATCH_ADDR = 0,  // CPU addresses z80address..end, whatever page is mapped there
    BRK_MATCH_BANK_ADDR  // Physical: page + offsets (z80address & #3FFF)..(end & #3FFF), through whatever slot
                         // shows the page; with slotOnly only through the slot of z80address
};

/// When a matching access stops the run (conditional-breakpoints design F5). hitCount counts every matching
/// access; the policy decides which of them stop
enum BreakpointHitModeEnum : uint8_t
{
    BRK_HIT_ALWAYS = 0,  // every hit stops
    BRK_HIT_EQUAL,       // only the hitTarget-th hit
    BRK_HIT_AT_LEAST,    // the hitTarget-th hit and every one after it
    BRK_HIT_MULTIPLE     // every hitTarget-th hit
};

///
/// Descriptor for a single address / port breakpoint
///
struct BreakpointDescriptor
{
    uint16_t breakpointID =
        BRK_INVALID;  // Unique breakpoint ID (sequence is shared across all memory and IO breakpoints)
    uint32_t keyAddress = 0xFFFF'FFFF;  // Composite bank + address key for fast lookup

    BreakpointTypeEnum type = BRK_MEMORY;
    BreakpointAddressMatchEnum matchType = BRK_MATCH_ADDR;

    uint8_t memoryType = BRK_MEM_READ | BRK_MEM_WRITE | BRK_MEM_EXECUTE;
    uint8_t ioType = BRK_IO_IN | BRK_IO_OUT;
    uint8_t keyType = BRK_KEY_PRESS | BRK_KEY_RELEASE;

    // Used if breakpoint is set to any matching address in Z80 address space (independently of the bank mapping)
    uint16_t z80address = 0xFFFF;
    // Range end, inclusive (isRange). A physical range stays in one page: end & #3FFF >= z80address & #3FFF
    uint16_t z80addressEnd = 0xFFFF;
    bool isRange = false;
    uint16_t EndAddress() const { return isRange ? z80addressEnd : z80address; }

    // Physical breakpoint restricted to the slot of z80address (today's "only while page X is at this address")
    bool slotOnly = false;
    // Port breakpoint: matches when (port & portMask) == (z80address & portMask) (design F7)
    uint16_t portMask = 0xFFFF;

    // Hit counting (design F5)
    BreakpointHitModeEnum hitMode = BRK_HIT_ALWAYS;
    uint32_t hitTarget = 0;
    uint32_t hitCount = 0;

    // Used if breakpoint is set to any matching address in specific memory page (independently of Z80 address)
    uint8_t page = 0xFF;                     // Page number (ROM 0-63 or RAM 0-255)
    MemoryBankModeEnum pageType = BANK_RAM;  // Memory type: BANK_ROM, BANK_RAM, or BANK_CACHE
    uint16_t bankOffset = 0xFFFF;            // Offset within the page (0-0x3FFF)
    // A watchpoint on a page of another memory space ("vram5": the Sprinter's video RAM, ttdphyspage.h virtual
    // pages): offsets (z80address & #3FFF)..(end & #3FFF) of that page, matched only by that memory's hooks
    // (HandleSpaceAccess), never by CPU addresses. 0xFFFF: none
    uint16_t spacePage = 0xFFFF;

    bool active = true;   // Breakpoint can be temporarily disabled
    bool hidden = false;  // Internal/temporary breakpoint (e.g. step-over, step-out, traps)

    std::string owner =
        "interactive";  // Owner: "interactive" for user/CLI, or "analyzer_manager" if set by analyzer manager
    std::string note;   // Annotation for the breakpoint
    std::string group = "default";  // Group name for organizing breakpoints
};

typedef std::unordered_map<uint32_t, BreakpointDescriptor*> BreakpointMapByAddress;
typedef std::unordered_map<uint16_t, BreakpointDescriptor*> BreakpointMapByPort;
typedef std::map<uint16_t, BreakpointDescriptor*> BreakpointMapByID;
typedef std::map<uint8_t, BreakpointMapByAddress> BreakpointMapByBank;

/// Hot-path state (hotpath-matching-design.md §3): per kind a gate and a one-bit-per-address filter.
/// Read by the emulation thread without locks; written by BreakpointManager when the set changes.
/// Kind index: 0 exec, 1 read (data reads and opcode / operand fetches), 2 write; ports: 0 in, 1 out
constexpr int BRK_KIND_EXEC = 0;
constexpr int BRK_KIND_READ = 1;
constexpr int BRK_KIND_WRITE = 2;
constexpr int BRK_PORT_IN = 0;
constexpr int BRK_PORT_OUT = 1;

struct BreakpointHotState
{
    uint8_t hasExec = 0;     // 1 if any active execute breakpoint exists
    uint8_t hasRead = 0;     // 1 if any active read breakpoint exists
    uint8_t hasWrite = 0;    // 1 if any active write breakpoint exists
    uint8_t hasPortIn = 0;   // 1 if any active port-in breakpoint exists
    uint8_t hasPortOut = 0;  // 1 if any active port-out breakpoint exists
    uint8_t hasSpace = 0;    // 1 if any active watchpoint on another memory space exists (the Sprinter's video RAM)
    uint8_t _padding[2] = {0, 0};

    // "There may be a breakpoint here": one bit per CPU address per kind (8 KB each, L1-sized). A physical
    // breakpoint marks its offsets in all four slots (globalbits, design §6): a set bit can be a false
    // "maybe" that the resolve answers
    uint64_t memoryFilter[3][1024] = {};
    // One bit per port per direction, masks expanded
    uint64_t portFilter[2][1024] = {};

    bool MemoryBit(int kind, uint16_t address) const { return (memoryFilter[kind][address >> 6] >> (address & 63)) & 1u; }
    bool PortBit(int direction, uint16_t port) const { return (portFilter[direction][port >> 6] >> (port & 63)) & 1u; }
};

/// What every surface can ask for: the debugger protocol's Breakpoint fields this manager supports
struct BreakpointSpec
{
    BreakpointTypeEnum type = BRK_MEMORY;
    uint8_t access = BRK_MEM_EXECUTE;  // BRK_MEM_* for memory, BRK_IO_* for ports
    uint16_t address = 0;              // CPU address, or port
    bool hasEnd = false;
    uint16_t addressEnd = 0;           // inclusive
    bool hasPage = false;              // physical breakpoint (memory only)
    uint8_t page = 0;
    MemoryBankModeEnum pageType = BANK_RAM;
    bool slotOnly = false;             // physical, only through the slot of `address`
    uint16_t spacePage = 0xFFFF;       // a page of another memory space ("vram5"); `address` & #3FFF is the offset
    uint16_t portMask = 0xFFFF;        // ports only
    BreakpointHitModeEnum hitMode = BRK_HIT_ALWAYS;
    uint32_t hitTarget = 0;
    std::string note;
    std::string group;                 // empty: "default"
    std::string owner;                 // empty: interactive
};

/// endregion </Types>

class BreakpointManager
{
    // region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
    const uint16_t _SUBMODULE = PlatformDebuggerSubmodulesEnum::SUBMODULE_DEBUG_BREAKPOINTS;
    ModuleLogger* _logger = nullptr;
    // endregion </ModuleLogger definitions for Module/Submodule>

    // region <Constants>
public:
    /// Owner ID for interactive (user/CLI) breakpoints
    static constexpr const char* OWNER_INTERACTIVE = "interactive";
    // endregion </Constants>

    // region <Fields>
protected:
    EmulatorContext* _context;
    BreakpointMapByAddress _breakpointMapByAddress;
    BreakpointMapByPort _breakpointMapByPort;
    BreakpointMapByID _breakpointMapByID;

    // Incremental counter to generate new breakpoint IDs
    // Note: no breakpoint IDs reuse allowed
    uint16_t _breakpointIDSeq = 0;

    // Last triggered breakpoint ID (for automation API queries)
    uint16_t _lastTriggeredBreakpointID = BRK_INVALID;

    // Hot-path state for fast breakpoint checks: the gates and the filters (hotpath-matching-design.md §3)
    BreakpointHotState _hotState;

    // The resolve side, painted at set time. A head is the id of an interned candidate set: the active
    // breakpoints covering that address, in id order (set 0 = none). The set only changes at a range
    // boundary, so N ranges make at most 2N + 1 sets, shared between kinds and pages
    std::vector<std::vector<BreakpointDescriptor*>> _candidateSets;
    std::vector<uint32_t> _cpuHeads[3];                                      // 64K per armed kind
    struct PageSlice
    {
        std::array<uint32_t, 0x4000> heads{};
    };
    static constexpr size_t kPageIds = 0x300;                                // (type << 8) | page
    std::vector<std::unique_ptr<PageSlice>> _pageSlices[3];                  // kPageIds per armed kind
    bool _hasPageSlices[3] = {};
    std::vector<uint32_t> _portHeads[2];                                     // 64K per armed direction

    // Stepping on from the execution breakpoint the emulator is stopped at: that one access is neither a hit
    // nor counted (Emulator::DirectStepScope arms it, the first instruction disarms it)
    bool _execPassArmed = false;
    uint16_t _execPassAddress = 0;

    // Batch: mutations inside it repaint once, at the end
    int _batchDepth = 0;
    bool _rebuildPending = false;

    // What the surfaces were last told (id -> the fields they show): breakpoints_changed names the ids
    // that differ from it, so no mutation has to report what it touched
    std::map<uint16_t, std::string> _published;
    /// endregion </Fields>

    // region <Constructors / destructors>
public:
    BreakpointManager() = delete;  // Disable default constructors. C++ 11 feature
    BreakpointManager(EmulatorContext* context);
    // One per emulator, owning its descriptors and painted tables: never copied
    BreakpointManager(const BreakpointManager&) = delete;
    BreakpointManager& operator=(const BreakpointManager&) = delete;
    virtual ~BreakpointManager();
    /// endregion </Constructors / destructors>

    /// region <Management methods>
public:
    void ClearBreakpoints();

    uint16_t AddBreakpoint(BreakpointDescriptor* descriptor);
    /// The one entry every surface uses (debugger protocol fields): validates the spec against this machine
    /// (range order, a physical range inside one page, the page exists, masks only on ports, a hit target
    /// for the counting modes) and adds it. BRK_INVALID with the reason in `error`
    uint16_t AddBreakpoint(const BreakpointSpec& spec, std::string& error);
    BreakpointDescriptor* GetBreakpointById(uint16_t breakpointID);
    bool RemoveBreakpoint(BreakpointDescriptor* descriptor);
    bool RemoveBreakpointByID(uint16_t breakpointID);

    size_t GetBreakpointsCount();

    // Get/clear last triggered breakpoint (for automation APIs)
    uint16_t GetLastTriggeredBreakpointID() const
    {
        return _lastTriggeredBreakpointID;
    }
    void ClearLastTriggeredBreakpoint()
    {
        _lastTriggeredBreakpointID = BRK_INVALID;
    }

    /// Structured status info for automation APIs
    struct BreakpointStatusInfo
    {
        bool valid = false;         // True if a breakpoint was found
        uint16_t id = BRK_INVALID;  // Breakpoint ID
        std::string type;           // "memory", "port", "keyboard"
        uint16_t address = 0;       // Z80 address or port number
        std::string access;         // "execute", "read", "write", "in", "out" (comma-separated)
        bool active = false;        // Current enable state
        std::string note;           // User annotation
        std::string group;          // Group name
        std::string page;           // "ram32" for a breakpoint bound to a page, "" otherwise
        std::string pageKind;       // "ram" / "rom" / "cache" with pageNumber (protocol {kind, page}), "" otherwise
        uint8_t pageNumber = 0;
        uint32_t hitCount = 0;      // matching accesses so far (the hit policy decides which stop)
    };

    /// Get structured info about the last triggered breakpoint
    BreakpointStatusInfo GetLastTriggeredBreakpointInfo() const;

    /// Get read-only pointer to hot-path state (for emulation thread fast checks)
    const BreakpointHotState* GetHotState() const
    {
        return &_hotState;
    }
    /// endregion </Management methods>

    /// region <Management assistance methods>

    // Shortcuts for breakpoint creation in Z80 address space
    // owner: OWNER_INTERACTIVE for user/CLI breakpoints, or custom ID for analyzer-owned breakpoints
    uint16_t AddExecutionBreakpoint(uint16_t z80address, const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddMemReadBreakpoint(uint16_t z80address, const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddMemWriteBreakpoint(uint16_t z80address, const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddPortInBreakpoint(uint16_t port, const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddPortOutBreakpoint(uint16_t port, const std::string& owner = OWNER_INTERACTIVE);

    // Page-specific breakpoints (for ROM/RAM/Cache page matching, independently of Z80 address)
    // page: ROM 0-63, RAM 0-255, Cache 0-1
    // pageType: BANK_ROM, BANK_RAM, or BANK_CACHE
    uint16_t AddExecutionBreakpointInPage(uint16_t z80address, uint8_t page, MemoryBankModeEnum pageType,
                                          const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddMemReadBreakpointInPage(uint16_t z80address, uint8_t page, MemoryBankModeEnum pageType,
                                        const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddMemWriteBreakpointInPage(uint16_t z80address, uint8_t page, MemoryBankModeEnum pageType,
                                         const std::string& owner = OWNER_INTERACTIVE);

    // Combined breakpoint types
    uint16_t AddCombinedMemoryBreakpoint(uint16_t z80address, uint8_t memoryType,
                                         const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddCombinedPortBreakpoint(uint16_t port, uint8_t ioType, const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddCombinedMemoryBreakpointInPage(uint16_t z80address, uint8_t memoryType, uint8_t page,
                                               MemoryBankModeEnum pageType,
                                               const std::string& owner = OWNER_INTERACTIVE);

    /// A page as the text surfaces write it (debugger protocol): "ram32", "rom3", "cache0", any case, the
    /// number decimal or as 0x.. / #.. / $... False with the reason in `error` for anything else. JSON
    /// carries the same as {kind, page}
    static bool ParsePageSpec(const std::string& text, uint8_t& page, MemoryBankModeEnum& pageType, std::string& error);
    /// A breakpoint's page as the surfaces write it, into the spec: a machine page (ParsePageSpec) or a page of
    /// another memory space, "vram0".."vram15" (the Sprinter's video RAM: offsets of that 16 KB page, read and
    /// write watchpoints, matched through the graphics windows and the accelerator)
    static bool ParsePageInto(const std::string& text, BreakpointSpec& spec, std::string& error);
    /// "ram32" for a breakpoint bound to a page, "" for one that matches the address in any page
    static std::string PageSpecName(const BreakpointDescriptor& breakpoint);
    /// "ram", "rom" or "cache": the protocol's page kind
    static const char* PageKindName(MemoryBankModeEnum pageType);
    /// Whether this machine has the page (RAM: config ramsize; ROM, cache: the emulator's page ceilings)
    bool HasPage(uint8_t page, MemoryBankModeEnum pageType) const;
    /// A hit policy as the text surfaces write it: "5" (the 5th hit only), ">=5" (from the 5th on), "%5"
    /// (every 5th), "" or "always" (every hit). JSON carries the same as hit_mode + hit_target
    static bool ParseHitSpec(const std::string& text, BreakpointHitModeEnum& mode, uint32_t& target, std::string& error);
    /// "5", ">=5", "%5" or "" (always): the text form of a breakpoint's policy
    static std::string HitSpecName(const BreakpointDescriptor& breakpoint);
    /// "always", "equal", "at_least", "multiple": the protocol's hit_mode names (ParseHitModeName reverses it)
    static const char* HitModeName(BreakpointHitModeEnum mode);
    static bool ParseHitModeName(const std::string& text, BreakpointHitModeEnum& mode);
    /// The options the script surfaces (Lua, Python) take, applied to a spec: page "ram5" (empty: none), the range
    /// end `to` (-1: none), slot_only, port mask (-1: none), hits "5" / ">=5" / "%5" (empty: always)
    static bool ApplyScriptOptions(BreakpointSpec& spec, const std::string& page, int32_t to, bool slotOnly, int32_t mask,
                                   const std::string& hits, std::string& error);
    /// Hit counters back to 0: one breakpoint (false for an unknown id) or all
    bool ResetHitCount(uint16_t breakpointID);
    void ResetAllHitCounts();
    /// The execution breakpoint at `address` lets the next instruction start there through without a hit or
    /// a count (stepping on from where the emulator stopped); DisarmExecPass ends it
    void ArmExecPass(uint16_t address)
    {
        _execPassArmed = true;
        _execPassAddress = address;
    }
    void DisarmExecPass() { _execPassArmed = false; }
    /// Many changes at once (an import, a script setting hundreds of breakpoints): inside a batch the hot path
    /// is repainted and the surfaces told once, at EndBatch. Batches nest
    void BeginBatch();
    void EndBatch();
    class Batch
    {
    public:
        explicit Batch(BreakpointManager& manager) : _manager(manager) { _manager.BeginBatch(); }
        ~Batch() { _manager.EndBatch(); }
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;

    private:
        BreakpointManager& _manager;
    };
    /// A memory breakpoint (memoryType: BRK_MEM_* bits) at the address, bound to `pageSpec` when it is not
    /// empty. BRK_INVALID with the reason in `error` for a bad or missing page
    uint16_t AddMemoryBreakpointInPageSpec(uint16_t z80address, uint8_t memoryType, const std::string& pageSpec,
                                           std::string& error);

    // Breakpoint listing
    const BreakpointMapByID& GetAllBreakpoints() const;
    std::string FormatBreakpointInfo(uint16_t breakpointID) const;
    std::string GetBreakpointListAsString(const std::string& newline = "\n") const;

    // Breakpoint activation/deactivation
    bool ActivateBreakpoint(uint16_t breakpointID);
    bool DeactivateBreakpoint(uint16_t breakpointID);
    void ActivateAllBreakpoints();
    void DeactivateAllBreakpoints();
    void ActivateBreakpointsByType(BreakpointTypeEnum type);
    void DeactivateBreakpointsByType(BreakpointTypeEnum type);
    void ActivateMemoryBreakpointsByType(uint8_t memoryType);
    void DeactivateMemoryBreakpointsByType(uint8_t memoryType);
    void ActivatePortBreakpointsByType(uint8_t ioType);
    void DeactivatePortBreakpointsByType(uint8_t ioType);

    // Breakpoint removal by address/port/type
    bool RemoveBreakpointByAddress(uint16_t address);
    bool RemoveBreakpointByPort(uint16_t port);
    void RemoveBreakpointsByType(BreakpointTypeEnum type);
    void RemoveMemoryBreakpointsByType(uint8_t memoryType);
    void RemovePortBreakpointsByType(uint8_t ioType);

    // Breakpoint group management
    uint16_t AddBreakpointToGroup(BreakpointDescriptor* descriptor, const std::string& groupName);
    bool SetBreakpointGroup(uint16_t breakpointID, const std::string& groupName);
    /// The breakpoint's annotation (empty clears it); false for an unknown id
    bool SetBreakpointNote(uint16_t breakpointID, const std::string& note);
    std::vector<std::string> GetBreakpointGroups() const;
    std::vector<uint16_t> GetBreakpointsByGroup(const std::string& groupName) const;
    std::string GetBreakpointListAsStringByGroup(const std::string& groupName) const;
    void ActivateBreakpointGroup(const std::string& groupName);
    void DeactivateBreakpointGroup(const std::string& groupName);
    bool RemoveBreakpointFromGroup(uint16_t breakpointID);
    void RemoveBreakpointGroup(const std::string& groupName);

    // endregion </Management assistance methods>

    // region <Runtime methods>
    // The hot path (hotpath-matching-design.md §4): inline, the gate and one filter bit; only a set bit calls
    // the out-of-line resolve. The callers (Z80 instruction start, Memory debug read / write, the port
    // decoder's debug in / out) compile this check into their own code
public:
    uint16_t HandlePCChange(uint16_t pc)
    {
        if (!_hotState.hasExec || !_hotState.MemoryBit(BRK_KIND_EXEC, pc))
            return BRK_INVALID;
        return ResolveMemory(BRK_KIND_EXEC, pc);
    }
    uint16_t HandleMemoryRead(uint16_t readAddress)
    {
        if (!_hotState.hasRead || !_hotState.MemoryBit(BRK_KIND_READ, readAddress))
            return BRK_INVALID;
        return ResolveMemory(BRK_KIND_READ, readAddress);
    }
    /// An access to a page of another memory space (its own hooks, not the CPU's address): the watchpoint that
    /// stops, or BRK_INVALID. `offset` is in the 16 KB page
    uint16_t HandleSpaceAccess(uint16_t spacePage, uint16_t offset, bool write)
    {
        if (!_hotState.hasSpace)
            return BRK_INVALID;
        return ResolveSpace(spacePage, offset, write);
    }
    uint16_t HandleMemoryWrite(uint16_t writeAddress)
    {
        if (!_hotState.hasWrite || !_hotState.MemoryBit(BRK_KIND_WRITE, writeAddress))
            return BRK_INVALID;
        return ResolveMemory(BRK_KIND_WRITE, writeAddress);
    }
    uint16_t HandlePortIn(uint16_t portAddress)
    {
        if (!_hotState.hasPortIn || !_hotState.PortBit(BRK_PORT_IN, portAddress))
            return BRK_INVALID;
        return ResolvePort(BRK_PORT_IN, portAddress);
    }
    uint16_t HandlePortOut(uint16_t portAddress)
    {
        if (!_hotState.hasPortOut || !_hotState.PortBit(BRK_PORT_OUT, portAddress))
            return BRK_INVALID;
        return ResolvePort(BRK_PORT_OUT, portAddress);
    }
    // endregion </Runtime methods>

    // region <Helper methods>
protected:
    uint16_t GenerateNewBreakpointID();

    uint16_t AddMemoryBreakpoint(BreakpointDescriptor* descriptor);
    uint16_t AddPortBreakpoint(BreakpointDescriptor* descriptor);

    BreakpointDescriptor* FindAddressBreakpoint(uint16_t address);
    BreakpointDescriptor* FindAddressBreakpoint(uint16_t address, const MemoryPageDescriptor& pageInfo);
    BreakpointDescriptor* FindPortBreakpoint(uint16_t port);

    /// A filter "maybe": the candidates of the CPU address and of the page the slot shows; the slot filter,
    /// the hit policy (counting), the TTD-replay suppression. The id of the first candidate that stops
    uint16_t ResolveSpace(uint16_t spacePage, uint16_t offset, bool write);
    uint16_t ResolveMemory(int kind, uint16_t address);
    uint16_t ResolvePort(int direction, uint16_t port);
    /// Walks one candidate set: counts every candidate that matches, returns the first that stops (or
    /// BRK_INVALID); `slot` is the slot of the access for slot-only physical breakpoints (-1: not physical)
    uint16_t WalkCandidates(uint32_t set, int slot, uint16_t first);
    /// Whether a breakpoint covers the access (for the lookups that are not on the hot path)
    static bool CoversMemory(const BreakpointDescriptor& bp, uint16_t address, const MemoryPageDescriptor& page);

    /// Rebuild hot-path filter state from current breakpoint set.
    /// Called after every mutation (add, remove, enable, disable, change); inside a batch only once, at its end.
    void RebuildFilters();
    /// NC_BREAKPOINTS_CHANGED for this emulator with the ids added, removed or changed since the last one
    /// (none: nothing posted). Hidden breakpoints (step-over, traps) are internal and never reported
    void NotifyBreakpointsChanged();
    /// The ids added, removed or changed since the last call (sorted, hidden ones left out); remembers the
    /// current set as published
    std::vector<uint16_t> TakeChangedIds();
    /// The fields a surface shows for one breakpoint, as one comparable string
    static std::string PublishedFields(const BreakpointDescriptor& breakpoint);

    // endregion </Helper methods>
};

#ifdef _CODE_UNDER_TEST

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing /
// benchmark purposes
//
class BreakpointManagerCUT : public BreakpointManager
{
public:
    BreakpointManagerCUT(EmulatorContext* context) : BreakpointManager(context) {}

    using BreakpointManager::_breakpointIDSeq;
    using BreakpointManager::_breakpointMapByAddress;
    using BreakpointManager::_breakpointMapByID;
    using BreakpointManager::_breakpointMapByPort;
    using BreakpointManager::_context;
    using BreakpointManager::_hotState;
    using BreakpointManager::TakeChangedIds;
    using BreakpointManager::_logger;

    using BreakpointManager::AddMemoryBreakpoint;
    using BreakpointManager::AddPortBreakpoint;
    using BreakpointManager::FindAddressBreakpoint;
    using BreakpointManager::FindPortBreakpoint;
    using BreakpointManager::GenerateNewBreakpointID;
    using BreakpointManager::RebuildFilters;
    using BreakpointManager::_candidateSets;
};

#endif  // _CODE_UNDER_TEST
