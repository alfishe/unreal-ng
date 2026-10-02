#include "scorpionturbooverlay.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{
    constexpr uint32_t kLineClocks = 448;    // 224 T at 7 MHz
    constexpr uint32_t kWindowClocks = 256;  // 128 T of each line
    constexpr uint32_t kWindowLines = 192;

    /// The pixel counter's phase at 7 MHz clock `e` (research-scorpion-turbo.md 4.4: tied to Even M1's parity)
    inline uint32_t Phase(uint32_t e) { return (e + 3) & 0x03; }
}  // namespace

ScorpionTurboOverlay::ScorpionTurboOverlay(Core* core, Z80* cpu, Memory* memory, const EmulatorState* state,
                                           uint32_t paperStartT, ScorpionTurboLogic logic)
    : _core(core), _cpu(cpu), _memory(memory), _state(state), _windowStart(2 * paperStartT + 1), _logic(logic)
{
}

bool ScorpionTurboOverlay::WaitsApply() const
{
    return _state->hw_turbo_ratio_applied == 2 && _core->IsContentionSwitchOn();
}

bool ScorpionTurboOverlay::InPicture(uint32_t e) const
{
    if (e < _windowStart)
        return false;
    const uint32_t d = e - _windowStart;
    return d / kLineClocks < kWindowLines && d % kLineClocks < kWindowClocks;
}

uint32_t ScorpionTurboOverlay::PictureRun(uint32_t e, bool& picture) const
{
    constexpr uint32_t kForever = 0xFFFFFFFFu;
    if (e < _windowStart)
    {
        picture = false;
        return _windowStart - e;
    }
    const uint32_t d = e - _windowStart;
    const uint32_t line = d / kLineClocks;
    const uint32_t x = d - line * kLineClocks;
    if (line >= kWindowLines)
    {
        picture = false;
        return kForever;
    }
    picture = x < kWindowClocks;
    if (picture)
        return kWindowClocks - x;
    return line + 1 < kWindowLines ? kLineClocks - x : kForever;
}

uint32_t ScorpionTurboOverlay::DataWait(uint32_t start) const
{
    // The CPU slot: an even count, and in the picture only the pair the video leaves (count 0). Closed form when
    // the picture / border state holds for the whole wait (it nearly always does): one division per access
    const uint32_t e2 = start + 1;
    const uint32_t p2 = Phase(e2);
    bool picture = false;
    const uint32_t run = PictureRun(e2, picture);
    const uint32_t wait = picture ? ((4u - p2) & 3u) : (p2 & 1u);
    if (wait < run)
        return wait;
    return DataWaitByEdges(start);  // the wait crosses the window's edge
}

uint32_t ScorpionTurboOverlay::DataWaitByEdges(uint32_t start) const
{
    const uint32_t e2 = start + 1;
    for (uint32_t j = 0;; j++)
    {
        const uint32_t p = Phase(e2 + j);
        if ((p & 1) == 0 && (p == 0 || !InPicture(e2 + j)))
            return j;
    }
}

uint32_t ScorpionTurboOverlay::OpcodeFetchWait(uint32_t start) const
{
    // At least one forced wait, released on the slot's column count so T3 samples the latched byte. Closed form
    // as in DataWait: the first odd count after e2 (border) or the first count 1 (picture)
    const uint32_t e2 = start + 1;
    const uint32_t p2 = Phase(e2);
    bool picture = false;
    const uint32_t run = PictureRun(e2, picture);
    const uint32_t wait = picture ? 1u + ((0u - p2) & 3u) : 1u + (p2 & 1u);
    if (wait < run)
        return wait;
    return OpcodeFetchWaitByEdges(start);
}

uint32_t ScorpionTurboOverlay::OpcodeFetchWaitByEdges(uint32_t start) const
{
    const uint32_t e2 = start + 1;
    for (uint32_t j = 1;; j++)
    {
        const uint32_t p = Phase(e2 + j);
        if ((p & 1) == 1 && (p == 1 || !InPicture(e2 + j)))
            return j;
    }
}

void ScorpionTurboOverlay::Wait(uint16_t addr, bool opcodeFetch)
{
    // The RAM select, not the address: RAM paged at #0000 waits, ROM never does
    if (_memory->IsWindowRom(static_cast<uint8_t>(addr >> 14)) || !WaitsApply())
        return;
    const uint32_t start = _cpu->AccessStartClock();
    // SC15.3 releases an opcode fetch on the slot like a data access (research-scorpion-turbo.md 4.2)
    const uint32_t clocks =
        opcodeFetch && _logic == ScorpionTurboLogic::SC151 ? OpcodeFetchWait(start) : DataWait(start);
    if (clocks)
        _cpu->AddWaitStates(clocks);
}

uint8_t ScorpionTurboOverlay::onRead(uint16_t addr, uint8_t normal, [[maybe_unused]] bool isExecution,
                                     [[maybe_unused]] bool romPaged)
{
    Wait(addr, false);
    return normal;
}

uint8_t ScorpionTurboOverlay::onReadM1(uint16_t addr, uint8_t normal, [[maybe_unused]] bool romPaged)
{
    Wait(addr, true);
    return normal;
}

void ScorpionTurboOverlay::onWrite(uint16_t addr, [[maybe_unused]] uint8_t value, [[maybe_unused]] bool romPaged)
{
    // No write buffer: the CPU's data goes straight into the DRAM in its slot
    Wait(addr, false);
}
