#include "tsconfengine.h"

#include <algorithm>

#include "emulator/emulatorcontext.h"
#include "emulator/platforms/tsconf/tsconfarbiter.h"
#include "emulator/platforms/tsconf/tsconfdma.h"
#include "emulator/platforms/tsconf/tsconfgeometry.h"
#include "emulator/platforms/tsconf/tsconfinterrupts.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/platforms/tsconf/tsconftsu.h"
#include "emulator/memory/memory.h"

void TsConfEngine::Reset()
{
    // The latched copies reset with their registers ([V] video_ports.v:138-147);
    // the tile pages and offsets are not reset and keep their latches
    _ts.latVConfig = _ts.regs[TsConfReg::VConfig];
    _ts.latVPage = _ts.regs[TsConfReg::VPage];
    _ts.latPalSel = _ts.regs[TsConfReg::PalSel];
    _ts.latGXOffsL = _ts.regs[TsConfReg::GXOffsL];
    _ts.latGXOffsH = _ts.regs[TsConfReg::GXOffsH];
    _ts.yOffsPending = 0;
    _ts.cntRow = 0;
    _ts.engNextLine = 0;
    _ts.budgetRaster = 0;
    _ts.cpuAccesses = 0;
    _ts.cpuLineAccesses = 0;
    _cpuLineRunning = 0;
    _passLine = kLines;
    RebuildLineTable();
}

void TsConfEngine::RebuildLineTable()
{
    const TsConfLine set = LatchedSet();
    for (TsConfLine& line : _lines)
        line = set;
    if (_ts.engNextLine > 0)
    {
        // The TSU lines of the current line and, when its ts_start passed, the
        // next one (the latches of the previous lines are gone: the current set)
        const uint32_t current = _ts.engNextLine - 1u;
        _passLine = kLines;
        RenderTsu(current, _lines[current], false);
        if (_ts.engNextLine < kLines && _ts.budgetRaster > current * kLineTacts + TsStartTact())
            RenderTsu(_ts.engNextLine, _lines[current], true);
    }
}

uint32_t TsConfEngine::TsStartTact() const
{
    // [V] video_sync.v:130: ts_start at hcount == hpix_beg_ts - 1, hpix_beg_ts =
    // the start of the graphics window of the latched geometry, or dot 88 with
    // T_CONFIG[0] (video_mode.v:196); 2 dots per tact
    const uint32_t start = (_ts.regs[TsConfReg::TConfig] & 0x01) ? TsConfGeometry::kWindows[3].x0
                                                                 : TsConfGeometry::WindowOf(_ts.latVConfig).x0;
    return (start - 1u) / 2u;
}

uint16_t TsConfEngine::VideoCost(uint8_t vConfig, uint32_t line)
{
    // [V] video_mode.v:128-133: ZX 1 of 8 cycles, 16C 1 of 4, 256C 1 of 2, TXT 4 of 8; a block starts on every
    // `len` dots while video_go is high, which is w + 4 dots (video_sync.v:237; TsConfArbiter::FetchOf)
    static constexpr uint8_t kLength[4] = {8, 4, 2, 8};
    static constexpr uint8_t kNeed[4] = {1, 1, 1, 4};
    if ((vConfig & 0x20) || !TsConfGeometry::LineInWindow(vConfig, line))
        return 0;  // NOGFX stops the fetch
    const uint32_t mode = vConfig & 0x03;
    const uint32_t dots = TsConfGeometry::WindowOf(vConfig).w + 4u;
    return static_cast<uint16_t>((dots + kLength[mode] - 1) / kLength[mode] * kNeed[mode]);
}

uint32_t TsConfEngine::VideoCyclesBefore(const TsConfArbiter::Fetch& fetch, uint32_t cycle)
{
    // A block decided at DRAM cycle b gives video cycles b + 1 .. b + need (no CPU asking: video first,
    // arbiter.v:188); measured with tools/machines/tsconf/rtl-sim tsulatch
    if (!fetch.active || fetch.h1 <= fetch.h0)
        return 0;
    const uint32_t blocks = (fetch.h1 - fetch.h0 + fetch.length - 1u) / fetch.length;
    uint32_t count = 0;
    for (uint32_t j = 0; j < fetch.need; j++)
    {
        const uint32_t first = fetch.h0 + 1u + j;  // video cycle j of block 0
        if (cycle > first)
            count += std::min(blocks, (cycle - first + fetch.length - 1u) / fetch.length);
    }
    return count;
}

