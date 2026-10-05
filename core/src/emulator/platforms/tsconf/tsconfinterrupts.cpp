#include "tsconfinterrupts.h"

#include <algorithm>

#include "emulator/emulatorcontext.h"
#include "emulator/platforms/tsconf/tsconfstate.h"

void TsConfInterrupts::Reset()
{
    _ts.intPending = 0;
    _ts.intLastRaster = 0;
    _ts.intFrameRaster = 0;
    _ts.intVdosClock = 0;
}

void TsConfInterrupts::OnMaskWrite(uint8_t mask)
{
    _ts.intPending &= mask;
}

void TsConfInterrupts::RaiseDma()
{
    if (_ts.regs[TsConfReg::IntMask] & TsConfInt::Dma)
        _ts.intPending |= TsConfInt::Dma;
}

void TsConfInterrupts::RaiseWaitPort()
{
    if (_ts.regs[TsConfReg::IntMask] & TsConfInt::WaitPort)
        _ts.intPending |= TsConfInt::WaitPort;
}

uint32_t TsConfInterrupts::Multiplier() const
{
    const uint32_t multiplier = _context->emulatorState.current_z80_frequency_multiplier;
    return multiplier ? multiplier : 1;
}

uint32_t TsConfInterrupts::RasterAt(uint32_t t) const
{
    return std::min<uint32_t>(t / Multiplier(), kFrameTacts - 1);
}

void TsConfInterrupts::CatchUp(uint32_t raster)
{
    const uint32_t from = _ts.intLastRaster;
    if (raster < from)
        return;  // already evaluated (a replayed step)

    const uint8_t mask = _ts.regs[TsConfReg::IntMask];

    // Frame: VS_INT * 224 + HS_INT, 9-bit VS_INT; out-of-range values never match
    const uint32_t hs = _ts.regs[TsConfReg::HsInt];
    const uint32_t vs = _ts.regs[TsConfReg::VsIntL] | ((_ts.regs[TsConfReg::VsIntH] & 0x01u) << 8);
    if ((mask & TsConfInt::Frame) && hs < kLineTacts && vs < kLines)
    {
        const uint32_t frameEvent = vs * kLineTacts + hs;
        if (frameEvent >= from && frameEvent <= raster)
        {
            _ts.intPending |= TsConfInt::Frame;
            _ts.intFrameRaster = static_cast<int32_t>(frameEvent);
        }
    }

    // Line: the end of every line, raster tact 224 n ([V] video_sync.v:125 line_start_s = hcount 447 && c3, the
    // last fclk of the line: int_lin is set from fclk 1792, the next line's first tact). The last line's event is
    // tact 0 of the next frame
    if (_lineSource) [[unlikely]]
        CatchUpLineWithSource(from, raster, (mask & TsConfInt::Line) != 0);
    else if ((mask & TsConfInt::Line) && (from == 0 || raster / kLineTacts > (from - 1) / kLineTacts))
        _ts.intPending |= TsConfInt::Line;

    _ts.intLastRaster = raster + 1;
}

void TsConfInterrupts::CatchUpLineWithSource(uint32_t from, uint32_t raster, bool latch)
{
    // The source's edges are taken even while the mask is off: an event
    // latches only while its mask bit is set, a masked edge is gone
    uint32_t edges[kMaxLineEdges];
    for (;;)
    {
        const size_t count = _lineSource->TakeLineEdges(raster, edges, kMaxLineEdges);
        for (size_t i = 0; i < count && latch; i++)
        {
            // An edge before `from` (seen at a bus access inside the current
            // instruction) counts at `from`
            const uint32_t at = std::max(edges[i], from);
            if (_lineSource->DrivesLine(std::min(at, kFrameTacts - 1) / kLineTacts))
                _ts.intPending |= TsConfInt::Line;
        }
        if (count < kMaxLineEdges)
            break;
    }
    if (!latch)
        return;

    // The ends of the lines the source does not drive: line l ends at tact 224 (l + 1), the last line at tact 0 of
    // the next frame (its msel is read as the source keeps it then)
    for (uint32_t k = (from + kLineTacts - 1) / kLineTacts; k <= raster / kLineTacts; k++)
    {
        const uint32_t line = (k + kLines - 1) % kLines;
        if (!_lineSource->DrivesLine(line))
            _ts.intPending |= TsConfInt::Line;
    }
}

