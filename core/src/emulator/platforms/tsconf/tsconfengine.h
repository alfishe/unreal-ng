#pragma once

#include <cstdint>

#include "emulator/cpu/z80.h"

class EmulatorContext;
class TsConfInterrupts;
struct TsConfState;

/// Registers one raster line is displayed with (the line-latched set of
/// hardware-spec §3.2 plus the graphics row counter)
struct TsConfLine
{
    uint8_t vConfig = 0;
    uint8_t vPage = 5;
    uint8_t palSel = 0x0F;
    uint8_t t0GPage = 0;
    uint8_t t1GPage = 0;
    uint16_t gxOffs = 0;
    uint16_t t0XOffs = 0;
    uint16_t t1XOffs = 0;
    uint16_t cntRow = 0;  ///< graphics row (9 bit, includes G_Y_OFFS)
};

/// TS-Conf line engine (TSConf technical-design §3.8): advances with the CPU
/// through the generic IMachineStepHook, on rendered and turbo-skipped frames
/// alike, so everything it decides is independent of the screen.
///
/// At every line start ([V] video_sync.v `line_start`, the last dot of the
/// previous line) it
/// - latches V_CONFIG, V_PAGE, G_X_OFFS, PAL_SEL, T0/T1_G_PAGE, T0/T1_X_OFFS
///   ([V] video_ports.v:153-164);
/// - moves the graphics row counter: reload with G_Y_OFFS at the end of line 31
///   and at the first line start after a G_Y_OFFS write, else +1 after every
///   line of the graphics window ([V] video_sync.v:176-181);
/// - records the line's set in the frame's line table, which ScreenTSConf
///   draws from.
/// It also drives the interrupt controller. The per-line DRAM budget
/// (video / CPU / TSU / DMA) comes with its consumers, the TSU and the DMA
/// (phases 4-5). All persistent state lives in TsConfState (TTD).
class TsConfEngine : public IMachineStepHook
{
public:
    static constexpr uint32_t kLineTacts = 224;
    static constexpr uint32_t kLines = 320;
    static constexpr uint32_t kFrameTacts = kLineTacts * kLines;

    TsConfEngine(EmulatorContext* context, TsConfState& state, TsConfInterrupts& interrupts)
        : _context(context), _ts(state), _interrupts(interrupts)
    {
    }

    /// Z80 reset: the reset latches, row counter 0, the frame starts over
    void Reset();
    /// After a TTD restore: the line table from the restored latches
    void RebuildLineTable();

    /// Process the line starts up to frame T-state t (idempotent). The port
    /// decoder calls it before a write that the next line start would latch,
    /// so a latch taken during the current instruction sees the old value
    void CatchUp(uint32_t t);
    /// #7FFD: V_PAGE changes at once, also for the rest of the current line
    void SetLiveVideoPage(uint8_t vPage);

    /// The set line `line` (0..319) of the current frame is displayed with
    const TsConfLine& Line(uint32_t line) const { return _lines[line < kLines ? line : kLines - 1]; }
    /// The raster line the beam is on at frame T-state t
    uint32_t LineAt(uint32_t t) const;

    /// region <IMachineStepHook>
    void OnMachineStep(uint32_t t) override;
    void OnMachineFrameRollover(uint32_t frameLength) override;
    /// endregion

private:
    uint32_t RasterAt(uint32_t t) const;
    void LineStart(uint32_t line);
    TsConfLine LatchedSet() const;

    EmulatorContext* _context;
    TsConfState& _ts;
    TsConfInterrupts& _interrupts;
    TsConfLine _lines[kLines];
};
