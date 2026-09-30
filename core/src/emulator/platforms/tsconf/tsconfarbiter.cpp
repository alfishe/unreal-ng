#include "tsconfarbiter.h"

#include "emulator/platforms/tsconf/tsconfengine.h"
#include "emulator/platforms/tsconf/tsconfgeometry.h"

TsConfArbiter::Fetch TsConfArbiter::FetchOf(const TsConfLine& set, uint32_t line)
{
    // Per mode: ZX, 16C, 256C, TXT (video_mode.v:85-88, 115, 128-133)
    static constexpr uint8_t kGoOffset[4] = {18, 6, 4, 10};
    static constexpr uint8_t kLength[4] = {8, 4, 2, 8};
    static constexpr uint8_t kNeed[4] = {1, 1, 1, 4};

    Fetch f;
    const uint8_t mode = set.vConfig & 0x03;
    f.active = TsConfGeometry::LineInWindow(set.vConfig, line) && !(set.vConfig & 0x20);
    if (!f.active)
        return f;
    const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(set.vConfig);
    const uint32_t xOffset = (mode == 2) ? (set.gxOffs & 1u) : (set.gxOffs & 3u);  // x_offs_mode[1:0]
    f.h0 = static_cast<uint16_t>(win.x0 - kGoOffset[mode] - xOffset);
    f.h1 = static_cast<uint16_t>(win.x0 + win.w - kGoOffset[mode] - xOffset + 4);
    f.length = kLength[mode];
    f.need = kNeed[mode];
    return f;
}

void TsConfArbiter::Reset()
{
    _cycle = 0;
    _blkRem = 0;
    _vidRem = 0;
    _charged = false;
}

void TsConfArbiter::Step(bool cpuRequest)
{
    if (_blkRem == 0)
    {
        const uint32_t line = _cycle / kLineCycles;
        const uint32_t h = _cycle % kLineCycles;
        const Fetch f = FetchOf(_engine.Line(line), line);
        if (f.active && h >= f.h0 && h < f.h1)
        {
            _blkRem = static_cast<uint8_t>(f.length - 1);
            _vidRem = static_cast<uint8_t>(cpuRequest ? f.need : f.need - 1);
        }
    }
    else
    {
        if (_vidRem == _blkRem)
            _vidRem--;  // video, the CPU refused
        else if (!cpuRequest && _vidRem)
            _vidRem--;  // video
        _blkRem--;
    }
    _cycle++;
}

void TsConfArbiter::SeekClean(uint32_t cycle)
{
    _cycle = cycle;
    _blkRem = 0;
    _vidRem = 0;
    _charged = false;

    const uint32_t line = cycle / kLineCycles;
    const uint32_t h = cycle % kLineCycles;
    const Fetch f = FetchOf(_engine.Line(line), line);
    if (!f.active || h <= f.h0)
        return;
    // The block decided last at or before `cycle` (blocks start at h0 + k*length below h1)
    uint32_t k = (h - f.h0) / f.length;
    const uint32_t lastK = (f.h1 - 1u - f.h0) / f.length;
    if (k > lastK)
        k = lastK;
    const uint32_t offset = h - (f.h0 + k * f.length);
    if (offset == 0 || offset >= f.length)
        return;  // `cycle` decides a block, or the blocks are over
    _blkRem = static_cast<uint8_t>(f.length - offset);
    _vidRem = static_cast<uint8_t>(f.need > offset ? f.need - offset : 0);
}

uint32_t TsConfArbiter::CpuAccess(uint32_t request, Access kind)
{
    uint32_t d0 = request >> 2;
    if (d0 < _cycle || d0 - _cycle > 4 * kLineCycles)
        SeekClean(d0);  // a new frame, or far away: nothing carries over

    // Cycles since the last access: the CPU clock stopped in those with
    // cpu_next = 0 (it was not in a read). Once a block boundary passes with
    // no request, cpu_next stays 1 until the next request
    uint32_t frozen = 0;
    while (_cycle < d0)
    {
        if (!_charged && !CpuNext())
        {
            frozen += 4;
            request += 4;
            d0 = request >> 2;
        }
        _charged = false;
        const bool boundary = _blkRem == 0;
        Step(false);
        if (boundary && _cycle < d0)
        {
            SeekClean(d0);
            break;
        }
    }

    // Video owns every cycle the CPU is refused; the grant is at c3 of G
    while (!CpuNext())
        Step(true);
    const uint32_t grant = _cycle;
    Step(true);  // the CPU owns grant + 1
    _charged = true;

    if (kind != Access::Write)
        return frozen + (4u * grant + 4u - request) + (kind == Access::M1 ? 2u : 3u);

    // A write stops the clock only in cycles with cpu_next = 0: the refused
    // ones before the grant and, possibly, its own
    uint32_t wait = (grant == d0) ? 0u : 4u * grant - request;
    if (!CpuNext())
        wait += 4;
    return frozen + wait;
}
