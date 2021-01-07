#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"

/// region <Types>

constexpr uint16_t BRK_INVALID = 0xFFFF;

///
/// Type of breakpoint. Types can be combined as a bitmask
///
enum BreakpointTypeEnum : uint8_t
{
    BRK_NONE = 0x00,
    BRK_EXECUTE = 0x01,
    BRK_READ = 0x02,
    BRK_WRITE = 0x04,
    BRK_IO_IN = 0x08,
    BRK_IO_OUT = 0x10
};

///
/// Descriptor for a single breakpoint
///
struct BreakpointDescriptor
{
    uint16_t breakpointID;
    BreakpointTypeEnum type = BRK_NONE;

    uint8_t bank;
    uint16_t bankAddress;
    uint16_t z80address = 0x0000;

    bool active;
};

struct BreakpointRangeDescription
{
    uint16_t z80AddressFrom = 0x0000;
    uint16_t z80AddressTo = 0x0000;

    bool active;
};

typedef std::map<uint16_t, BreakpointDescriptor> BreakpointMap;

/// endregion </Types>

class BreakpointManager
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    BreakpointMap _breakpointMap;
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

    uint16_t AddBreakpoint(BreakpointDescriptor descriptor);
    bool RemoveBreakpoint(BreakpointDescriptor descriptor);
    bool RemoveBreakpoint(uint16_t breakpointID);
    /// endregion </Management methods>

    /// region <Runtime methods>
public:
    bool HandlePCChange(uint16_t pc);
    bool HandleMemoryRead(uint16_t readAddress);
    bool HandleMemoryWrite(uint16_t writeAddress);
    bool HandlePortIn(uint16_t portAddress);
    bool HandlePortOut(uint16_t portAddress);
    /// endregion </Runtime methods>
};
