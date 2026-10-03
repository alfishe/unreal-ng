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

/// The Sprinter's "original waits" (PLD WAIT_ORIG, SP2_ACEX.TDF:558-559; research-zx-mode.md §7.3,
/// tdd-zx-mode.md §3.3): a ZX mode that slows the CPU on the Spectrum screen memory a little, as the
/// launcher's ORIGIN.ZX asks for (ALL_MODE = #FA).
///
///   WAIT_ORIG = /MR or CT5 or ALL_MODE2 or ((!(V_RAM & A14 & A15) & !(A14 & !A15)) or TURBO)
///
/// In words: with ALL_MODE bit 2 = 0 and the CPU at 3.5 MHz, a memory access to #4000-#7FFF, or to
/// #C000-#FFFF while #7FFD bit 2 is set (V_RAM = PN2, DCP.TDF:577: Spectrum pages 4-7), is held while
/// CT5 = 0. CT[5..0] is the video counter's low part (VIDEO2.TDF:280-298): CT[2..0] counts 0, 1, 2, 4,
/// 5, 6 (six 42 MHz clocks), CT[5..3] steps once per six clocks, so CT5 is low for 24 clocks and high
/// for 24 - a 48-clock period = 4 T at 3.5 MHz, 56 periods per 224-T line (one per 16-pixel square).
/// The CPU samples /WAIT in T2 and once per wait state after it, so an access whose T2 falls on the
/// first low T waits 2 T, on the second low T 1 T, on a high T none (0.75 T on average).
///
/// Where the CT period starts relative to the frame is not documented (tdd-zx-mode Q1): kPhase is a
/// placeholder until the measurement program (testdata/machines/sprinter/zx-timing/) reports it from a
/// real board. Worked example (kPhase = 0): LD A,(#4000) whose read starts at frame T 1 000:
/// T2 = 1 001, (1 001 + 0) mod 4 = 1: the second low T, 1 T of wait; at T 1 003: T2 mod 4 = 0, 2 T.
///
/// Installed only while the waits apply (PortDecoder_Sprinter::ApplyWaits): every other machine and
/// the Sprinter outside this mode pay nothing.
class SprinterOrigWaits : public MemoryWaitOverlay
{
public:
    /// T-states per CT5 period (48 clocks of 42 MHz)
    static constexpr uint32_t kPeriod = 4;
    /// Frame T at which a CT5-low half starts, mod 4: a placeholder (tdd-zx-mode Q1), the one place to change
    static constexpr uint32_t kPhase = 0;

    explicit SprinterOrigWaits(Z80* cpu) : MemoryWaitOverlay(cpu) {}

    /// Wait states for a memory cycle that started (T1) at base T-state `startClock` of the frame
    static uint32_t Rule(uint32_t startClock)
    {
        const uint32_t sample = (startClock + 1u + kPhase) % kPeriod;  // T2, where /WAIT is first sampled
        return sample == 0 ? 2u : (sample == 1 ? 1u : 0u);
    }

    /// The windows the PLD equation covers: window 1 always, window 3 while #7FFD bit 2 is set
    static bool WindowWaits(uint8_t window, uint8_t port7ffd)
    {
        return window == 1 || (window == 3 && (port7ffd & 0x04) != 0);
    }

    uint32_t ExtraClocks([[maybe_unused]] MemoryWaitAccess kind, [[maybe_unused]] uint16_t addr, uint32_t startClock) override
    {
        return Rule(startClock);
    }
};
