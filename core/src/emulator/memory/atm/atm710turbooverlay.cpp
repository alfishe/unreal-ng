#include "atm710turbooverlay.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"

Atm710TurboOverlay::Atm710TurboOverlay(Core* core, Z80* cpu, Memory* memory, const EmulatorState* state)
    : _core(core), _cpu(cpu), _memory(memory), _state(state)
{
}

bool Atm710TurboOverlay::WaitsApply() const
{
    return _state->hw_turbo_ratio_applied == 2 && _core->IsContentionSwitchOn();
}

void Atm710TurboOverlay::Wait(uint16_t addr)
{
    // The RAM select (RAMCS'), not the address: RAM paged at #0000 waits, ROM never does
    if (_memory->IsWindowRom(static_cast<uint8_t>(addr >> 14)) || !WaitsApply())
        return;
    _cpu->AddWaitStates(RamWait(_cpu->AccessStartClock()));
}

uint8_t Atm710TurboOverlay::onRead(uint16_t addr, uint8_t normal, [[maybe_unused]] bool isExecution,
                                   [[maybe_unused]] bool romPaged)
{
    Wait(addr);
    return normal;
}

uint8_t Atm710TurboOverlay::onReadM1(uint16_t addr, uint8_t normal, [[maybe_unused]] bool romPaged)
{
    Wait(addr);
    return normal;
}

void Atm710TurboOverlay::onWrite(uint16_t addr, [[maybe_unused]] uint8_t value, [[maybe_unused]] bool romPaged)
{
    Wait(addr);
}