uint32_t TsConfEngine::PassStart(const TsConfArbiter::Fetch& fetch, uint32_t x0, uint32_t& prefetch)
{
    const uint32_t start = x0 + (prefetch ? 1u : 2u);
    if (prefetch)
    {
        uint32_t cycle = start;
        for (uint32_t words = 0; words < prefetch; cycle++)
            words += IsVideoCycle(fetch, cycle) ? 0u : 1u;
        prefetch += IsVideoCycle(fetch, cycle) ? 0u : 1u;
    }
    return start;
}

bool TsConfEngine::IsVideoCycle(const TsConfArbiter::Fetch& fetch, uint32_t cycle)
{
    if (!fetch.active || cycle <= fetch.h0)
        return false;
    const uint32_t d = cycle - fetch.h0 - 1u;
    return d / fetch.length < (fetch.h1 - fetch.h0 + fetch.length - 1u) / fetch.length && d % fetch.length < fetch.need;
}

void TsConfEngine::RenderTsu(uint32_t line, const TsConfLine& latch, bool split)
{
    // The TS window: the graphics window, or all 360x288 with T_CONFIG[0]
    TsConfLine& set = _lines[line];
    const uint8_t tConfig = _ts.regs[TsConfReg::TConfig];
    const TsConfGeometry::Window& win = (tConfig & 0x01) ? TsConfGeometry::kWindows[3] : TsConfGeometry::WindowOf(latch.vConfig);
    set.tsX0 = win.x0;
    set.tsW = win.w;
    set.tsu = false;
    set.tsuCost = 0;
    set.tsuSplit = 0xFFFF;
    if (_passLine == line)
        _passLine = kLines;
    if (!_context->pMemory || !(tConfig & 0xE0))
        return;  // TSU off (T_CONFIG[7:5] = 0): no prefetch, no pass, no DRAM
    const uint8_t* ram = _context->pMemory->RAMBase();

    // The hardware works on this line during the previous one: first the
    // tilemap prefetch (window [y0 - 17, y0 + h - 9) of the previous line,
    // for TS line + 16), then the line itself ([V] video_sync.v:229-230)
    uint32_t used = 0;
    const uint32_t previous = line ? line - 1 : kLines - 1;
    if (previous + 17 >= win.y0 && previous + 9 < static_cast<uint32_t>(win.y0 + win.h))
        used += TsConfTsu::Prefetch(_ts, ram, (line - win.y0 + 16) & 0x1FF, _mapRing);

    // The pass runs from ts_start (dot x0 - 1 of the previous line) to the next ts_start; video keeps its blocks
    // (the line's graphics fetch). The TSU starts at DRAM cycle x0 + 1, x0 + 2 without a prefetch, and the
    // prefetch holds one more cycle after its last word unless video takes that cycle (tools/machines/tsconf/
    // rtl-sim tsulatch: `tmap` drops two clocks after tm_end, video_ts.v:104-121,139)
    const TsConfLine& shown = _lines[previous];
    const TsConfArbiter::Fetch fetch = TsConfArbiter::FetchOf(shown, previous);
    const uint32_t x0 = win.x0;
    const uint32_t start = PassStart(fetch, x0, used);

    if (line >= win.y0 && line < static_cast<uint32_t>(win.y0 + win.h))
    {
        // The cycles video leaves up to line_start (after cycle 447) are the split, then this line's up to x0;
        // the CPU (its previous line) takes its share. Objects beyond the budget are dropped (TSU-8)
        const uint32_t span = kLineAccesses - start;
        const uint32_t freeBefore = span - (VideoCost(shown.vConfig, previous) - VideoCyclesBefore(fetch, start));
        const uint32_t freeAfter = x0 - VideoCyclesBefore(TsConfArbiter::FetchOf(shown, line), x0);
        const uint32_t cpu = _ts.cpuLineAccesses;
        const uint32_t budget = freeBefore + freeAfter > cpu ? freeBefore + freeAfter - cpu : 0u;
        uint32_t limit = UINT32_MAX;
        if (split)
        {
            const uint32_t cpuBefore = cpu * span / kLineAccesses;
            limit = freeBefore > cpuBefore ? freeBefore - cpuBefore : 0u;
            set.tsuSplit = static_cast<uint16_t>(std::min<uint32_t>(limit, 0xFFFE));
        }
        set.tsu = TsConfTsu::BeginLine(_ts, latch, ram, _mapRing, line - win.y0, win.w, _tsu[line], budget, limit, used,
                                       _pass);
        used = _pass.used;
        if (_pass.paused)
            _passLine = line;
    }
    set.tsuCost = static_cast<uint16_t>(used);
}

