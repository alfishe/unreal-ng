#pragma once

/// @file scorpionturbooverlay.h
/// @brief Scorpion ZS-256 Turbo+ wait states at 7 MHz, the SC15.1 firmware of the turbo board's logic chip
/// (docs/inprogress/2026-09-29-machine-waits/tdd.md section 4, research-scorpion-turbo.md section 4).
///
/// In turbo the CPU runs at 7 MHz, but the DRAM still gives it only the slots the video does not use: one slot
/// every 4 clocks while the picture is fetched, one every 2 in the border. A RAM access waits for the next
/// slot; an opcode fetch waits one clock more (the chip's forced wait, so the fetch samples a settled byte).
/// In 7 MHz clocks, `e2` = the clock edge that starts the access's T2 and `p(e) = (e + 3) mod 4`:
///
/// | access | wait |
/// |:--|:--|
/// | data read or write, RAM | the first `j >= 0` with `p(e2 + j)` even and (border or `p == 0`): 0-3 in the picture, 0-1 in the border |
/// | opcode fetch, RAM | the first `j >= 1` with `p(e2 + j)` odd and (border or `p == 1`): 1-4 in the picture, 1-2 in the border |
/// | ROM | 0 |
/// | I/O cycle | 2 (the port decoder adds them) |
///
/// The picture is the fetch window: 128 T (256 clocks) of each of the 192 lines, from 14336 T after INT.
///
/// Worked example: a NOP stream in RAM takes 8 clocks per NOP in the picture (no gain over 3.5 MHz) and 6 in
/// the border; `LD A,(HL)` from RAM to RAM 12 and 10.
///
/// Installed by the Scorpion port decoder while turbo is on (PortDecoder_Scorpion256::SyncTurboWaits); it adds
/// waits only with the `contention` feature on. Not modeled: the drop to 3.5 MHz while /INT is active, and the
/// SC15.3 firmware's rule.

#include <cstdint>

#include "emulator/memory/hostbusoverlay.h"
#include "emulator/platform.h"

class Core;
class Memory;
class Z80;

class ScorpionTurboOverlay final : public HostBusOverlay
{
public:
    /// `paperStartT`: the frame T-state (3.5 MHz) of the fetch window's first line; `logic`: the logic chip's
    /// firmware (SC15.1 adds a wait to every opcode fetch and two T to every I/O cycle, SC15.3 neither: fetches
    /// wait for the slot like data, I/O takes one T more)
    ScorpionTurboOverlay(Core* core, Z80* cpu, Memory* memory, const EmulatorState* state, uint32_t paperStartT,
                         ScorpionTurboLogic logic = ScorpionTurboLogic::SC151);

    /// The I/O cycle's extra T in turbo (the port decoder adds them)
    uint32_t IoWaits() const { return _logic == ScorpionTurboLogic::SC153 ? 1u : 2u; }

    /// The CPU runs at 7 MHz and the `contention` feature is on: the waits apply
    bool WaitsApply() const;

    /// Wait clocks for an access whose T1 starts on 7 MHz clock `start` of the frame (the rule above)
    uint32_t DataWait(uint32_t start) const;
    uint32_t OpcodeFetchWait(uint32_t start) const;
    /// The same rule edge by edge (the fallback when a wait crosses the window's edge, and the tests' reference)
    uint32_t DataWaitByEdges(uint32_t start) const;
    uint32_t OpcodeFetchWaitByEdges(uint32_t start) const;

    uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) override;
    uint8_t onReadM1(uint16_t addr, uint8_t normal, bool romPaged) override;
    void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;

private:
    /// Clock `e` is inside the fetch window
    bool InPicture(uint32_t e) const;
    /// Whether clock `e` is inside the fetch window, and for how many clocks from `e` that stays so
    uint32_t PictureRun(uint32_t e, bool& picture) const;
    void Wait(uint16_t addr, bool opcodeFetch);

    Core* _core = nullptr;
    Z80* _cpu = nullptr;
    Memory* _memory = nullptr;
    const EmulatorState* _state = nullptr;
    uint32_t _windowStart = 0;
    ScorpionTurboLogic _logic = ScorpionTurboLogic::SC151;  ///< the fetch window's first clock (7 MHz) in the frame: p = 0
};
