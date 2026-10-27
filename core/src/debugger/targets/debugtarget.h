#pragma once

/// @file debugtarget.h
/// @brief A CPU other than the machine's own that the debugger can stop on (gs-debugger design §3.1): what a
/// BreakpointManager built for it asks instead of the main machine's CPU, memory and feature switches.

#include <cstdint>

#include "debugger/targets/debugcpuid.h"
#include "emulator/memory/memory.h"

struct BreakpointTapDemand;
struct Z80Registers;

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

    /// The CPU's registers now, in the main Z80's layout (breakpoint conditions; T stays 0). False without a CPU
    virtual bool Registers(Z80Registers& out) const = 0;
    /// A byte of the CPU's memory as it sees it now, no side effects. False without a CPU
    virtual bool Peek(uint16_t address, uint8_t& out) const = 0;

    /// What the target's breakpoints need now (BreakpointManager repaint): the target turns its hooks on or off
    virtual void OnBreakpointDemand(const BreakpointTapDemand& demand) = 0;
};
