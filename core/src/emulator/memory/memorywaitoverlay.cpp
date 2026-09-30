#include "memorywaitoverlay.h"

#include "emulator/cpu/z80.h"

void MemoryWaitOverlay::Wait(MemoryWaitAccess kind, uint16_t addr)
{
    if (!_slotWaits[addr >> 14])
        return;
    const uint32_t clocks = ExtraClocks(kind, addr, _cpu->AccessStartClock());
    if (clocks)
        _cpu->AddWaitStates(clocks);
}

uint8_t MemoryWaitOverlay::onRead(uint16_t addr, uint8_t normal, bool isExecution, [[maybe_unused]] bool romPaged)
{
    Wait(isExecution ? MemoryWaitAccess::Code : MemoryWaitAccess::Read, addr);
    return normal;
}

void MemoryWaitOverlay::onWrite(uint16_t addr, [[maybe_unused]] uint8_t value, [[maybe_unused]] bool romPaged)
{
    Wait(MemoryWaitAccess::Write, addr);
}