bool TsConfInterrupts::FramePulseActive(uint32_t t) const
{
    const int64_t start = static_cast<int64_t>(_ts.intFrameRaster) * Multiplier();
    return static_cast<int64_t>(t) - start < static_cast<int64_t>(kFramePulseClocks);
}

bool TsConfInterrupts::VdosFrozen() const
{
    return _ts.vdos || _ts.preVdos;
}

bool TsConfInterrupts::IsIntAsserted(uint32_t t)
{
    CatchUp(RasterAt(t));

    // vdos gates the output; the latches keep their events and the frame pulse stands still (§5)
    if (VdosFrozen())
        return false;

    // The frame pulse ends by itself after 32 CPU clocks
    if ((_ts.intPending & TsConfInt::Frame) && !FramePulseActive(t))
        _ts.intPending &= static_cast<uint8_t>(~TsConfInt::Frame);

    return _ts.intPending != 0;
}

void TsConfInterrupts::OnVdosEnter(uint32_t t)
{
    // Events up to the trapped access are latched; a pulse that is over by then is dropped (as IsIntAsserted does)
    CatchUp(RasterAt(t));
    if ((_ts.intPending & TsConfInt::Frame) && !FramePulseActive(t))
        _ts.intPending &= static_cast<uint8_t>(~TsConfInt::Frame);
    _ts.intVdosClock = t;
}

void TsConfInterrupts::OnVdosExit(uint32_t t)
{
    CatchUp(RasterAt(t));
    ThawFramePulse(t);
}

void TsConfInterrupts::ThawFramePulse(uint32_t t)
{
    if (!(_ts.intPending & TsConfInt::Frame))
        return;
    const uint32_t multiplier = Multiplier();
    const int64_t start = static_cast<int64_t>(_ts.intFrameRaster) * multiplier;
    if (start < static_cast<int64_t>(_ts.intVdosClock))
    {
        // Running when vdos began: the counter stood still for the whole interval
        const uint32_t frozen = t - _ts.intVdosClock;
        _ts.intFrameRaster += static_cast<int32_t>((frozen + multiplier - 1) / multiplier);
    }
    else
    {
        // The event fell inside vdos: the counter was reset there and counts from the end
        _ts.intFrameRaster = static_cast<int32_t>((t + multiplier - 1) / multiplier);
    }
}

uint8_t TsConfInterrupts::AcknowledgeInterrupt(uint32_t t)
{
    static constexpr struct
    {
        uint8_t bit;
        uint8_t vector;
    } kPriority[] = {
        {TsConfInt::Frame, 0xFF},
        {TsConfInt::Line, 0xFD},
        {TsConfInt::Dma, 0xFB},
        {TsConfInt::WaitPort, 0xF9},
    };

    // The source is chosen at the INTA cycle's IORQ, about 3 clocks after /INT was sampled ([V] zint.v:117-132,
    // int_sel latched at intack_s): a frame pulse that ends in between is gone (intctr_fin)
    const uint32_t ack = t + kAcknowledgeClocks;
    CatchUp(RasterAt(ack));
    if ((_ts.intPending & TsConfInt::Frame) && !VdosFrozen() && !FramePulseActive(ack))
        _ts.intPending &= static_cast<uint8_t>(~TsConfInt::Frame);

    for (uint8_t sel = 0; sel < 4; sel++)
    {
        if (_ts.intPending & kPriority[sel].bit)
        {
            _ts.intPending &= static_cast<uint8_t>(~kPriority[sel].bit);
            _ts.intSel = sel;
            return kPriority[sel].vector;
        }
    }
    return kPriority[_ts.intSel & 0x03].vector;  // nothing left: int_sel keeps its last source (no else branch)
}

