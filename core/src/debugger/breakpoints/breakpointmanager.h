#pragma once
#include <array>
#include <functional>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
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
    BRK_NEXTREG,     // ZX Next register access (NR read through #253B, NR write by NEXTREG / #253B / the copper)
    BRK_BEAM,        // ZX Next: the beam reaches a place in the raster (line counter cvc of the copper, pixel clock hc)
    BRK_COPPER,      // ZX Next: the copper completes the instruction at a list index (a WAIT that became true, or a MOVE)
    BRK_MAGIC,       // ZX Next: the CPU completed a magic opcode (ED FF or DD 01: a NOP on the hardware), address = the opcode word
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

/// NextREG breakpoint: ioType IN = a read of the register (port #253B), OUT = a write. The doors a write comes through
/// (zxnext.vhd nr_wr_en: cpu_req or copper_req; the CPU side is NEXTREG n,v / NEXTREG n,A or OUT #253B)
constexpr uint8_t BRK_NR_DOOR_NEXTREG = 0x01;  // the NEXTREG instructions (no bus cycle)
constexpr uint8_t BRK_NR_DOOR_PORT = 0x02;     // OUT (#253B) after OUT (#243B); also the read door of IN (#253B)
constexpr uint8_t BRK_NR_DOOR_COPPER = 0x04;   // a copper MOVE
constexpr uint8_t BRK_NR_DOOR_ALL = 0x07;
// The magic opcodes other emulators (jnext, ZEsarUX: ED FF; CSpect: DD 01) stop on. The Z80N executes both as on the hardware
// (ED FF is a NOP, DD 01 is LD BC,nn with the prefix ignored); the breakpoint only notices them after they ran
constexpr uint16_t BRK_MAGIC_ED_FF = 0xEDFF;
constexpr uint16_t BRK_MAGIC_DD_01 = 0xDD01;
constexpr uint16_t BRK_BEAM_MAX_LINE = 511;    // the copper's line counter is 9 bits (copper.vhd vcount_i)
constexpr uint16_t BRK_BEAM_MAX_HC = 511;      // and so is the pixel clock (hcount_i)
constexpr uint16_t BRK_COPPER_LIST_MAX = 1023; // 1024 instructions (copper_list_addr_o is 10 bits)
constexpr int BRK_NR_READ = 0;
constexpr int BRK_NR_WRITE = 1;

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

    // NextREG breakpoint: the register is z80address..EndAddress() (0..255), the access is ioType (IN read, OUT write),
    // the doors (BRK_NR_DOOR_*) a write may come through, and the value written / read must satisfy
    // (value & nrMask) == (nrValue & nrMask); a zero mask accepts every value
    uint8_t nrDoors = BRK_NR_DOOR_ALL;
    uint8_t nrValue = 0;
    uint8_t nrMask = 0;

    // Beam breakpoint: the line is z80address (0..BRK_BEAM_MAX_LINE), the pixel clock in the row is beamHc. A copper breakpoint takes the
    // list index z80address..EndAddress() (0..BRK_COPPER_LIST_MAX)
    uint16_t beamHc = 0;

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
/// A copy of one breakpoint as it was when asked for: the manager's own descriptors change and go away on the
/// emulation thread, so no surface ever holds them
typedef std::shared_ptr<const BreakpointDescriptor> BreakpointSnapshot;
typedef std::map<uint16_t, BreakpointSnapshot> BreakpointListByID;

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
    uint8_t hasNextRegRead = 0;   // 1 if any active NextREG read breakpoint exists
    uint8_t hasNextRegWrite = 0;  // 1 if any active NextREG write breakpoint exists
    uint8_t hasBeam = 0;          // 1 if any active beam breakpoint exists
    uint8_t hasCopperPc = 0;      // 1 if any active copper-instruction breakpoint exists
    uint8_t hasMagic = 0;         // 1 if any active magic-opcode breakpoint exists
    // 1 if the copper's work must be watched as it happens: a beam or copper-instruction breakpoint, or a NextREG write breakpoint that
    // takes the copper's door. Off: the copper is caught up with the beam once per line, as always (no cost)
    uint8_t copperWatch = 0;

    // "There may be a breakpoint here": one bit per CPU address per kind (8 KB each, L1-sized). A physical
    // breakpoint marks its offsets in all four slots (globalbits, design §6): a set bit can be a false
    // "maybe" that the resolve answers
    uint64_t memoryFilter[3][1024] = {};
    // One bit per port per direction, masks expanded
    uint64_t portFilter[2][1024] = {};
    // One bit per NextREG per direction (BRK_NR_READ, BRK_NR_WRITE)
    uint64_t nextRegFilter[2][4] = {};

    bool MemoryBit(int kind, uint16_t address) const { return (memoryFilter[kind][address >> 6] >> (address & 63)) & 1u; }
    bool PortBit(int direction, uint16_t port) const { return (portFilter[direction][port >> 6] >> (port & 63)) & 1u; }
    bool NextRegBit(int direction, uint8_t reg) const { return (nextRegFilter[direction][reg >> 6] >> (reg & 63)) & 1u; }
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
    uint8_t nrDoors = BRK_NR_DOOR_ALL; // NextREG only: the doors a write comes through
    uint8_t nrValue = 0;               // NextREG only: (value & nrMask) == (nrValue & nrMask)
    uint8_t nrMask = 0;                //   0: any value
    uint16_t beamHc = 0;               // BRK_BEAM only: the pixel clock in the row
    BreakpointHitModeEnum hitMode = BRK_HIT_ALWAYS;
    uint32_t hitTarget = 0;
    std::string note;
    std::string group;                 // empty: "default"
    std::string owner;                 // empty: interactive
};

