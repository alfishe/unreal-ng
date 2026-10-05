#include "tsconfarbiter.h"

#include <algorithm>

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
    _refused.open = false;
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
    // The refused cycles still open up to this access's request (its T2 falling edge, the edge of T1 + 3): from
    // the request on the request's own stall (stall14_ini / stall14_cycrd) covers them, as the walk below counts
    uint32_t stopped = 0;
    uint32_t tw = 0;
    if (_refused.open && ToWindow(request - 3u, tw))
    {
        if (kind == Access::Write)
            Advance(1, 0, tw + 3u);
        else
            Advance(tw + 2u, tw + 5u, tw + 3u);
        stopped = Charge(tw + 3u);
        request += stopped;
        _refused.open = false;
    }

    uint32_t d0 = request >> 2;
    if (d0 < _cycle || d0 - _cycle > 4 * kLineCycles)
        SeekClean(d0);  // a new frame, or far away: nothing carries over

    // Cycles since the last access (the CPU clock stops of their refused ones are charged above). Once a block
    // boundary passes with no request, cpu_next stays 1 until the next request
    while (_cycle < d0)
    {
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

    uint32_t wait;
    if (kind != Access::Write)
        wait = (4u * grant + 4u - request) + (kind == Access::M1 ? 2u : 3u);
    else
        wait = (grant == d0) ? 0u : 4u * grant - request;  // a write stops only in refused cycles before the grant

    // T1 as this machine cycle's edges after the wait see it
    const uint32_t t1 = request - 3u + wait;
    return stopped + wait + OpenRefused(t1, kind);
}

/// region <Refused cycles>

uint32_t TsConfArbiter::OpenRefused(uint32_t t1, Access kind)
{
    if (CpuNext())
        return 0;
    // _cycle = G + 1, the CPU's own cycle: cpu_next is 0 in it and every cycle to the block's end. The clock
    // edges of this machine cycle up to its request already came (T3's rising edge is the next, at the end of
    // T1 + 4), so the simulation starts there at the earliest
    Refused& r = _refused;
    r.open = true;
    r.stopped = false;
    r.runs = 0;
    r.shift = 0;
    r.end = 4u * (_cycle + _blkRem);
    r.period = std::max(4u * _cycle, t1 + 4u);
    r.edge = r.period;
    return SettleCycle(t1, kind);
}

bool TsConfArbiter::ToWindow(uint32_t t1, uint32_t& tw)
{
    Refused& r = _refused;
    // A machine cycle starts at most a few fclk before the simulated period; anything else is a new frame or
    // a clock switch that did not reset (never in the RTL's terms: the window is over)
    if (t1 < r.shift || t1 - r.shift + 64u < r.period || t1 - r.shift > r.period + 4u * kLineCycles)
    {
        r.open = false;
        return false;
    }
    tw = t1 - r.shift;
    return true;
}

uint32_t TsConfArbiter::SettleCycle(uint32_t t1, Access kind)
{
    uint32_t tw;
    if (!ToWindow(t1, tw))
        return 0;
    // The FPGA sees MREQ + RD from T1 + 2 (T1's falling edge) up to T3's rising edge (M1) or falling edge (read).
    // The next machine cycle starts at T1 + 8 (M1) / T1 + 6 at the earliest, so its read is not seen before
    // its T1 + 2: the read state is known up to that edge, and this cycle owns the edges up to the next T1
    switch (kind)
    {
        case Access::M1:
            Advance(tw + 2u, tw + 4u, tw + 9u);
            return Charge(tw + 8u);
        case Access::Read:
            Advance(tw + 2u, tw + 5u, tw + 7u);
            return Charge(tw + 6u);
        default:
            Advance(1, 0, tw + 7u);
            return Charge(tw + 6u);
    }
}

uint32_t TsConfArbiter::Settle(uint32_t t1, Access kind)
{
    return _refused.open ? SettleCycle(t1, kind) : 0;
}

uint32_t TsConfArbiter::SettleBeforeM1(uint32_t t1)
{
    uint32_t tw;
    if (!_refused.open || !ToWindow(t1, tw))
        return 0;
    Advance(1, 0, tw + 1u);  // no read seen up to this M1's T1 falling edge
    return Charge(tw);       // the edges up to its T1 rising edge are the previous cycles'
}

/// The Z80 clock in the refused cycles, fclk by fclk (zmem.v stall14_cyc = memrd ? stall14_cycrd : !cpu_next;
/// zclock.v registers zpos / zneg, so a stopped fclk suppresses the clock edge at the end of the next one). In
/// fclk `period` the FPGA sees the pins as the edges up to the end of period - 1 left them (pin delay 1): with
/// `edge` the next edge to come, a memory read is seen when readFrom <= edge <= readTo
void TsConfArbiter::Advance(uint32_t readFrom, uint32_t readTo, uint32_t known)
{
    Refused& r = _refused;
    while (r.period < r.end || r.stopped)
    {
        bool stop = false;
        if (r.period < r.end)
        {
            if (r.edge > known)
                return;  // the next machine cycle decides
            stop = r.edge < readFrom || r.edge > readTo;
        }
        if (!r.stopped)
            r.edge++;  // the edge at the end of this fclk still comes
        if (stop)
        {
            // Stopped twice in a row nothing moves any more: stopped to the end of the refused cycles
            const uint32_t n = r.stopped ? r.end - r.period : 1u;
            if (r.runs && r.runEdge[r.runs - 1] == r.edge)
                r.runFclk[r.runs - 1] += n;
            else if (r.runs < kMaxRuns)
            {
                r.runEdge[r.runs] = r.edge;
                r.runFclk[r.runs] = n;
                r.runs++;
            }
            else
                r.runFclk[r.runs - 1] += n;
            r.period += n - 1u;
        }
        r.stopped = stop;
        r.period++;
    }
}

uint32_t TsConfArbiter::Charge(uint32_t limit)
{
    Refused& r = _refused;
    uint32_t fclks = 0;
    uint8_t charged = 0;
    while (charged < r.runs && r.runEdge[charged] <= limit)
        fclks += r.runFclk[charged++];
    for (uint8_t i = charged; i < r.runs; i++)
    {
        r.runEdge[i - charged] = r.runEdge[i];
        r.runFclk[i - charged] = r.runFclk[i];
    }
    r.runs = static_cast<uint8_t>(r.runs - charged);
    r.shift += fclks;
    if (!r.runs && r.period >= r.end && !r.stopped)
        r.open = false;  // over and everything charged
    return fclks;
}

/// endregion </Refused cycles>
