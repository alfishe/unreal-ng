#pragma once

#include <cstdint>

#include "emulator/memory/memorywaitoverlay.h"
#include "emulator/video/sprinter/sprinterintsource.h"

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
    /// The 6-clock slot
    uint32_t PhasePeriod() const override { return 6; }
};

/// The Sprinter's "original waits" (PLD WAIT_ORIG, SP2_ACEX.TDF:558-559; research-zx-mode.md §7.3,
/// tdd-zx-mode.md §3.3): a ZX mode that slows the CPU on the Spectrum screen memory a little, as the
/// launcher's ORIGIN.ZX asks for (ALL_MODE = #FA).
///
///   WAIT_ORIG = /MR or CT5 or ALL_MODE2 or ((!(V_RAM & A14 & A15) & !(A14 & !A15)) or TURBO)
///
/// In words: with ALL_MODE bit 2 = 0 and the CPU at 3.5 MHz, a memory access (/MR: never a port cycle) to
/// #4000-#7FFF, or to #C000-#FFFF while #7FFD bit 2 is set (V_RAM = PN2, DCP.TDF:577: Spectrum pages 4-7),
/// is held while CT5 = 0 - in every line, the border included. CT[5..0] is the video counter's low part
/// (VIDEO2.TDF:284-298): CT[2..0] counts 0, 1, 2, 4, 5, 6 (six 42 MHz clocks), CT[5..3] steps once per six
/// clocks, so CT5 is low for 24 clocks and high for 24 - a 48-clock period = 4 T at 3.5 MHz, 56 periods per
/// 224-T line (one per 16-pixel square). The CPU samples /WAIT in T2 and once per wait state after it.
///
/// The phase follows from the PLD (tdd-zx-mode §3.3, Q1 closed 2026-10-03):
/// - the 3.5 MHz clock is a toggle of CT[2..0] = 2 (DCP.TDF:275), registered on the falling 42 MHz edge; CT and
///   the toggle both power up at 0 and nothing resets them, so every CPU T-state starts 3.5 clocks after a
///   CT[2..0] = 0 state and CT5's edges (CT[5..3] steps on CT[2..0] 6 -> 0) sit 3.5 clocks before a T boundary;
/// - INTT is clocked by CT5 (VIDEO2.TDF:394) and INT_X is set by its rising edge (SP2_ACEX.TDF:744), so the frame
///   INT starts with a CT5-high half: the T-states from INT are high, high, low, low (repeating);
/// - an access whose T1 is T 1 after INT (mod 4) has T2 on the first low T: /WAIT seen twice, 2 T; T1 at 2: 1 T;
///   T1 at 3 or 0: T2 on the high half, none. Waits by T1 from INT mod 4: 0, 2, 1, 0 (0.75 T on average).
/// A Verilator run of the transcribed counters, clock, INT and WAIT_ORIG gives the same table over a whole
/// frame (the Sprinter verification package, orig_phase.csv); a board measurement would still confirm it.
///
/// SprinterIntSource places every INT on the CT5 rise, frame T = kCt5RiseT (mod 4), so the rule is relative to
/// that one constant. M1 opcode fetches, operand / data reads and writes all sample /WAIT in T2: one table.
/// Worked example: LD A,(#4000) whose data read starts at frame T 1 003: 1 003 - 2 = 1 001, mod 4 = 1: 2 T of
/// wait; at T 1 004: 2 (mod 4), 1 T; at T 1 005 / 1 006: none.
///
/// Installed only while the waits apply (PortDecoder_Sprinter::ApplyWaits): every other machine and
/// the Sprinter outside this mode pay nothing.
class SprinterOrigWaits : public MemoryWaitOverlay
{
public:
    /// T-states per CT5 period (48 clocks of 42 MHz)
    static constexpr uint32_t kPeriod = 4;
    /// Frame T (mod 4) of the CT5 rise = every INT edge (SprinterIntSource::kCt5RiseT)
    static constexpr uint32_t kCt5RiseT = SprinterIntSource::kCt5RiseT;
    /// Wait states by (T1 - kCt5RiseT) mod 4: T1 at INT + 1 has T2 on the first CT5-low T
    static constexpr uint8_t kWaitsFromRise[kPeriod] = {0, 2, 1, 0};

    explicit SprinterOrigWaits(Z80* cpu) : MemoryWaitOverlay(cpu) {}

    /// Wait states for a memory cycle that started (T1) at base T-state `startClock` of the frame
    static uint32_t Rule(uint32_t startClock)
    {
        return kWaitsFromRise[(startClock + kPeriod - kCt5RiseT) % kPeriod];
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
    /// The CT5 period
    uint32_t PhasePeriod() const override { return kPeriod; }
};
