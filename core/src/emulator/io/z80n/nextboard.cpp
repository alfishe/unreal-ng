#include "stdafx.h"

#include "nextboard.h"

#include "emulator/memory/next/nextmemory.h"

void NextBoard::Reset()
{
    for (uint8_t& r : _regs)
        r = 0;
    _selected = 0;
    _regs[kRegMachineType] = 0x00;
}

uint8_t NextBoard::Read(uint8_t reg) const
{
    switch (reg)
    {
        case kRegMachineId:
            return kMachineIdNext;
        case kRegCoreVersion:
            return kCoreVersion;
        case kRegResetType:
            return 0;  // power on
        default:
            break;
    }
    if (reg >= kRegMmu0 && reg < kRegMmu0 + NextMemory::kSlots)
        return _memory->GetMmu(reg - kRegMmu0);
    return _regs[reg];
}

void NextBoard::Write(uint8_t reg, uint8_t value)
{
    if (reg == kRegMachineId || reg == kRegCoreVersion)
        return;  // read only
    if (reg >= kRegMmu0 && reg < kRegMmu0 + NextMemory::kSlots)
    {
        _memory->SetMmu(reg - kRegMmu0, value);
        return;
    }
    _regs[reg] = value;
}