bool TsConfEngine::ProbeTsuLine(uint32_t line, uint8_t* indices, TsConfTsu::Source* sources) const
{
    if (line >= kLines || !_context->pMemory)
        return false;
    const TsConfLine& set = _lines[line];
    if (!set.tsu || line == 0)
        return false;
    // Drawn during the previous line with its latches, the objects after line_start with this line's (TSU-6)
    const TsConfLine& latch = _lines[line - 1];
    const uint8_t tConfig = _ts.regs[TsConfReg::TConfig];
    const TsConfGeometry::Window& win = (tConfig & 0x01) ? TsConfGeometry::kWindows[3] : TsConfGeometry::WindowOf(latch.vConfig);
    if (line < win.y0 || line >= static_cast<uint32_t>(win.y0 + win.h))
        return false;
    const uint8_t* ram = _context->pMemory->RAMBase();

    // The ring as the prefetches of the 24 lines up to this one left it (RenderTsu's window rule)
    TsConfTsu::MapRing ring{};
    uint32_t used = 0;
    for (uint32_t back = 24; back != UINT32_MAX; back--)
    {
        if (line < back)
            continue;
        const uint32_t raster = line - back;
        const uint32_t previous = raster ? raster - 1 : kLines - 1;
        if (previous + 17 >= win.y0 && previous + 9 < static_cast<uint32_t>(win.y0 + win.h))
        {
            uint32_t fetched = TsConfTsu::Prefetch(_ts, ram, (raster - win.y0 + 16) & 0x1FF, ring);
            if (back == 0)
            {
                // This line's own prefetch counts in its budget, with the cycle it holds after its words
                PassStart(TsConfArbiter::FetchOf(latch, line - 1), win.x0, fetched);
                used = fetched;
            }
        }
    }
    // The budget the line used: an object that fitted then fits now, the first dropped one is dropped again
    return TsConfTsu::ProbeLine(_ts, latch, set, ram, ring, line - win.y0, win.w, indices, sources, set.tsuCost,
                                set.tsuSplit, used);
}

