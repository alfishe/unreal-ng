#pragma once

/// @file gsbusobserver.h
/// @brief Observer of the General Sound CPU's own bus cycles that a board's logic sees: GS port accesses and DAC
/// fetches (reads at #6000-#7FFF). Used by the ZX-MultiSound's bus trace (multisoundbustrace.h); no emulator
/// dependencies. Off by default: one pointer test per GS port access and DAC fetch.

#include <cstdint>

class IGSBusObserver
{
public:
    virtual ~IGSBusObserver() = default;

    /// A GS CPU IN (write = false, `value` = the byte the CPU got) or OUT on its port space; `time` is the host time of
    /// the instruction (the same axis and frame base as IGSDacSink)
    virtual void GsPortCycle(uint64_t time, uint8_t port, uint8_t value, bool write) = 0;
    /// A GS CPU memory read at #6000-#7FFF (a DAC sample latch), `value` = the byte read
    virtual void GsDacFetch(uint64_t time, uint16_t address, uint8_t value) = 0;
};
