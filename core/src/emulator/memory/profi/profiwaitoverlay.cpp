#include "profiwaitoverlay.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/ports/models/profiboard.h"

namespace
{
    constexpr int32_t kLineT = 224;        // T per line at 3.5 MHz
    constexpr int32_t kFetchWindowT = 128; // the paper fetch window of a line
    constexpr int32_t kPaperLines = 192;
}  // namespace

ProfiWaitOverlay::ProfiWaitOverlay(Core* core, Z80* cpu, Memory* memory, const EmulatorState* state,
                                   const Setup& setup)
    : _core(core), _cpu(cpu), _memory(memory), _state(state), _setup(setup)
{
}

bool ProfiWaitOverlay::WaitsApply() const
{
    if (!_core->IsContentionSwitchOn())
        return false;
    if (_state->pDFFD & 0x80)
    {
        // Hi-res: the v5 arbiter on its third crystal, SB8 or not (design-hires.md H3); the v3 only in turbo (6 MHz)
        return _setup.v5 || _state->hw_turbo_ratio_applied == ProfiHiresClockNum(false, 0, true);
    }
    if (_state->hw_turbo_ratio_applied == 2)
        return true;
    // 3.5 MHz: only the v5 board's video WAIT, in the Spectrum raster, with SB8 in its PROFI3+ position
    return _setup.v5 && !_setup.pentagonJumper;
}

bool ProfiWaitOverlay::DistanceToRequestNs(double tNs, double& d) const
{
    const double rel0 = tNs - ProfiHiresWindowStartNs(0);
    if (_state->p7FFD & 0x20)
    {
        // #7FFD bit 5 (BCMR) in hi-res: NET00194 keeps the requests running the whole line, every line, on one phase
        const double k = rel0 / kProfiHiresRequestNs;
        const double nearest = static_cast<double>(static_cast<int64_t>(k + (k >= 0 ? 0.5 : -0.5)));
        d = rel0 - nearest * kProfiHiresRequestNs;
        return true;
    }
    // Only in the fetch window: 32 ticks, 64 requests, of each of the 240 paper lines. An access just before a
    // window's first request still counts, so the line is found with a margin
    const double shifted = rel0 + 200.0;
    if (shifted < 0)
        return false;
    const uint32_t line = static_cast<uint32_t>(shifted / kProfiLineNs);
    if (line >= kProfiHiresPaperLines)
        return false;
    const double rel = tNs - ProfiHiresWindowStartNs(line);
    double k = rel / kProfiHiresRequestNs;
    k = k < 0 ? 0 : (k > 2 * kProfiHiresTicksPerWindow - 1 ? 2 * kProfiHiresTicksPerWindow - 1 : k + 0.5);
    const double edge = static_cast<double>(static_cast<uint32_t>(k)) * kProfiHiresRequestNs;
    d = rel - edge;
    return true;
}

uint32_t ProfiWaitOverlay::HiresExtraClocks(bool rom, uint32_t start) const
{
    const uint32_t num = _state->hw_turbo_ratio_applied;
    if (!_setup.v5)
    {
        // v3 turbo (6 MHz): the Spectrum-mode slot rule in 6 MHz clocks (research-profi-hires-timing.md, M)
        if (rom)
            return 0;
        return (start & 1) ? 3u : 2u;
    }

    // v5.06 model (research-profi-hires-timing.md 3, M): around each video request edge
    const bool turbo = num >= _setup.zq3MHz;
    if (rom)
    {
        // The ROM one-shot (200 ns): 1 T at 5-6 MHz, none at 4 MHz; 2 / 1 in turbo
        const bool slow = _setup.zq3MHz <= 16;
        return turbo ? (slow ? 1u : 2u) : (slow ? 0u : 1u);
    }
    const double tNs = static_cast<double>(start) * ProfiHiresCpuPeriodNs(num);
    double d = 0;
    const bool request = DistanceToRequestNs(tNs, d);
    if (!turbo)
        return (request && d >= -130.0 && d <= 40.0) ? 1u : 0u;
    // Turbo: the DRAM ring restarts on every CPU request (1 T), and a request edge close by costs 1-2 more
    if (request && d >= -90.0 && d < 0.0)
        return 3;
    if (request && d >= 0.0 && d <= 90.0)
        return 2;
    return 1;
}

