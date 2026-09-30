#include "evoturbooverlay.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

bool EvoTurboOverlay::WaitsApply() const
{
    return _state->hw_turbo_shift_applied == 2 && _core->IsContentionSwitchOn();
}

void EvoTurboOverlay::Read(uint16_t addr, bool opcodeFetch)
{
    // ROM is not behind the DRAM controller; an access to it drops both words
    if (_memory->IsWindowRom(static_cast<uint8_t>(addr >> 14)))
    {
        Invalidate();
        return;
    }

    const uint16_t word = static_cast<uint16_t>(addr >> 1);
    if ((_cache.codeValid && _cache.codeWord == word) || (_cache.dataValid && _cache.dataWord == word))
        return;

    // A miss: the DRAM cycle ends 2 or 3 clocks after the Z80's by the start clock's parity (research-zxevo.md A.3)
    if (WaitsApply())
        _cpu->AddWaitStates(2u + (_cpu->AccessStartClock() & 1u));

    if (opcodeFetch)
    {
        _cache.codeWord = word;
        _cache.codeValid = true;
    }
    else
    {
        _cache.dataWord = word;
        _cache.dataValid = true;
    }
}

uint8_t EvoTurboOverlay::onRead(uint16_t addr, uint8_t normal, [[maybe_unused]] bool isExecution,
                                [[maybe_unused]] bool romPaged)
{
    // Operand bytes are data reads for the DRAM controller (/M1 high): only the opcode fetch fills the code word
    Read(addr, false);
    return normal;
}

uint8_t EvoTurboOverlay::onReadM1(uint16_t addr, uint8_t normal, [[maybe_unused]] bool romPaged)
{
    Read(addr, true);
    return normal;
}

void EvoTurboOverlay::onWrite(uint16_t addr, [[maybe_unused]] uint8_t value, [[maybe_unused]] bool romPaged)
{
    if (_memory->IsWindowRom(static_cast<uint8_t>(addr >> 14)))
    {
        Invalidate();
        return;
    }

    // Writes always go to the DRAM, without a wait; a cached copy of the word is stale now
    const uint16_t word = static_cast<uint16_t>(addr >> 1);
    if (_cache.codeWord == word)
        _cache.codeValid = false;
    if (_cache.dataWord == word)
        _cache.dataValid = false;
}
