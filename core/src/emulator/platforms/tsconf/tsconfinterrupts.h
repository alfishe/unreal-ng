#pragma once

#include <cstdint>

#include "emulator/cpu/z80.h"

class EmulatorContext;
struct TsConfState;

/// Another source for the line interrupt: the VDAC2 card's FT812 INT_N while
/// the card shows the FT812 picture. In the VDAC2 firmware build the line
/// INT starts on the falling edge of INT_N instead of the line start, for the
/// lines latched with V_CONFIG bit 2 (msel) set:
///   int_start_lin(vdac2_msel ? int_start_ft : line_start_s)  [V] top.v:1093
///   vdac2_msel = vconf[2], vconf latched at line_start_s      [V] video_ports.v:153-157
/// (vdac2-integration-design.md §6)
struct ITsConfLineSource
{
    virtual ~ITsConfLineSource() = default;
    /// msel latched for raster line `line` (0..319) of the current frame
    virtual bool DrivesLine(uint32_t line) const = 0;
    /// Falling INT_N edges up to raster tact `raster` of the current frame,
    /// oldest first, forgotten once taken (TsConfInterrupts::kMaxLineEdges at a time)
    virtual size_t TakeLineEdges(uint32_t raster, uint32_t* out, size_t max) = 0;
};

/// TS-Conf interrupt controller (hardware-spec §5, technical-design §3.4).
///
/// Owns /INT through the generic IInterruptSource: the ULA window of the
/// config (intstart / intlen) is not used for this machine.
///
/// | Source    | INT_MASK bit | Vector | Event (raster tact in the 71680-tact frame) |
/// |:--|:--|:--|:--|
/// | Frame     | 0 (reset 1)  | 0xFF   | VS_INT * 224 + HS_INT (none when HS_INT >= 224 or VS_INT >= 320); a 32 CPU-clock pulse |
/// | Line      | 1            | 0xFD   | 224 n, the end of every one of the 320 lines (the last at tact 0 of the next frame); the VDAC2 FT812 INT_N edge on msel lines (ITsConfLineSource) |
/// | DMA       | 2            | 0xFB   | DMA completion (phase 5)                                 |
/// | Wait-port | 3            | 0xF9   | the AVR's strobe (ZiFi / enhanced RS-232 ISR not zero: RaiseWaitPort) |
///
/// Priority frame > line > DMA > wait-port; an acknowledge clears only the
/// source it served. An event latches only while its mask bit is set;
/// clearing a mask bit clears that latch. While vdos - from the trapped VG93
/// access on (pre_vdos) - the output is gated, the latches keep their events
/// and the frame pulse stands still (deferred, not lost).
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
    /// CPU clocks from sampling /INT to the INTA cycle's IORQ, where the source is chosen
    static constexpr uint32_t kAcknowledgeClocks = 3;

    TsConfInterrupts(EmulatorContext* context, TsConfState& state) : _context(context), _ts(state) {}

    /// Z80 reset: no latched source, events evaluated from the frame start
    void Reset();
    /// An INT_MASK write: a cleared bit drops its latch
    void OnMaskWrite(uint8_t mask);
    /// DMA completion (phase 5)
    void RaiseDma();
    /// The AVR's wait-port strobe (TS firmware: on every main-loop pass while its ZiFi ISR is not zero)
    void RaiseWaitPort();
    /// vdos starts at a trapped VG93 access (pre_vdos) at CPU clock t: the output is gated and the frame pulse's
    /// counter stands still from here ([V] zint.v:194 `!vdos`, top.v:1106 vdos = pre_vdos)
    void OnVdosEnter(uint32_t t);
    /// vdos ends at CPU clock t: a frame pulse frozen by it runs on for the clocks it had left, one whose event
    /// fell inside vdos starts its 32 clocks here
    void OnVdosExit(uint32_t t);
    /// A CPU clock switch at frame T-state t (SYS_CONFIG): a running frame pulse keeps the clocks it has counted
    /// ([V] zint.v:194 counts zpos edges). Before the switch: true and the clocks counted when one runs
    bool BeforeClockSwitch(uint32_t t, uint32_t& elapsed);
    /// After it (t at the new clock): the pulse runs on from `elapsed`
    void AfterClockSwitch(uint32_t t, uint32_t elapsed);
    /// The line INT's other source (the VDAC2 card), nullptr = line starts only
    void SetLineSource(ITsConfLineSource* source) { _lineSource = source; }
    static constexpr size_t kMaxLineEdges = 8;

    /// region <IInterruptSource>
    bool IsIntAsserted(uint32_t t) override;
    uint8_t AcknowledgeInterrupt(uint32_t t) override;
    /// The frame INT pulse counts only CPU clocks without /WAIT ([V] zint.v: intctr counts on
    /// `zpos && !wait_r`): a stretch of the clock freezes a running pulse, and a pulse that starts inside one
    /// begins when it ends. Without it a long wait (the AVR UART's 400-clock accesses, DRAM waits) swallowed the
    /// 32-clock pulse and the program missed the interrupt
    bool ObservesWaits() const override { return true; }
    void OnWait(uint32_t ttBefore, uint32_t ticks) override;
    /// endregion

    /// region <IMachineStepHook>
    void OnMachineStep(uint32_t t) override { CatchUp(RasterAt(t)); }
    void OnMachineFrameRollover(uint32_t frameLength) override;
    /// endregion

    /// Raster tact reached at frame T-state t
    uint32_t RasterAt(uint32_t t) const;
    /// Latch the events up to frame T-state t (before a write changes the mask or the frame INT position)
    void CatchUpTo(uint32_t t) { CatchUp(RasterAt(t)); }

private:
    /// Latch the events of raster tacts [intLastRaster, raster]
    void CatchUp(uint32_t raster);
    /// The line events of [from, raster] with an external line source
    void CatchUpLineWithSource(uint32_t from, uint32_t raster, bool latch);
    /// Is the frame pulse (32 CPU clocks from its event) still running at t?
    bool FramePulseActive(uint32_t t) const;
    /// vdos (or the trapped access that starts it) holds the output and the frame pulse
    bool VdosFrozen() const;
    /// CPU clock (of the frame) the frame pulse counts from
    int64_t FramePulseStart() const;
    /// The frozen interval [intVdosClock, t) ends at CPU clock t: move the frame pulse past it
    void ThawFramePulse(uint32_t t);
    uint32_t Multiplier() const;

    EmulatorContext* _context;
    TsConfState& _ts;
    ITsConfLineSource* _lineSource = nullptr;
};
