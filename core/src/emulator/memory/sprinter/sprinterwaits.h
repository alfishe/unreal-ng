#pragma once

#include <cstdint>

#include "emulator/memory/memorywaitoverlay.h"

/// The Sprinter's turbo wait rule (Sprinter technical-design §4, decision D4).
///
/// At 21 MHz the main RAM cannot keep up: every access is stretched to the next
/// 6-clock slot. MAME's model (sprinter.cpp do_mem_wait, :1720-1731): an access
/// that started at CPU clock t costs ((6 - t mod 6) mod 6) + 6 - taken extra
/// clocks, where `taken` is the part of the slot the CPU cycle covers itself:
/// 3 for memory, 4 for a port access. Fast RAM and ROM have no waits; at 3.5 MHz
/// nothing is added (the overlay is not installed).
///
/// Worked example: a RAM read at t = 100: 100 mod 6 = 4, (6 - 4) mod 6 = 2,
/// plus 6 - 3 gives 5 extra clocks; at t = 102 (mod 6 = 0) it costs 3.
///
/// The rule lives only here, so a measured model can replace it.
class SprinterWaits : public MemoryWaitOverlay
{
public:
    static constexpr uint32_t kMemoryTaken = 3;
    static constexpr uint32_t kPortTaken = 4;

    explicit SprinterWaits(Z80* cpu) : MemoryWaitOverlay(cpu) {}

    /// Extra CPU clocks for an access of a component that takes `taken` clocks, started at `startClock`
    static uint32_t Rule(uint32_t startClock, uint32_t taken)
    {
        const uint32_t phase = startClock % 6;
        return (phase ? 6 - phase : 0) + 6 - taken;
    }

    /// The start of a port cycle, in CPU clocks, from Z80::AccessStartClock() taken in the
    /// port decoder. AccessStartClock() is "now - 3" (a memory cycle's 3 T are charged
    /// before the bus sees it), but every IN / OUT charges only the I/O cycle's first T
    /// (T1, before IORQ) before it calls the decoder and the other 3 after (op_D3, op_DB,
    /// the ED group), so the port cycle started at "now - 1" = AccessStartClock() + 2.
    /// MAME calls dcp_r / dcp_w at the start of the IORQ cycle (z80.lst `in` / `out`:
    /// the handler runs before the cycle's clocks are counted), so its do_mem_wait(4)
    /// sees that clock (technical-design §4)
    static uint32_t IoCycleStart(uint32_t accessStartClock) { return accessStartClock + 2u; }

    uint32_t ExtraClocks([[maybe_unused]] MemoryWaitAccess kind, [[maybe_unused]] uint16_t addr, uint32_t startClock) override
    {
        return Rule(startClock, kMemoryTaken);
    }
};
