#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include <map>
#include <unordered_map>

/// region <Types>

constexpr uint16_t BRK_INVALID = 0xFFFF;

///
/// Type of breakpoint. Types can be combined as a bitmask
///

enum BreakpointTypeEnum : uint8_t
{
    BRK_MEMORY = 0,     // Memory access (Read | Write | Execution)
    BRK_IO,             // I/O port access (Read | Write)
    BRK_KEYBOARD,       // Key press event
    //BRK_INTERRUPT     // Interrupt event
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
    BRK_MATCH_ADDR = 0,     // Match Z80 space address (no distinction between banks)
    BRK_MATCH_BANK_ADDR     // Match exact address in specific bank
};

///
/// Descriptor for a single address / port breakpoint
///
struct BreakpointDescriptor
{
    uint16_t breakpointID = BRK_INVALID;        // Unique breakpoint ID (sequence is shared across all memory and IO breakpoints)
    uint32_t keyAddress = 0xFFFF'FFFF;          // Composite bank + address key for fast lookup

    BreakpointTypeEnum type = BRK_MEMORY;
    BreakpointAddressMatchEnum matchType = BRK_MATCH_ADDR;

    uint8_t memoryType = BRK_MEM_READ | BRK_MEM_WRITE | BRK_MEM_EXECUTE;
    uint8_t ioType = BRK_IO_IN | BRK_IO_OUT;
    uint8_t keyType = BRK_KEY_PRESS | BRK_KEY_RELEASE;

    // Used if breakpoint is set to any matching address in Z80 address space (independently of the bank mapping)
    uint16_t z80address = 0xFFFF;

    // Used if breakpoint is set to any matching address in specific memory page (independently of Z80 address)
    uint8_t page = 0xFF;                        // Page number (ROM 0-63 or RAM 0-255)
    MemoryBankModeEnum pageType = BANK_RAM;     // Memory type: BANK_ROM, BANK_RAM, or BANK_CACHE
    uint16_t bankOffset = 0xFFFF;               // Offset within the page (0-0x3FFF)

    bool active = true;

    std::string owner = "interactive";          // Owner: "interactive" for user/CLI, or "analyzer_manager" if set by analyzer manager
    std::string note;                           // Annotation for the breakpoint
    std::string group = "default";              // Group name for organizing breakpoints
};

///
/// Descriptor for range of memory addresses / ports breakpoints
///
struct BreakpointRangeDescription
{
    uint16_t breakpointID;

    BreakpointTypeEnum type = BRK_MEMORY;
    BreakpointAddressMatchEnum matchType = BRK_MATCH_ADDR;

    uint8_t memoryType = BRK_MEM_READ | BRK_MEM_WRITE | BRK_MEM_EXECUTE;
    uint8_t ioType = BRK_IO_IN | BRK_IO_OUT;

    uint16_t z80AddressFrom = 0x0000;
    uint16_t z80AddressTo = 0x0000;

    uint8_t bankFrom = 0xFF;
    uint16_t bankAddressFrom = 0xFFFF;
    uint8_t bankTo = 0xFF;
    uint16_t bankAddressTo = 0xFFFF;

    bool active;

    std::string note;                           // Annotation for the breakpoint
};

typedef std::unordered_map<uint32_t, BreakpointDescriptor*> BreakpointMapByAddress;
typedef std::unordered_map<uint16_t, BreakpointDescriptor*> BreakpointMapByPort;
typedef std::map<uint16_t, BreakpointDescriptor*> BreakpointMapByID;
typedef std::map<uint8_t, BreakpointMapByAddress> BreakpointMapByBank;

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
    /// endregion </Fields>

    // region <Constructors / destructors>
public:
    BreakpointManager() = delete;       // Disable default constructors. C++ 11 feature
    BreakpointManager(EmulatorContext* context);
    virtual ~BreakpointManager();
    /// endregion </Constructors / destructors>

    /// region <Management methods>
