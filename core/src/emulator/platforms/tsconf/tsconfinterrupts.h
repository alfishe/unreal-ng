#pragma once

#include <cstdint>

#include "emulator/cpu/z80.h"

class EmulatorContext;
struct TsConfState;

/// TS-Conf interrupt controller (hardware-spec §5, technical-design §3.4).
///
/// Owns /INT through the generic IInterruptSource: the ULA window of the
/// config (intstart / intlen) is not used for this machine.
///
/// | Source    | INT_MASK bit | Vector | Event (raster tact in the 71680-tact frame) |
/// |:--|:--|:--|:--|
/// | Frame     | 0 (reset 1)  | 0xFF   | VS_INT * 224 + HS_INT (none when HS_INT >= 224 or VS_INT >= 320); a 32 CPU-clock pulse |
/// | Line      | 1            | 0xFD   | 224 n - 1 on every one of the 320 lines                  |
/// | DMA       | 2            | 0xFB   | DMA completion (phase 5)                                 |
/// | Wait-port | 3            | 0xF9   | not emulated                                             |
///
/// Priority frame > line > DMA > wait-port; an acknowledge clears only the
/// source it served. An event latches only while its mask bit is set;
/// clearing a mask bit clears that latch. While vdos the output is gated and
/// the latches keep their events (deferred, not lost).
///
/// Raster tact = Z80::t / current_z80_frequency_multiplier: the frame always
/// has 71680 raster tacts, the CPU runs 1, 2 or 4 clocks per tact (SYS_CONFIG).
/// Events are evaluated lazily up to the tact the CPU has reached, from the
/// step hook (after every instruction) and at every INT check, so no event is
/// missed or seen early. All state lives in TsConfState (TTD).
class TsConfInterrupts : public IInterruptSource, public IMachineStepHook
{
public:
    static constexpr uint32_t kLineTacts = 224;
    static constexpr uint32_t kLines = 320;
    static constexpr uint32_t kFrameTacts = kLineTacts * kLines;  // 71680
    static constexpr uint32_t kFramePulseClocks = 32;

    TsConfInterrupts(EmulatorContext* context, TsConfState& state) : _context(context), _ts(state) {}

    /// Z80 reset: no latched source, events evaluated from the frame start
    void Reset();
    /// An INT_MASK write: a cleared bit drops its latch
    void OnMaskWrite(uint8_t mask);
    /// DMA completion (phase 5)
    void RaiseDma();

    /// region <IInterruptSource>
    bool IsIntAsserted(uint32_t t) override;
    uint8_t AcknowledgeInterrupt(uint32_t t) override;
    /// endregion

    /// region <IMachineStepHook>
    void OnMachineStep(uint32_t t) override { CatchUp(RasterAt(t)); }
    void OnMachineFrameRollover(uint32_t frameLength) override;
    /// endregion

    /// Raster tact reached at frame T-state t
    uint32_t RasterAt(uint32_t t) const;

private:
    /// Latch the events of raster tacts [intLastRaster, raster]
    void CatchUp(uint32_t raster);
    /// Is the frame pulse (32 CPU clocks from its event) still running at t?
    bool FramePulseActive(uint32_t t) const;
    uint32_t Multiplier() const;

    EmulatorContext* _context;
    TsConfState& _ts;
};
