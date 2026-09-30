#pragma once

#include <cstdint>
#include <functional>

#include "emulator/cpu/z80.h"
#include "emulator/platforms/tsconf/tsconftsu.h"

class EmulatorContext;
class TsConfDma;
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
    uint16_t tsX0 = 0;    ///< TS window: first raster dot
    uint16_t tsW = 0;     ///< TS window width in dots
    bool tsu = false;     ///< the TSU drew something on this line
    uint16_t videoCost = 0;  ///< DRAM accesses of the graphics fetch on this line
    uint16_t tsuCost = 0;    ///< DRAM accesses of the TSU (prefetch + objects)
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
/// It also drives the interrupt controller and the DMA, which gets what is left
/// of each line's 448 DRAM accesses (one per 7 MHz dot) after the graphics
/// fetch (ZX 1/8, 16C 1/4, 256C and TXT 1/2 of the window dots), the TSU and
/// the CPU (its DRAM reads, counted by TsConfMemory; its writes are not counted -
/// a v1 approximation). All persistent state lives in TsConfState (TTD).
class TsConfEngine : public IMachineStepHook
{
public:
    static constexpr uint32_t kLineTacts = 224;
    static constexpr uint32_t kLines = 320;
    static constexpr uint32_t kFrameTacts = kLineTacts * kLines;

    static constexpr uint32_t kLineAccesses = 448;

    TsConfEngine(EmulatorContext* context, TsConfState& state, TsConfInterrupts& interrupts, TsConfDma& dma)
        : _context(context), _ts(state), _interrupts(interrupts), _dma(dma)
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
    /// TSU pixel (CRAM index, 0 = transparent) at raster dot `dot` of line `line`
    uint8_t TsuPixel(uint32_t line, uint32_t dot) const
    {
        const TsConfLine& set = Line(line);
        if (!set.tsu || dot < set.tsX0 || dot >= static_cast<uint32_t>(set.tsX0 + set.tsW))
            return 0;
        return _tsu[line][dot - set.tsX0];
    }

    /// Debug (video mapper): TS-window line of raster line `line` drawn again
    /// with each pixel's source - the tilemap ring rebuilt as the prefetch of
    /// the lines before built it, the budget the line actually used, the
    /// TSU registers and SFILE as they are now (so it matches what was drawn
    /// while they have not changed since). `indices` / `sources` take
    /// TsConfTsu::kMaxWidth entries, x relative to Line(line).tsX0
    /// @return false when the TSU drew nothing on that line
    bool ProbeTsuLine(uint32_t line, uint8_t* indices, TsConfTsu::Source* sources) const;

    /// Draw the picture up to raster tact `raster` (the screen's hook): a DMA
    /// CRAM write draws the dots before it with the old colour (TIM-5)
    void SetVideoFlush(std::function<void(uint32_t raster)> flush) { _videoFlush = std::move(flush); }

    /// Tact of ts_start in the current line: where the TSU draws the next line (TSU-6)
    uint32_t TsStartTact() const;

    /// The TSU line buffer of line `line` (valid for Line(line).tsX0 .. + tsW
    /// when Line(line).tsu; 0 = transparent)
    const uint8_t* TsuRow(uint32_t line) const { return _tsu[line < kLines ? line : kLines - 1]; }

    /// region <IMachineStepHook>
    void OnMachineStep(uint32_t t) override;
    void OnMachineFrameRollover(uint32_t frameLength) override;
    /// endregion

private:
    uint32_t RasterAt(uint32_t t) const;
    void LineStart(uint32_t line);
    /// TS window of the line and its TSU pixels (hs §4.4)
    /// The TSU draws `line` into its buffer and line entry, with the registers
    /// latched for `latch` (the line during which it works)
    void RenderTsu(uint32_t line, const TsConfLine& latch);

    TsConfLine LatchedSet() const;
    /// Hand the DMA its share of the DRAM cycles from budgetRaster to `raster`
    void AccountBudget(uint32_t raster);
    static uint16_t VideoCost(uint8_t vConfig, uint32_t line);

    EmulatorContext* _context;
    TsConfState& _ts;
    TsConfInterrupts& _interrupts;
    TsConfDma& _dma;
    uint32_t _cpuLineRunning = 0;  ///< CPU DRAM reads on the current line so far
    std::function<void(uint32_t)> _videoFlush;
    TsConfLine _lines[kLines];
    uint8_t _tsu[kLines][360] = {};  ///< TSU line buffers of the frame (TsConfTsu::kMaxWidth)
    /// Tilemap prefetch ring (TsConfTsu::MapRing). Not TTD state: every frame
    /// refills it before the TS window starts (the prefetch runs from 17 lines
    /// above the window, and the window starts at line 32 or later)
    uint16_t _mapRing[4][64][2] = {};
};
