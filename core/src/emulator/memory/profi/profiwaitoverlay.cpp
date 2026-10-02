#include "profiwaitoverlay.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

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
    if (_state->hw_turbo_ratio_applied == 2)
        return !(_setup.v5 && (_state->pDFFD & 0x80));   // v5 hi-res runs from its third crystal: not modeled
    // 3.5 MHz: only the v5 board's video WAIT, in the Spectrum raster, with SB8 in its PROFI3+ position
    return _setup.v5 && !_setup.pentagonJumper && !(_state->pDFFD & 0x80);
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
