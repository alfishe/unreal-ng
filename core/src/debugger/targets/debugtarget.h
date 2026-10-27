#pragma once

/// @file debugtarget.h
/// @brief A CPU other than the machine's own that the debugger can stop on (gs-debugger design §3.1): what a
/// BreakpointManager built for it asks instead of the main machine's CPU, memory and feature switches.

#include <cstdint>

#include "debugger/targets/debugcpuid.h"
#include "emulator/memory/memory.h"

struct BreakpointTapDemand;

class IDebugTarget
{
public:
    virtual ~IDebugTarget() = default;

    virtual DebugCpuId Id() const = 0;
    /// The surfaces' name ("gs")
    virtual const char* Name() const = 0;
    /// The CPU exists now (gs: a card with a CPU is fitted)
    virtual bool Available() const = 0;

    /// The page behind a CPU address now, for page-tied breakpoints (BANK_INVALID: no page there, or no CPU)
    virtual MemoryPageDescriptor MapAddress(uint16_t address) const = 0;
    /// 16K pages of a kind the CPU can map; a page-tied breakpoint must name one of them
    virtual uint32_t PageCount(MemoryBankModeEnum kind) const = 0;

    /// What the target's breakpoints need now (BreakpointManager repaint): the target turns its hooks on or off
    virtual void OnBreakpointDemand(const BreakpointTapDemand& demand) = 0;
};
