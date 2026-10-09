#include "stdafx.h"

#include "nextboard.h"

#include "emulator/io/z80n/nextinterrupts.h"
#include "emulator/memory/next/nextmemory.h"

void NextBoard::Reset(bool hard)
{
    // A soft reset keeps the machine type (and with it config mode or not); a hard one is the power-on state
    // the bare personality (no boot ROM) is a 128K machine from the start
    const uint8_t type = hard ? (_memory->HasBootRom() ? 0 : 2) : MachineType();
    for (uint8_t& r : _regs)
        r = 0;
    _regs[kRegMachineType] = type;
    // reset values of the DivMMC automap registers (esxdos-and-sd.md section 3.2)
    _regs[0xB8] = 0x83;
    _regs[0xB9] = 0x01;
    _regs[0xBA] = 0x00;
    _regs[0xBB] = 0xCD;
    if (type >= 1 && type <= 4)
        _timing = type;
    else if (hard)
        _timing = 2;
    if (_machine)
    {
        _machine->SetCpuSpeed(1);
        _machine->SetContentionDisabled(false);
        _machine->SetMachineTiming(_timing);
    }
    _selected = 0;
    _resetPending = false;
    if (_interrupts)
        _interrupts->Reset();
    const bool config = type == 0;
    _memory->SetConfigBank(0);
    _memory->SetConfigMode(config);
    // "switched on by a reset in config mode"
    _memory->SetBootRomEnabled(config && _memory->HasBootRom());
}

uint8_t NextBoard::Read(uint8_t reg) const
{
    switch (reg)
    {
        case kRegMachineId:
            return kMachineIdNext;
        case kRegCoreVersion:
            return kCoreVersion;
        case kRegCoreVersionSub:
            return kCoreVersionSub;
        case kRegResetType:
            return 0;  // power on
        case kRegMemoryMapping:
            return _machine ? _machine->ReadMemoryMapping() : _regs[reg];
        case kRegCpuSpeed:
            return static_cast<uint8_t>((_regs[reg] & 3) | ((_regs[reg] & 3) << 4));  // programmed | actual
        default:
            break;
    }
    if (reg >= kRegMmu0 && reg < kRegMmu0 + NextMemory::kSlots)
        return _memory->GetMmu(reg - kRegMmu0);
    uint8_t value;
    if (_interrupts && _interrupts->ReadNr(reg, value))
        return value;
    return _regs[reg];
}

void NextBoard::Write(uint8_t reg, uint8_t value)
{
    if (_log)
        _log->push_back({reg, value, _pc ? *_pc : uint16_t(0)});
    if (reg == kRegMachineId || reg == kRegCoreVersion || reg == kRegCoreVersionSub)
        return;  // read only
    if (reg >= kRegMmu0 && reg < kRegMmu0 + NextMemory::kSlots)
    {
        _memory->SetMmu(reg - kRegMmu0, value);
        return;
    }
    if (_interrupts && _interrupts->WriteNr(reg, value))
    {
        _regs[reg] = value;
        return;
    }
    switch (reg)
    {
        case kRegResetType:
            if (value & 0x03)
            {
                _resetPending = true;
                _resetHard = (value & 0x02) != 0;
            }
            _regs[reg] = value & 0xF8;  // bits 7:3 are held (ESP bus reset, ...)
            return;
        case kRegMachineType:
        {
            // Any write switches the boot ROM off; only config mode takes the machine type
            _memory->SetBootRomEnabled(false);
            if (_memory->InConfigMode())
            {
                _regs[reg] = value;
                if ((value & 7) != 0)
                    _memory->SetConfigMode(false);
            }
            else
                _regs[reg] = static_cast<uint8_t>((_regs[reg] & 0x07) | (value & 0xF8));
            // The timing: bits 6:4 when bit 7 allows, else the machine type chosen in config mode
            const uint8_t wanted = (value & 0x80) ? static_cast<uint8_t>((value >> 4) & 7) : static_cast<uint8_t>(_regs[reg] & 7);
            if (wanted >= 1 && wanted <= 4 && wanted != _timing)
            {
                _timing = wanted;
                if (_machine)
                    _machine->SetMachineTiming(_timing);
            }
            return;
        }
        case kRegPeripheral4:
            _regs[reg] = value;
            if ((value & 0x08) && _machine)
                _machine->ClearDivMmcMapram();
            return;
        case kRegMemoryMapping:
            if (_machine)
                _machine->WriteMemoryMapping(value);
            return;
        case kRegPeripheral2:
            _regs[reg] = value;
            if (_machine)
                _machine->SetContentionDisabled((value & 0x40) != 0);
            return;
        case kRegCpuSpeed:
            _regs[reg] = value & 0x03;
            if (_machine)
                _machine->SetCpuSpeed(static_cast<uint8_t>(1u << (value & 3)));
            return;
        case kRegConfigMapping:
            _regs[reg] = value & 0x3F;
            _memory->SetConfigBank(value);
            return;
        default:
            _regs[reg] = value;
            return;
    }
}

void NextBoard::AfterInstruction()
{
    if (!_resetPending)
        return;
    _resetPending = false;
    if (_machine)
        _machine->PerformReset(_resetHard);
}