void TsConfEngine::AccountBudget(uint32_t raster)
{
    const uint32_t start = _ts.budgetRaster;
    const uint32_t cpu = _ts.cpuAccesses;
    _ts.cpuAccesses = 0;
    _cpuLineRunning += cpu;

    // Idle DMA: the free cycles go to nobody - only the position moves (it
    // also marks which ts_start events have passed, TSU-6)
    if (!_dma.Busy()) [[likely]]
    {
        if (raster > _ts.budgetRaster)
            _ts.budgetRaster = raster;
        return;
    }

    // The SPI / IDE device phase is time: 8 fclk per raster tact, whatever the DRAM load
    if (raster > _ts.budgetRaster)
        _ts.dmaDeviceFclk += (raster - _ts.budgetRaster) * kFclkPerTact;

    uint32_t free = 0;
    for (uint32_t pos = _ts.budgetRaster; pos < raster;)
    {
        const uint32_t line = pos / kLineTacts;
        const uint32_t lineStart = line * kLineTacts;
        const uint32_t end = std::min(lineStart + kLineTacts, raster);
        const TsConfLine& set = _lines[line];
        const uint32_t a = pos - lineStart;
        const uint32_t b = end - lineStart;
        const uint32_t dots = 2 * (b - a);  // DRAM cycles of the span
        // The video takes its cycles inside its fetch window [h0, h1) only ([V] arbiter.v:171-189): the border
        // gives the DMA every cycle. The TSU's are spread over the line. Both telescope over calls
        uint32_t share = set.tsuCost * b / kLineTacts - set.tsuCost * a / kLineTacts;
        if (set.videoCost)
        {
            const TsConfArbiter::Fetch fetch = TsConfArbiter::FetchOf(set, line);
            const uint32_t h0 = fetch.h0;
            const uint32_t width = fetch.h1 > fetch.h0 ? fetch.h1 - fetch.h0 : 0u;
            auto taken = [&](uint32_t cycle) {
                const uint32_t in = cycle > h0 ? std::min(cycle - h0, width) : 0u;
                return width ? set.videoCost * in / width : 0u;
            };
            share += taken(2 * b) - taken(2 * a);
        }
        free += dots > share ? dots - share : 0;
        pos = end;
    }
    if (raster > _ts.budgetRaster)
        _ts.budgetRaster = raster;
    free = free > cpu ? free - cpu : 0;

    if (_dma.Busy())
    {
        _ts.dmaCredit += free;
        if (_dma.WritesCram() && _videoFlush && raster > start)
        {
            // CRAM is read at the dot (hs §4.3): place each word in the accounted
            // span by its share of the credit and draw the picture up to there
            // first, so the dots before the write keep the old colour (TIM-5)
            const uint32_t credit = _ts.dmaCredit;
            const uint32_t span = raster - start;
            _ts.dmaCredit -= _dma.RunEach(credit, [&](uint32_t used) {
                _videoFlush(start + static_cast<uint32_t>(credit ? static_cast<uint64_t>(used) * span / credit : 0));
            });
        }
        else
        {
            _ts.dmaCredit -= _dma.Run(_ts.dmaCredit);
        }

        // A device transfer waits word by word: the time and the DRAM cycles it could not use while the other
        // was short do not bank up for a burst later (one word's worth stays: the word in progress)
        if (const uint32_t device = _dma.DeviceFclk())
        {
            _ts.dmaDeviceFclk = std::min(_ts.dmaDeviceFclk, device);
            _ts.dmaCredit = std::min(_ts.dmaCredit, _dma.WordCost());
        }
    }
}

uint32_t TsConfEngine::RasterAt(uint32_t t) const
{
    const uint32_t multiplier = std::max<uint32_t>(_context->emulatorState.current_z80_frequency_multiplier, 1u);
    return std::min<uint32_t>(t / multiplier, kFrameTacts - 1);
}

uint32_t TsConfEngine::LineAt(uint32_t t) const
{
    return RasterAt(t) / kLineTacts;
}

TsConfLine TsConfEngine::LatchedSet() const
{
    TsConfLine set;
    set.vConfig = _ts.latVConfig;
    set.vPage = _ts.latVPage;
    set.palSel = _ts.latPalSel;
    set.t0GPage = _ts.latT0GPage;
    set.t1GPage = _ts.latT1GPage;
    set.gxOffs = static_cast<uint16_t>(_ts.latGXOffsL | ((_ts.latGXOffsH & 1u) << 8));
    set.t0XOffs = static_cast<uint16_t>(_ts.latT0XOffsL | ((_ts.latT0XOffsH & 1u) << 8));
    set.t1XOffs = static_cast<uint16_t>(_ts.latT1XOffsL | ((_ts.latT1XOffsH & 1u) << 8));
    set.cntRow = _ts.cntRow;
    return set;
}

