#include "tsconfinterrupts.h"

#include <algorithm>

#include "emulator/emulatorcontext.h"
#include "emulator/platforms/tsconf/tsconfstate.h"

void TsConfInterrupts::Reset()
{
    _ts.intPending = 0;
    _ts.intLastRaster = 0;
    _ts.intFrameRaster = 0;
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

    // Line: tact 224 n - 1 of every line, i.e. (tact + 1) % 224 == 0
    if (_lineSource) [[unlikely]]
        CatchUpLineWithSource(from, raster, (mask & TsConfInt::Line) != 0);
    else if ((mask & TsConfInt::Line) && (raster + 1) / kLineTacts > from / kLineTacts)
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

    // The line starts of the lines the source does not drive
    for (uint32_t line = from / kLineTacts; line <= raster / kLineTacts; line++)
    {
        const uint32_t event = line * kLineTacts + kLineTacts - 1;
        if (event >= from && event <= raster && !_lineSource->DrivesLine(line))
            _ts.intPending |= TsConfInt::Line;
    }
}

bool TsConfInterrupts::FramePulseActive(uint32_t t) const
{
    const int64_t start = static_cast<int64_t>(_ts.intFrameRaster) * Multiplier();
    return static_cast<int64_t>(t) - start < static_cast<int64_t>(kFramePulseClocks);
}

bool TsConfInterrupts::IsIntAsserted(uint32_t t)
{
    CatchUp(RasterAt(t));

    // The frame pulse ends by itself after 32 CPU clocks
    if ((_ts.intPending & TsConfInt::Frame) && !FramePulseActive(t))
        _ts.intPending &= static_cast<uint8_t>(~TsConfInt::Frame);

    // vdos gates the output; the latches keep their events (§5)
    return _ts.intPending != 0 && !_ts.vdos;
}

uint8_t TsConfInterrupts::AcknowledgeInterrupt([[maybe_unused]] uint32_t t)
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

    for (const auto& source : kPriority)
    {
        if (_ts.intPending & source.bit)
        {
            _ts.intPending &= static_cast<uint8_t>(~source.bit);
            return source.vector;
        }
    }
    return 0xFF;  // nothing latched any more: the bus floats
}

void TsConfInterrupts::OnMachineFrameRollover([[maybe_unused]] uint32_t frameLength)
{
    // Finish the old frame (its last line event sits on the last tact), then
    // start the new one; a running frame pulse carries over
    CatchUp(kFrameTacts - 1);
    _ts.intLastRaster = 0;
    _ts.intFrameRaster -= static_cast<int32_t>(kFrameTacts);
}