bool ProfiWaitOverlay::InFetchWindow(uint32_t t) const
{
    const int32_t d = static_cast<int32_t>(t) - static_cast<int32_t>(_setup.paperStartT);
    if (d < 0)
        return false;
    return d / kLineT < kPaperLines && d % kLineT < kFetchWindowT;
}

uint32_t ProfiWaitOverlay::V5RamWait(uint32_t t) const
{
    // research-profi-v5-wait.md section 2: d = c - P0, L = floor((d + 1) / 224), q = d - 224 L; one wait on the
    // even T of the window (phase 0), on the odd T one earlier (phases 2 and 3), none on phase 1
    if (_setup.phase == 1)
        return 0;
    const int32_t d = static_cast<int32_t>(t) - static_cast<int32_t>(_setup.paperStartT);
    if (d + 1 < 0)
        return 0;
    const int32_t line = (d + 1) / kLineT;
    if (line >= kPaperLines)
        return 0;
    const int32_t q = d - kLineT * line;
    if (_setup.phase == 0)
        return (q >= 0 && q <= 126 && (q & 1) == 0) ? 1u : 0u;
    return (q >= -1 && q <= 125 && (q & 1) != 0) ? 1u : 0u;
}

uint32_t ProfiWaitOverlay::ExtraClocks(bool rom, uint32_t start) const
{
    // `start` counts CPU clocks of the scaled frame (Z80::t: the frame is config.frame x the clock multiplier, the
    // host speed control included). The rules count the board's own clocks: strip the host speed part
    const uint32_t ratio = _state->hw_turbo_ratio_applied ? _state->hw_turbo_ratio_applied : 1;
    const uint32_t multiplier = _state->current_z80_frequency_multiplier ? _state->current_z80_frequency_multiplier : 1;
    const uint32_t host = multiplier > ratio ? multiplier / ratio : 1;
    start /= host;
    if (_state->pDFFD & 0x80)
        return HiresExtraClocks(rom, start);
    const bool turbo = ratio == 2;
    if (!_setup.v5)
    {
        // v3: the CPU waits for its DRAM slot in turbo only (research-profi-v3-turbo-floatbus.md A4)
        if (!turbo || rom)
            return 0;
        return (start & 1) ? 3u : 2u;
    }
    if (turbo)
    {
        if (rom)
            return 1;
        return InFetchWindow(start / 2) ? 2u : 1u;
    }
    if (rom)
        return _setup.romWait ? 1u : 0u;
    return V5RamWait(start);
}

void ProfiWaitOverlay::Wait(uint16_t addr, bool write)
{
    if (!WaitsApply())
        return;
    // The RAM select, not the address: RAM paged at #0000 waits like any other page
    const bool rom = _memory->IsWindowRom(static_cast<uint8_t>(addr >> 14));
    if (rom && write)
        return;   // the one-shot fires on ROM reads only
    const uint32_t clocks = ExtraClocks(rom, _cpu->AccessStartClock());
    if (clocks)
        _cpu->AddWaitStates(clocks);
}

uint8_t ProfiWaitOverlay::onRead(uint16_t addr, uint8_t normal, [[maybe_unused]] bool isExecution,
                                 [[maybe_unused]] bool romPaged)
{
    Wait(addr, false);
    return normal;
}

uint8_t ProfiWaitOverlay::onReadM1(uint16_t addr, uint8_t normal, [[maybe_unused]] bool romPaged)
{
    Wait(addr, false);
    return normal;
}

void ProfiWaitOverlay::onWrite(uint16_t addr, [[maybe_unused]] uint8_t value, [[maybe_unused]] bool romPaged)
{
    Wait(addr, true);
}