void TsConfEngine::LineStart(uint32_t line)
{
    const uint32_t previous = line ? line - 1 : kLines - 1;

    // Row counter, with the geometry the previous line was shown with:
    // reload at the end of line 31 and after a G_Y_OFFS write, else step
    // after every line of the graphics window
    const uint16_t yOffs = static_cast<uint16_t>(_ts.regs[TsConfReg::GYOffsL] | ((_ts.regs[TsConfReg::GYOffsH] & 1u) << 8));
    if (previous == TsConfGeometry::kFirstVisibleLine - 1 || _ts.yOffsPending)
        _ts.cntRow = yOffs;
    else if (TsConfGeometry::LineInWindow(_ts.latVConfig, previous))
        _ts.cntRow = static_cast<uint16_t>((_ts.cntRow + 1) & 0x1FF);
    _ts.yOffsPending = 0;

    const uint8_t* r = _ts.regs;
    _ts.latVConfig = r[TsConfReg::VConfig];
    _ts.latVPage = r[TsConfReg::VPage];
    _ts.latPalSel = r[TsConfReg::PalSel];
    _ts.latGXOffsL = r[TsConfReg::GXOffsL];
    _ts.latGXOffsH = r[TsConfReg::GXOffsH];
    _ts.latT0GPage = r[TsConfReg::T0GPage];
    _ts.latT1GPage = r[TsConfReg::T1GPage];
    _ts.latT0XOffsL = r[TsConfReg::T0XOffsL];
    _ts.latT0XOffsH = r[TsConfReg::T0XOffsL + 1];
    _ts.latT1XOffsL = r[TsConfReg::T0XOffsL + 4];
    _ts.latT1XOffsH = r[TsConfReg::T0XOffsL + 5];

    // The CPU's DRAM reads of the line that just ended feed the TSU budget
    _ts.cpuLineAccesses = static_cast<uint16_t>(std::min<uint32_t>(_cpuLineRunning, 0xFFFF));
    _cpuLineRunning = 0;

    // The TSU drew this line during the previous one (TSU-6): keep its result
    const TsConfLine rendered = _lines[line];
    _lines[line] = LatchedSet();
    _lines[line].videoCost = VideoCost(_lines[line].vConfig, line);
    if (line > 0)
    {
        _lines[line].tsX0 = rendered.tsX0;
        _lines[line].tsW = rendered.tsW;
        _lines[line].tsu = rendered.tsu;
        _lines[line].tsuCost = rendered.tsuCost;
        _lines[line].tsuSplit = rendered.tsuSplit;
    }

    // The objects of this line's pass after line_start take the latch just taken (TsConfTsu "When")
    if (_passLine == line)
    {
        _passLine = kLines;
        if (_context->pMemory)
        {
            TsConfTsu::FinishLine(_ts, _lines[line], _context->pMemory->RAMBase(), _mapRing, _tsu[line], _pass);
            _lines[line].tsuCost = static_cast<uint16_t>(_pass.used);
        }
    }
}

void TsConfEngine::CatchUp(uint32_t t)
{
    const uint32_t raster = RasterAt(t);
    // Line by line: the budget of a line is accounted before the next line
    // starts (the CPU reads of that line feed the TSU budget)
    // Events in raster order: line starts, and ts_start of the current line,
    // where the TSU draws the next line (TSU-6). A ts_start has happened once
    // the budget was accounted past it, which TTD restores with the state
    for (;;)
    {
        uint32_t next = UINT32_MAX;
        bool tsStart = false;
        if (_ts.engNextLine < kLines)
            next = _ts.engNextLine * kLineTacts;
        if (_ts.engNextLine >= 1 && _ts.engNextLine < kLines)
        {
            const uint32_t event = (_ts.engNextLine - 1u) * kLineTacts + TsStartTact();
            if (_ts.budgetRaster <= event && event < next)
            {
                next = event;
                tsStart = true;
            }
        }
        if (next == UINT32_MAX || next > raster)
            break;
        if (tsStart)
        {
            AccountBudget(next + 1);
            RenderTsu(_ts.engNextLine, _lines[_ts.engNextLine - 1u], true);
        }
        else
        {
            AccountBudget(next);
            LineStart(_ts.engNextLine++);
        }
    }
    AccountBudget(raster);
}

void TsConfEngine::SetLiveVideoPage(uint8_t vPage)
{
    _ts.latVPage = vPage;
    const uint32_t current = _ts.engNextLine ? _ts.engNextLine - 1u : 0u;
    _lines[current].vPage = vPage;
}

void TsConfEngine::OnMachineStep(uint32_t t)
{
    _interrupts.OnMachineStep(t);
    CatchUp(t);
}

void TsConfEngine::OnMachineFrameRollover(uint32_t frameLength)
{
    // Every line of the old frame started and its DRAM cycles are accounted;
    // the next frame starts at line 0
    CatchUp(kFrameTacts * std::max<uint32_t>(_context->emulatorState.current_z80_frequency_multiplier, 1u));
    AccountBudget(kFrameTacts);
    _ts.engNextLine = 0;
    _ts.budgetRaster = 0;
    _cpuLineRunning = 0;
    _passLine = kLines;
    _interrupts.OnMachineFrameRollover(frameLength);
    // Last: the interrupt controller has taken the old frame's line events
    // (the VDAC2 card's edges are tacts of the frame it closes here)
    if (_frameEnd)
        _frameEnd();
}
