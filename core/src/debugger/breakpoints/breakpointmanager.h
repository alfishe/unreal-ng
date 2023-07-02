#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include <map>

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

    uint16_t z80address = 0xFFFF;
    uint8_t bank = 0xFF;
    uint16_t bankAddress = 0xFFFF;

    bool active = true;

    std::string note;                           // Annotation for the breakpoint
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

typedef std::map<uint32_t, BreakpointDescriptor*> BreakpointMapByAddress;
typedef std::map<uint16_t, BreakpointDescriptor*> BreakpointMapByPort;
typedef std::map<uint16_t, BreakpointDescriptor*> BreakpointMapByID;

typedef std::map<uint8_t, BreakpointMapByAddress> BreakpointMapByBank;

/// endregion </Types>

class BreakpointManager
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
    const uint16_t _SUBMODULE = PlatformDebuggerSubmodulesEnum::SUBMODULE_DEBUG_BREAKPOINTS;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    EmulatorContext* _context;
    BreakpointMapByAddress _breakpointMapByAddress;
    BreakpointMapByPort _breakpointMapByPort;
    BreakpointMapByID _breakpointMapByID;

    // Incremental counter to generate new breakpoint IDs
    // Note: no breakpoint IDs reuse allowed
    uint16_t _breakpointIDSeq = 0;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    BreakpointManager() = delete;       // Disable default constructors. C++ 11 feature
    BreakpointManager(EmulatorContext* context);
    virtual ~BreakpointManager();
    /// endregion </Constructors / destructors>

    /// region <Management methods>
public:
    void ClearBreakpoints();

    uint16_t AddBreakpoint(BreakpointDescriptor* descriptor);
    bool RemoveBreakpoint(BreakpointDescriptor* descriptor);
    bool RemoveBreakpointByID(uint16_t breakpointID);

    size_t GetBreakpointsCount();
    /// endregion </Management methods>

    /// region <Management assistance methods>

    // Shortcuts for breakpoint creation
    uint16_t AddExecutionBreakpoint(uint16_t z80address);
    uint16_t AddMemReadBreakpoint(uint16_t z80address);
    uint16_t AddMemWriteBreakpoint(uint16_t z80address);
    uint16_t AddPortInBreakpoint(uint16_t port);
    uint16_t AddPortOutBreakpoint(uint16_t port);

    /// endregion </Management assistance methods>

    /// region <Runtime methods>
public:
    uint16_t HandlePCChange(uint16_t pc);
    uint16_t HandleMemoryRead(uint16_t readAddress);
    uint16_t HandleMemoryWrite(uint16_t writeAddress);
    uint16_t HandlePortIn(uint16_t portAddress);
    uint16_t HandlePortOut(uint16_t portAddress);
    /// endregion </Runtime methods>

    /// region <Helper methods>
protected:
    uint16_t GenerateNewBreakpointID();

    uint16_t AddMemoryBreakpoint(BreakpointDescriptor* descriptor);
    uint16_t AddPortBreakpoint(BreakpointDescriptor* descriptor);

    BreakpointDescriptor* FindAddressBreakpoint(uint16_t address);
    BreakpointDescriptor* FindPortBreakpoint(uint16_t port);

    /// endregion </Helper methods>
};