void TsConfInterrupts::OnWait(uint32_t ttBefore, uint32_t ticks)
{
    if (!(_ts.regs[TsConfReg::IntMask] & TsConfInt::Frame)) [[unlikely]]
        return;  // no frame INT to freeze
    if (VdosFrozen()) [[unlikely]]
        return;  // vdos already holds the counter; OnVdosExit moves the pulse past the whole interval

    const uint32_t multiplier = Multiplier();
    const uint32_t t0 = ttBefore >> 8;                                  // CPU clocks, the stall's start
    const uint32_t t1 = static_cast<uint32_t>((static_cast<uint64_t>(ttBefore) + ticks + 255) >> 8);  // its end

    // Events up to the stall's start are latched; is the pulse running at it?
    CatchUp(RasterAt(t0));
    // A pulse that is over is dropped here as IsIntAsserted drops it (lazily): a stale latch is not "running"
    if ((_ts.intPending & TsConfInt::Frame) && !FramePulseActive(t0))
        _ts.intPending &= static_cast<uint8_t>(~TsConfInt::Frame);
    const bool pending = (_ts.intPending & TsConfInt::Frame) != 0;
    const bool running = pending;
    const int32_t startedAt = _ts.intFrameRaster;

    // Then the events inside the stall (a pulse that started in it)
    CatchUp(RasterAt(t1));

    const uint32_t stall = t1 - t0;
    if (running)
    {
        // The counter stood still for the whole stall: the pulse starts that much later
        _ts.intFrameRaster = startedAt + static_cast<int32_t>((stall + multiplier - 1) / multiplier);
    }
    else if (!pending && (_ts.intPending & TsConfInt::Frame))
    {
        // The event fell inside the stall: its 32 clocks begin when the stall ends
        _ts.intFrameRaster = static_cast<int32_t>((t1 + multiplier - 1) / multiplier);
    }
}

void TsConfInterrupts::OnMachineFrameRollover([[maybe_unused]] uint32_t frameLength)
{
    // Finish the old frame (its last line event sits on the last tact), then
    // start the new one; a running frame pulse carries over
    CatchUp(kFrameTacts - 1);
    if (VdosFrozen())
    {
        // vdos goes on: close the frozen interval at the frame end and reopen it at the new frame's start
        ThawFramePulse(kFrameTacts * Multiplier());
        _ts.intVdosClock = 0;
    }
    _ts.intLastRaster = 0;
    _ts.intFrameRaster -= static_cast<int32_t>(kFrameTacts);

    // The instruction that crossed the frame end may have carried a stretch of the clock (a /WAIT) into the new
    // frame; Z80::t is the part of it that is left. A frame INT whose event fell inside that part has its pulse
    // start where the clock ran again (OnWait does this for a stall inside one frame): without it the first INT of
    // the frame was lost whenever the stall straddled the frame end - the instruction's own clocks never reach 32
    Core* core = _context->pCore;
    Z80* z80 = core ? core->GetZ80() : nullptr;
    if (z80 && (_ts.regs[TsConfReg::IntMask] & TsConfInt::Frame) && !VdosFrozen())
    {
        const uint32_t residue = z80->t;
        if (residue > kFramePulseClocks)
        {
            // An old pulse that is over is dropped here as IsIntAsserted drops it (lazily)
            if ((_ts.intPending & TsConfInt::Frame) && !FramePulseActive(residue))
                _ts.intPending &= static_cast<uint8_t>(~TsConfInt::Frame);
            const bool pendingBefore = (_ts.intPending & TsConfInt::Frame) != 0;
            CatchUp(RasterAt(residue));  // the new frame's events up to where the CPU is
            if (!pendingBefore && (_ts.intPending & TsConfInt::Frame) && !FramePulseActive(residue))
            {
                const uint32_t multiplier = Multiplier();
                _ts.intFrameRaster = static_cast<int32_t>((residue + multiplier - 1) / multiplier);
            }
        }
    }
}