public:
    void ClearBreakpoints();

    uint16_t AddBreakpoint(BreakpointDescriptor* descriptor);
    BreakpointDescriptor* GetBreakpointById(uint16_t breakpointID);
    bool RemoveBreakpoint(BreakpointDescriptor* descriptor);
    bool RemoveBreakpointByID(uint16_t breakpointID);

    size_t GetBreakpointsCount();
    
    // Get/clear last triggered breakpoint (for automation APIs)
    uint16_t GetLastTriggeredBreakpointID() const { return _lastTriggeredBreakpointID; }
    void ClearLastTriggeredBreakpoint() { _lastTriggeredBreakpointID = BRK_INVALID; }
    
    /// Structured status info for automation APIs
    struct BreakpointStatusInfo
    {
        bool valid = false;                  // True if a breakpoint was found
        uint16_t id = BRK_INVALID;           // Breakpoint ID
        std::string type;                    // "memory", "port", "keyboard"
        uint16_t address = 0;                // Z80 address or port number
        std::string access;                  // "execute", "read", "write", "in", "out" (comma-separated)
        bool active = false;                 // Current enable state
        std::string note;                    // User annotation
        std::string group;                   // Group name
    };
    
    /// Get structured info about the last triggered breakpoint
    BreakpointStatusInfo GetLastTriggeredBreakpointInfo() const;
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
    uint16_t AddExecutionBreakpointInPage(uint16_t z80address, uint8_t page, MemoryBankModeEnum pageType, const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddMemReadBreakpointInPage(uint16_t z80address, uint8_t page, MemoryBankModeEnum pageType, const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddMemWriteBreakpointInPage(uint16_t z80address, uint8_t page, MemoryBankModeEnum pageType, const std::string& owner = OWNER_INTERACTIVE);

    // Combined breakpoint types
    uint16_t AddCombinedMemoryBreakpoint(uint16_t z80address, uint8_t memoryType, const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddCombinedPortBreakpoint(uint16_t port, uint8_t ioType, const std::string& owner = OWNER_INTERACTIVE);
    uint16_t AddCombinedMemoryBreakpointInPage(uint16_t z80address, uint8_t memoryType, uint8_t page, MemoryBankModeEnum pageType, const std::string& owner = OWNER_INTERACTIVE);
    
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
    std::vector<std::string> GetBreakpointGroups() const;
    std::vector<uint16_t> GetBreakpointsByGroup(const std::string& groupName) const;
    std::string GetBreakpointListAsStringByGroup(const std::string& groupName) const;
    void ActivateBreakpointGroup(const std::string& groupName);
    void DeactivateBreakpointGroup(const std::string& groupName);
    bool RemoveBreakpointFromGroup(uint16_t breakpointID);
    void RemoveBreakpointGroup(const std::string& groupName);

    // endregion </Management assistance methods>

    // region <Runtime methods>
public:
    uint16_t HandlePCChange(uint16_t pc);
    uint16_t HandleMemoryRead(uint16_t readAddress);
    uint16_t HandleMemoryWrite(uint16_t writeAddress);
    uint16_t HandlePortIn(uint16_t portAddress);
    uint16_t HandlePortOut(uint16_t portAddress);
    // endregion </Runtime methods>

    // region <Helper methods>
protected:
    uint16_t GenerateNewBreakpointID();

    uint16_t AddMemoryBreakpoint(BreakpointDescriptor* descriptor);
    uint16_t AddPortBreakpoint(BreakpointDescriptor* descriptor);

    BreakpointDescriptor* FindAddressBreakpoint(uint16_t address);
    BreakpointDescriptor* FindAddressBreakpoint(uint16_t address, const MemoryPageDescriptor& pageInfo);
    BreakpointDescriptor* FindPortBreakpoint(uint16_t port);

    // endregion </Helper methods>
};


#ifdef _CODE_UNDER_TEST

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
class BreakpointManagerCUT : public BreakpointManager
{
public:
    BreakpointManagerCUT(EmulatorContext* context) : BreakpointManager(context) {}

    using BreakpointManager::_context;
    using BreakpointManager::_logger;
    using BreakpointManager::_breakpointMapByAddress;
    using BreakpointManager::_breakpointMapByPort;
    using BreakpointManager::_breakpointMapByID;
    using BreakpointManager::_breakpointIDSeq;

    using BreakpointManager::GenerateNewBreakpointID;
    using BreakpointManager::AddMemoryBreakpoint;
    using BreakpointManager::AddPortBreakpoint;
    using BreakpointManager::FindAddressBreakpoint;
    using BreakpointManager::FindPortBreakpoint;
};

#endif // _CODE_UNDER_TEST

