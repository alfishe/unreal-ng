#pragma once

#include <QString>

#include <cstdint>
#include <string>

#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/notifications.h"

/// A breakpoint hit as NC_EXECUTION_BREAKPOINT carries it (copied off the message centre's thread)
struct BreakpointHit
{
    std::string emulatorId;
    uint16_t id = BRK_INVALID;
    uint16_t address = 0;
    BreakpointHitKind kind = BreakpointHitKind::Execute;
    std::string cpu = "main";
    uint16_t cpuPc = 0;
};

/// The stop line for a hit of `breakpoint`: which breakpoint, on what access, the condition that held, the hit count
/// (rich text with the links "edit" and "list")
QString BreakpointStopLine(const BreakpointDescriptor& breakpoint, const BreakpointHit& hit);
