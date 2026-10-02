// eve-emu - clocks, scan, frame events, swap, INT_N (spec §4, §5, arch §8.3).
#include "eve-internal.h"

#include <algorithm>

namespace EveLib
{

namespace
{

constexpr uint32_t kIntSwap = 0x01;

uint32_t FrameLines(const EveChip& chip)
{
    return RegGet(chip, Reg::Vcycle);
}

// The line at whose end the frame event happens (spec §5.3, V9).
uint32_t FrameEventLine(const EveChip& chip)
{
    const uint32_t vcycle = FrameLines(chip);
    if (!kFrameEventAtEndOfVisible)
        return vcycle - 1;
    uint32_t last = RegGet(chip, Reg::Voffset) + RegGet(chip, Reg::Vsize);
    last = std::max<uint32_t>(last, 1);
    last = std::min(last, vcycle);
    return last - 1;
}

void FrameEvent(EveChip& chip)
{
    ScanState& scan = chip.state.scan;
    CatchUp(chip);
    if (scan.dlswapPending == 2)
        ApplySwap(chip);
    ++scan.frames;
    RegSet(chip, Reg::Frames, static_cast<uint32_t>(scan.frames));
    ++scan.completedFrames;
}

void EndOfLine(EveChip& chip)
{
    ScanState& scan = chip.state.scan;
    if (scan.dlswapPending == 1)
        ApplySwap(chip);
    if (scan.line == FrameEventLine(chip))
        FrameEvent(chip);
    if (++scan.line >= FrameLines(chip))
    {
        scan.line = 0;
        FrameStart(chip);
    }
}

void AdvanceScan(EveChip& chip, uint64_t clocks)
{
    const uint32_t lineClocks = LineClocks(chip);
    if (lineClocks == 0 || FrameLines(chip) == 0)
        return;
    ScanState& scan = chip.state.scan;
    uint64_t position = scan.lineClock + clocks;
    while (position >= lineClocks)
    {
        position -= lineClocks;
        scan.lineClock = 0;
        EndOfLine(chip);
    }
    scan.lineClock = static_cast<uint32_t>(position);
}

uint64_t ScanClocksToNextEvent(const EveChip& chip)
{
    if (!ScanRunning(chip))
        return UINT64_MAX;
    const ScanState& scan = chip.state.scan;
    const uint64_t lineClocks = LineClocks(chip);
    const uint64_t restOfLine = lineClocks - scan.lineClock;
    if (scan.dlswapPending == 1)
        return restOfLine;
    const uint32_t eventLine = FrameEventLine(chip);
    const uint32_t vcycle = FrameLines(chip);
    const uint64_t linesAhead = scan.line <= eventLine ? eventLine - scan.line : vcycle - scan.line + eventLine;
    return linesAhead * lineClocks + restOfLine;
}

uint64_t NextEvent(const EveChip& chip)
{
    if (!ClockRunning(chip))
        return UINT64_MAX;
    uint64_t next = ScanClocksToNextEvent(chip);
    next = std::min(next, CoproClocksToNextEvent(chip));
    next = std::min(next, AudioClocksToNextEvent(chip));
    return next;
}

} // namespace

bool ClockRunning(const EveChip& chip)
{
    return chip.state.power.mode == PowerMode::Active;
}

uint32_t LineClocks(const EveChip& chip)
{
    return RegGet(chip, Reg::Hcycle) * RegGet(chip, Reg::Pclk);
}

bool ScanRunning(const EveChip& chip)
{
    return ClockRunning(chip) && LineClocks(chip) != 0 && FrameLines(chip) != 0;
}

void Advance(EveChip& chip, uint64_t clocks)
{
    if (!ClockRunning(chip))
        return;
    while (clocks > 0)
    {
        const uint64_t step = std::max<uint64_t>(1, std::min(clocks, NextEvent(chip)));
        chip.state.totalClocks += step;
        chip.state.scan.clocksSinceReset += step;
        AdvanceScan(chip, step);
        AudioAdvance(chip, step);
        CoproRun(chip, step);
        clocks -= step;
    }
}

uint64_t ClocksToNextEvent(const EveChip& chip)
{
    return NextEvent(chip);
}

void RaiseInterrupt(EveChip& chip, uint32_t bits)
{
    RegSet(chip, Reg::IntFlags, RegGet(chip, Reg::IntFlags) | bits);
}

bool IntAsserted(const EveChip& chip)
{
    return (RegGet(chip, Reg::IntEn) & 1) != 0 &&
           (RegGet(chip, Reg::IntFlags) & RegGet(chip, Reg::IntMask)) != 0;
}

void RequestSwap(EveChip& chip, uint32_t mode)
{
    chip.state.scan.dlswapPending = static_cast<uint8_t>(mode);
    // With no picture being scanned the swap happens at once (spec §5.3).
    if (!ScanRunning(chip))
        ApplySwap(chip);
}

void ApplySwap(EveChip& chip)
{
    ScanState& scan = chip.state.scan;
    if (kSwapCopiesList)
    {
        Region& active = ActiveDlRegion(chip);
        const Region& pending = PendingDlRegion(chip);
        if (std::memcmp(active.base, pending.base, kRamDlSize) != 0)
        {
            std::memcpy(active.base, pending.base, kRamDlSize);
            active.MarkDirtyRange(0, kRamDlSize);
        }
    }
    else
    {
        scan.activeDl ^= 1;
    }
    scan.dlswapPending = 0;
    RegSet(chip, Reg::Dlswap, 0);
    RaiseInterrupt(chip, kIntSwap);
    DisplayListSwapped(chip);
    CoproSwapDone(chip);
}

void TimingChanged(EveChip& chip)
{
    ScanState& scan = chip.state.scan;
    const uint32_t lineClocks = LineClocks(chip);
    const uint32_t vcycle = FrameLines(chip);
    if (lineClocks != 0 && scan.lineClock >= lineClocks)
        scan.lineClock = lineClocks - 1;
    if (vcycle != 0 && scan.line >= vcycle)
        scan.line = vcycle - 1;
    if (scan.dlswapPending != 0 && !ScanRunning(chip))
        ApplySwap(chip);
}

void GetTiming(const EveChip& chip, EveTiming& out)
{
    auto get16 = [&chip](Reg reg) { return static_cast<uint16_t>(RegGet(chip, reg)); };
    out.hcycle = get16(Reg::Hcycle);
    out.hoffset = get16(Reg::Hoffset);
    out.hsize = get16(Reg::Hsize);
    out.hsync0 = get16(Reg::Hsync0);
    out.hsync1 = get16(Reg::Hsync1);
    out.vcycle = get16(Reg::Vcycle);
    out.voffset = get16(Reg::Voffset);
    out.vsize = get16(Reg::Vsize);
    out.vsync0 = get16(Reg::Vsync0);
    out.vsync1 = get16(Reg::Vsync1);
    out.pclkDivider = get16(Reg::Pclk);
    const uint32_t hz = ClockRunning(chip) ? chip.state.power.systemClockHz : 0;
    out.pixelClockHz = out.pclkDivider != 0 ? hz / out.pclkDivider : 0;
    out.framePeriodClocks = static_cast<uint64_t>(out.hcycle) * out.vcycle * out.pclkDivider;
}

} // namespace EveLib

using namespace EveLib;

extern "C" {

void EveAdvance(EveChip* chip, uint64_t systemClocks)
{
    Advance(*chip, systemClocks);
}

uint64_t EveClocksToNextEvent(const EveChip* chip)
{
    return ClocksToNextEvent(*chip);
}

uint32_t EveSystemClockHz(const EveChip* chip)
{
    return ClockRunning(*chip) ? chip->state.power.systemClockHz : 0;
}

uint64_t EveTotalClocks(const EveChip* chip)
{
    return chip->state.totalClocks;
}

int EveIntAsserted(const EveChip* chip)
{
    return IntAsserted(*chip) ? 1 : 0;
}

void EveGetTiming(const EveChip* chip, EveTiming* out)
{
    GetTiming(*chip, *out);
}

uint64_t EveCompletedFrames(const EveChip* chip)
{
    return chip->state.scan.completedFrames;
}

} // extern "C"
