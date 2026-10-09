#include "stdafx.h"

#include "nextboard.h"

#include "emulator/io/z80n/nextinterrupts.h"
#include "emulator/io/z80n/nextregtable.h"

#include <cstdio>
#include "emulator/memory/next/nextmemory.h"

void NextBoard::Reset(bool hard)
{
    // A soft reset keeps the machine type (and with it config mode or not); a hard one is the power-on state
    // the bare personality (no boot ROM) is a 128K machine from the start
    const uint8_t type = hard ? (_memory->HasBootRom() ? 0 : 2) : MachineType();
    if (hard)
    {
        for (uint8_t& r : _regs)
            r = 0;
        // the power-on settings of zxnext.vhd: joystick 1 Kempston + scandoubler, the hotkeys, the internal speaker, mouse DPI
        _regs[0x05] = 0x41;
        _regs[0x06] = 0xA0;
        _regs[0x08] = 0x10;
        _regs[0x0A] = 0x01;
        _regs[0x10] = 0x01;  // core id 1 (zxnext.vhd nr_10_coreid)
    }
    else
    {
        // a soft reset keeps what the software and the firmware set (peripheral settings, ...) and resets the fields the
        // VHDL's reset block names
        _regs[0x06] |= 0xA0;
        _regs[0x08] &= static_cast<uint8_t>(~0x40);
        _regs[0x09] &= static_cast<uint8_t>(~0x10);
    }
    _regs[kRegMachineType] = type;
    // the whole-byte reset values of the table (registers.txt); the registers with devices behind them reset those on their own
    size_t count;
    for (const NextRegInfo* r = NextRegTable(count); r != NextRegTable(count) + count; r++)
        if (r->hasReset && r->number != kRegMachineType)
            _regs[r->number] = r->reset;
    // NR #8C: a soft reset copies the after-reset bits 3:0 into 7:4
    const uint8_t alt = _memory->AltRomRegister();
    _memory->SetAltRomRegister(hard ? 0 : static_cast<uint8_t>((alt << 4) | (alt & 0x0F)));
    _memory->SetMachineType(type == 0 ? 2 : type);
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
    _video.Reset();
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
        case kRegAltRom:
            return _memory->AltRomRegister();
        case kRegMemoryMapping:
            return _machine ? _machine->ReadMemoryMapping() : _regs[reg];
        case kRegCpuSpeed:
            return static_cast<uint8_t>((_regs[reg] & 3) | ((_regs[reg] & 3) << 4));  // programmed | actual
        default:
            break;
    }
    if (reg >= kRegMmu0 && reg < kRegMmu0 + NextMemory::kSlots)
        return _memory->GetMmu(reg - kRegMmu0);
    switch (reg)
    {
        case 0x18: case 0x19: case 0x1A: case 0x1B:
            return _video.ReadClip(reg - 0x18);
        case 0x1C:
            return _video.ReadClipControl();
        case 0x40:
            return _video.PaletteIndex();
        case 0x41:
            return _video.ReadPaletteValue8();
        case 0x42:
            return _video.UlaNextFormat();
        case 0x43:
            return _video.PaletteControl();
        case 0x44:
            return _video.ReadPaletteValue9();
        case 0x1E:
            return _interrupts ? static_cast<uint8_t>(_interrupts->CurrentLine() >> 8) : 0;
        case 0x1F:
            return _interrupts ? static_cast<uint8_t>(_interrupts->CurrentLine() & 0xFF) : 0;
        case 0x10:
            return static_cast<uint8_t>((_regs[reg] & 0x1F) << 2);  // the core id; the DRIVE / M1 buttons read 0
        case 0x08:
            return static_cast<uint8_t>(_regs[reg] | 0x80);  // bit 7: port #7FFD is not locked
        default:
            break;
    }
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
        case 0x18: case 0x19: case 0x1A: case 0x1B:
            _video.WriteClip(reg - 0x18, value);
            return;
        case 0x1C:
            _video.WriteClipControl(value);
            return;
        case 0x10:
            if (_memory->InConfigMode())  // the core id is writable in config mode only
                _regs[reg] = value & 0x1F;
            return;
        case 0x40:
            _video.WritePaletteIndex(value);
            return;
        case 0x41:
            _video.WritePaletteValue8(value);
            return;
        case 0x42:
            _video.WriteUlaNextFormat(value);
            return;
        case 0x43:
            _video.WritePaletteControl(value);
            return;
        case 0x44:
            _video.WritePaletteValue9(value);
            return;
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
            _memory->SetMachineType(MachineType() == 0 ? 2 : MachineType());
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
        case kRegAltRom:
            _memory->SetAltRomRegister(value);
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

std::string NextBoard::DescribeRegisters() const
{
    std::string out;
    size_t count;
    const NextRegInfo* table = NextRegTable(count);
    char line[160];
    for (size_t i = 0; i < count; i++)
    {
        const NextRegInfo& r = table[i];
        std::snprintf(line, sizeof line, "NR %02X %s%s %-34.34s value %02X%s\n", r.number, r.readable ? "R" : "-", r.writable ? "W" : "-", r.name,
                      r.readable ? Read(r.number) : 0, "");
        out += line;
    }
    return out;
}

void NextBoard::SetMachineType(uint8_t type)
{
    _regs[kRegMachineType] = static_cast<uint8_t>((_regs[kRegMachineType] & 0xF8) | (type & 7));
    _memory->SetMachineType(type == 0 ? 2 : type);
}