/// endregion </Types>

/// Threads: the surfaces (DeZog, ZRCP, GDB, WebAPI, CLI, Lua, Python, the GUI) call it from their own
/// threads while the emulation thread runs the hot path (Handle*), which reads the painted tables and the
/// descriptors without locks. So every change holds the CPU (Emulator::CpuHold: the main loop parked at an
/// instruction boundary, no direct run elsewhere; free on the thread that drives the Z80) and then takes the
/// manager's lock; every query takes the lock and returns copies (BreakpointSnapshot), never the descriptors
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

    // Last triggered breakpoint ID (for automation API queries): written by the hot path, read by the surfaces
    std::atomic<uint16_t> _lastTriggeredBreakpointID{BRK_INVALID};
    // Changes of the set (NotifyBreakpointsChanged): monitoring caches its summary by it
    std::atomic<uint64_t> _revision{0};

    // The surfaces' threads against each other and against a change the emulation thread makes. The hot path
    // never takes it: a change holds the CPU first (ChangeScope)
    mutable std::recursive_mutex _mutex;
    class ChangeScope;

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

    /// A breakpoint a user or a client set (owner interactive, not hidden). The others belong to their
    /// owners - analyzers (the BASIC typer's ROM control points), DeZog, step-over traps - and a user's
    /// list or clear must not show or remove them: clearing the typer's points under it stalls typing
    static bool IsUserBreakpoint(const BreakpointDescriptor& descriptor);
    /// Removes the user breakpoints only (IsUserBreakpoint); the owned ones stay
    void ClearUserBreakpoints();

    /// Takes ownership of @p descriptor: it is stored, or freed when it is not (a plain breakpoint with the same
    /// address or port exists - its id is returned -, an invalid type)
    uint16_t AddBreakpoint(BreakpointDescriptor* descriptor);
    /// The same; @p adopted false when an existing breakpoint's id came back (the caller must not remove it as its own)
    uint16_t AddBreakpoint(BreakpointDescriptor* descriptor, bool& adopted);
    /// The one entry every surface uses (debugger protocol fields): validates the spec against this machine
    /// (range order, a physical range inside one page, the page exists, masks only on ports, a hit target
    /// for the counting modes) and adds it. BRK_INVALID with the reason in `error`
    uint16_t AddBreakpoint(const BreakpointSpec& spec, std::string& error);
    /// A copy of the breakpoint, null for an unknown id
    BreakpointSnapshot GetBreakpointById(uint16_t breakpointID) const;
    bool RemoveBreakpoint(BreakpointDescriptor* descriptor);
    bool RemoveBreakpointByID(uint16_t breakpointID);

    size_t GetBreakpointsCount() const;

    // Get/clear last triggered breakpoint (for automation APIs)
    uint16_t GetLastTriggeredBreakpointID() const
    {
        return _lastTriggeredBreakpointID.load(std::memory_order_relaxed);
    }
    /// A stop that was found earlier than it is served (a copper write caught up inside the picture) says which breakpoint it was
    void NoteTriggered(uint16_t breakpointID) { _lastTriggeredBreakpointID.store(breakpointID, std::memory_order_relaxed); }
    void ClearLastTriggeredBreakpoint()
    {
        _lastTriggeredBreakpointID.store(BRK_INVALID, std::memory_order_relaxed);
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
    /// NextREG doors as the text surfaces write them: "nextreg", "port", "copper", joined by ',' or '+'; "" or "all" = every door.
    /// False with the reason in `error` for anything else
    static bool ParseNextRegDoors(const std::string& text, uint8_t& doors, std::string& error);
    /// "nextreg,port,copper" for a door mask (the reverse of ParseNextRegDoors)
    static std::string NextRegDoorsName(uint8_t doors);
    /// "5", ">=5", "%5" or "" (always): the text form of a breakpoint's policy
    static std::string HitSpecName(const BreakpointDescriptor& breakpoint);
    /// "always", "equal", "at_least", "multiple": the protocol's hit_mode names (ParseHitModeName reverses it)
    static const char* HitModeName(BreakpointHitModeEnum mode);
    static bool ParseHitModeName(const std::string& text, BreakpointHitModeEnum& mode);
    /// The options the script surfaces (Lua, Python) take, applied to a spec: page "ram5" (empty: none), the range
    /// end `to` (-1: none), slot_only, port mask (-1: none), hits "5" / ">=5" / "%5" (empty: always)
    static bool ApplyScriptOptions(BreakpointSpec& spec, const std::string& page, int32_t to, bool slotOnly, int32_t mask,
                                   const std::string& hits, std::string& error);
    /// The options the script surfaces take for a NextREG breakpoint, applied to a spec: the range end `to` (-1: none), doors
    /// "nextreg,port,copper" (empty: all), `value` (-1: any), `valueMask` (-1: #FF with a value), hits as in ApplyScriptOptions
    static bool ApplyNextRegOptions(BreakpointSpec& spec, int32_t to, const std::string& doors, int32_t value, int32_t valueMask,
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
    /// is repainted and the surfaces told once, at EndBatch. Batches nest. Use Batch from another thread than the
    /// emulation one: it holds the CPU for the whole batch, so the hot path never runs on tables painted before a
    /// removal inside it
    void BeginBatch();
    void EndBatch();
    class Batch
    {
    public:
        explicit Batch(BreakpointManager& manager);
        ~Batch();
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;

    private:
        BreakpointManager& _manager;
        std::unique_ptr<ChangeScope> _scope;
    };
    /// A memory breakpoint (memoryType: BRK_MEM_* bits) at the address, bound to `pageSpec` when it is not
    /// empty. BRK_INVALID with the reason in `error` for a bad or missing page
    uint16_t AddMemoryBreakpointInPageSpec(uint16_t z80address, uint8_t memoryType, const std::string& pageSpec,
                                           std::string& error);

    // Breakpoint listing: copies, by id
    BreakpointListByID GetAllBreakpoints() const;
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

    /// The user breakpoints (IsUserBreakpoint) counted, for monitoring (GET /monitor/debug.breakpoints, the groups
    /// route): one pass under the lock, no copies of the descriptors
    struct BreakpointSummary
    {
        struct Group
        {
            std::string name;
            uint32_t total = 0;
            uint32_t active = 0;
            std::vector<uint16_t> ids;
        };
        uint32_t total = 0;
        uint32_t active = 0;
        uint32_t owned = 0;  ///< breakpoints of an owner (analyzers, DeZog) and hidden ones (step traps): not counted above
        /// Per kind, a breakpoint with several accesses counted in each: memory (exec, read, write), port (in, out),
        /// keyboard, nextreg, beam, copper, magic
        uint32_t memory = 0, exec = 0, read = 0, write = 0;
        uint32_t port = 0, portIn = 0, portOut = 0;
        uint32_t keyboard = 0, nextreg = 0, beam = 0, copper = 0, magic = 0;
        std::vector<Group> groups;  ///< by name
    };
    BreakpointSummary GetSummary() const;
    /// Bumped on every change of the set (add, remove, enable, group, note; hidden ones too): a reader caches by it
    uint64_t Revision() const { return _revision.load(std::memory_order_relaxed); }
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
    /// A NextREG access (the board's single read / write choke points): the breakpoint that stops, or BRK_INVALID.
    /// `door` is one BRK_NR_DOOR_* bit; `value` is what is written or what the read returns
    /// The copper completed the instruction at list index `pc`: the copper-instruction breakpoint that stops, or BRK_INVALID
    /// The CPU completed an instruction whose opcode word is `opcodeWord`: the magic breakpoint that stops, or BRK_INVALID
    uint16_t HandleMagic(uint16_t opcodeWord)
    {
        return _hotState.hasMagic ? ResolveMagic(opcodeWord) : BRK_INVALID;
    }
    uint16_t HandleCopperPc(uint16_t pc)
    {
        return _hotState.hasCopperPc ? ResolveCopperPc(pc) : BRK_INVALID;
    }
    /// The beam moved: `crossed(line, hc)` says whether the place was passed since the last look. The beam breakpoint that stops
    uint16_t HandleBeam(const std::function<bool(uint16_t line, uint16_t hc)>& crossed)
    {
        return _hotState.hasBeam ? ResolveBeam(crossed) : BRK_INVALID;
    }
    uint16_t HandleNextReg(int direction, uint8_t reg, uint8_t value, uint8_t door)
    {
        if (!(direction == BRK_NR_READ ? _hotState.hasNextRegRead : _hotState.hasNextRegWrite) ||
            !_hotState.NextRegBit(direction, reg))
            return BRK_INVALID;
        return ResolveNextReg(direction, reg, value, door);
    }
    // endregion </Runtime methods>

    // region <Helper methods>
protected:
    uint16_t GenerateNewBreakpointID();
    /// Removes and frees every breakpoint (the caller holds what a change holds)
    void DeleteAllBreakpoints();

    /// AddBreakpoint without freeing: @p adopted tells whether @p descriptor was stored
    uint16_t AddBreakpointAdopting(BreakpointDescriptor* descriptor, bool& adopted);
    uint16_t AddMemoryBreakpoint(BreakpointDescriptor* descriptor);
    uint16_t AddPortBreakpoint(BreakpointDescriptor* descriptor);
    uint16_t AddNextRegBreakpoint(BreakpointDescriptor* descriptor);

    BreakpointDescriptor* FindAddressBreakpoint(uint16_t address);
    BreakpointDescriptor* FindAddressBreakpoint(uint16_t address, const MemoryPageDescriptor& pageInfo);
    BreakpointDescriptor* FindPortBreakpoint(uint16_t port);

    /// A filter "maybe": the candidates of the CPU address and of the page the slot shows; the slot filter,
    /// the hit policy (counting), the TTD-replay suppression. The id of the first candidate that stops
    uint16_t ResolveSpace(uint16_t spacePage, uint16_t offset, bool write);
    uint16_t ResolveMemory(int kind, uint16_t address);
    uint16_t ResolvePort(int direction, uint16_t port);
    uint16_t ResolveNextReg(int direction, uint8_t reg, uint8_t value, uint8_t door);
    uint16_t ResolveCopperPc(uint16_t pc);
    uint16_t ResolveMagic(uint16_t opcodeWord);
    uint16_t ResolveBeam(const std::function<bool(uint16_t line, uint16_t hc)>& crossed);
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
