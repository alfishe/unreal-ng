#pragma once

/// @file debugtarget.h
/// @brief A CPU other than the machine's own that the debugger can stop on (gs-debugger design §3.1): what a
/// BreakpointManager built for it asks instead of the main machine's CPU, memory and feature switches.

#include <cstdint>

#include "debugger/targets/debugcpuid.h"
#include "emulator/memory/memory.h"

struct BreakpointTapDemand;
struct Z80Registers;

/// Where the target's CPU is in time at a breakpoint hit (the per-hit record of a breakpoint that only counts)
struct DebugTargetHitClock
{
    uint64_t frame = 0;    ///< the ZX frame the CPU runs in
    uint32_t t = 0;        ///< the CPU's time since that frame's start, in its own unit
    int64_t cpuTime = -1;  ///< the CPU's own clock, in its own unit
    uint16_t pc = 0;       ///< the instruction doing the access (an event: the instruction the CPU stands at)
};

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

    /// Inside a breakpoint hit of this CPU (its hooks, the emulation thread): its moment and the instruction. `event`:
    /// the hit is an event's, not an access of an instruction. False when the target cannot tell
    virtual bool HitClock(bool event, DebugTargetHitClock& out) const
    {
        (void)event;
        (void)out;
        return false;
    }
};
