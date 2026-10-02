#pragma once

/// @file atm710turbooverlay.h
/// @brief ATM Turbo 2+ v7.10 wait states at 7 MHz
/// (docs/inprogress/2026-10-02-atm710-turbo-waits/reference-atm710-turbo-waits.md).
///
/// In turbo the CPU runs at 7 MHz but shares the RAM with the video fetcher:
/// the arbiter D68 gives it every other RAM slot (one slot = 4 master clocks
/// = 2 T at 7 MHz), and D69.1 holds /WAIT from the RAM select until RAS falls
/// in the CPU's slot. A RAM cycle is stretched so that its T3 starts at the
/// end of the first CPU slot after T2: with `t` the 7 MHz clock of T1 in the
/// frame (slots start on even clocks),
///
/// | access | wait (7 MHz T) |
/// |:--|:--|
/// | opcode fetch, memory read or write, RAM | 2 + (t mod 2) |
/// | ROM, I/O, interrupt acknowledge, refresh, internal cycles | 0 |
///
/// The same in every video mode, in the border and the blanking (D68 has no
/// mode input; the video counters refresh the DRAM all the time). At 3.5 MHz
/// the CPU always owns the T2 slot: no waits.
///
/// Worked examples (code in RAM, 7 MHz): NOP 6 T from an even clock, 7 from an
/// odd one; LD A,(HL) 11 / 12; four NOPs 24; a NOP in ROM 4.
///
/// Installed by the ATM710 port decoder while the turbo bit (#FF77 bit 3) is
/// set (PortDecoder_ATM710::SyncTurboRamWaits); the waits follow the applied
/// clock and need the `contention` feature on. No state of its own: TTD and
/// snapshots need nothing.

#include <cstdint>

#include "emulator/memory/hostbusoverlay.h"
#include "emulator/platform.h"

class Core;
class Memory;
class Z80;

class Atm710TurboOverlay final : public HostBusOverlay
{
public:
    Atm710TurboOverlay(Core* core, Z80* cpu, Memory* memory, const EmulatorState* state);

    /// The CPU runs at 7 MHz and the `contention` feature is on: the waits apply
    bool WaitsApply() const;

    /// Wait clocks for a RAM access whose T1 is 7 MHz clock `start` of the frame
    static uint32_t RamWait(uint32_t start) { return 2u + (start & 1u); }

    uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) override;
    uint8_t onReadM1(uint16_t addr, uint8_t normal, bool romPaged) override;
    void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;

private:
    void Wait(uint16_t addr);

    Core* _core = nullptr;
    Z80* _cpu = nullptr;
    Memory* _memory = nullptr;
    const EmulatorState* _state = nullptr;
};
