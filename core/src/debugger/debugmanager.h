#pragma once

#include "stdafx.h"

#include <map>

/// region <Structures>

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
public:
    BreakpointTypeEnum type = BRK_NONE;
    uint16_t address = 0x0000;
    bool isActive = false;
};

/// endregion </Structures>

class DebugManager
{
    /// region <Fields>
protected:
    std::map<uint16_t, BreakpointDescriptor> _breakpoints;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    DebugManager();
    virtual ~DebugManager();

    /// endregion </Constructors / Destructors>

    /// region <Breakpoint management>
public:
    void AddBreakpoint(BreakpointTypeEnum type, uint16_t address);
    void RemoveBreakpoint(BreakpointTypeEnum type, uint16_t address);
    void RemoveAllBreakpoints();

    void DisableBreakpoint(BreakpointTypeEnum type, uint16_t address);
    void EnableBreakpoint(BreakpointTypeEnum type, uint16_t address);

    /// endregion </Breakpoint management>
};
