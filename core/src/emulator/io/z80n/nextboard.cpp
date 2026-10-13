#include "stdafx.h"

#include "nextboard.h"

#include "emulator/io/z80n/nextinterrupts.h"
#include "emulator/io/z80n/nextregtable.h"

#include <cstdio>
#include "emulator/memory/next/nextmemory.h"

namespace
{
/// A soft reset (`reset` of zxnext.vhd without the configuration load) leaves these registers of the whole-byte table alone: NR #04 (no
/// reset at all, 5717-5732), NR #7F, NR #81, NR #8F (power-on values only), NR #80 (the high nibble takes the low one, 2186) and
/// NR #82-#89 (restored only by their reset-type bit, handled in Reset)
bool SoftResetKeeps(uint8_t number)
{
    return number == 0x04 || number == 0x7F || number == 0x80 || number == 0x81 || (number >= 0x82 && number <= 0x89) || number == 0x8F;
}
}  // namespace

void NextBoard::Reset(bool hard)
{
    // A soft reset keeps the machine type (and with it config mode or not); a hard one is the power-on state
    // the bare personality (no boot ROM) is a 128K machine from the start
    const uint8_t type = hard ? (_memory->HasBootRom() ? 0 : 2) : MachineType();
    if (hard)
    {
        // NR #02 bit 1 loads the FPGA configuration again (zxnext_top_issue5.vhd 1095, flashboot of core 1): every signal takes its
        // declared value
        _resetType = 4;  // power on
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
        // zxnext.vhd 4930-5090: the block `if reset = '1'` names these (soft and hard alike); everything else the program or the
        // firmware set stays: NR #05 (joysticks, 50 / 60 Hz, scandoubler), NR #0A, NR #11, NR #7F, NR #81, NR #8A, NR #8F, ...
        _regs[0x02] &= static_cast<uint8_t>(~0x0C);  // the generated-NMI flags (3844-3861); the bus reset bit 7 is not reset
        _regs[0x06] |= 0xA0;                         // the CPU speed and 50 / 60 Hz hotkeys on
        _regs[0x08] &= static_cast<uint8_t>(~0x40);  // contention on
        _regs[0x09] &= static_cast<uint8_t>(~0x10);  // the sprite number is not tied to port #303B
        for (uint8_t r : {0x15, 0x68, 0x6A, 0x6B, 0x6C, 0x70, 0x71, 0xA0, 0xA2, 0xD8})
            _regs[r] = 0;
        // NR #80 (2186): the high nibble takes the low one
        _regs[0x80] = static_cast<uint8_t>((_regs[0x80] & 0x0F) | ((_regs[0x80] & 0x0F) << 4));
        // NR #82-#85 come back to #FF when their reset-type bit (7 of NR #85) is 1, NR #86-#89 when bit 7 of NR #89 is 0 (5052-5068)
        if (_regs[0x85] & 0x80)
        {
            _regs[0x82] = _regs[0x83] = _regs[0x84] = 0xFF;
            _regs[0x85] = 0xFF;
        }
        if (!(_regs[0x89] & 0x80))
        {
            _regs[0x86] = _regs[0x87] = _regs[0x88] = 0xFF;
            _regs[0x89] = 0x7F;
        }
    }
    // the tilemap base addresses reset to #2C / #0C (the VHDL's signals; the register table has no reset value for them)
    _regs[0x6E] = 0x2C;
    _regs[0x6F] = 0x0C;
    _regs[0xA9] = 0x01;  // nr_a9_esp_gpio0 (5085)
    _regs[kRegMachineType] = type;
    // the whole-byte reset values of the table (registers.txt); the registers with devices behind them reset those on their own
    size_t count;
    for (const NextRegInfo* r = NextRegTable(count); r != NextRegTable(count) + count; r++)
        if (r->hasReset && r->number != kRegMachineType && (hard || !SoftResetKeeps(r->number)))
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
    // nr_register <= X"24" (4603): "protection against legacy programs accidentally hitting ports 0x243B, 0x253B"
    _selected = 0x24;
    _resetPending = false;
    _video.Reset(hard);  // the palette index and control; the palette memory only with the configuration load
    RefreshLayer2Mapping();
    if (hard)
        _sprites.Reset();
    else
        _sprites.SoftReset();  // sprites.vhd reset_i: the indices, the mirror and the status; the attribute and pattern RAMs stay
    _copper.Reset(hard);       // zxnext.vhd: mode, address, stored byte and offset; copper.vhd: the list counter; the list RAM on a hard one
    if (_interrupts)
    {
        _interrupts->Reset();
        _interrupts->SetCounterOffset(0);  // nr_64_copper_offset is reset: the line counter the interrupts see follows it
    }
    const bool config = type == 0;
    _memory->SetConfigBank(_regs[kRegConfigMapping]);  // NR #04 is not reset: zero at power on, the program's value after a soft reset
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
            // bit 7 the expansion bus reset, bits 3:2 the NMI requests as written, bits 1:0 how the last reset happened (the VHDL's
            // nr_02_reset_type: 0 power on, then 2, then 1 for the soft resets that follow)
            return static_cast<uint8_t>((_regs[reg] & 0x8C) | (_resetType & 3));
        case kRegAltRom:
            return _memory->AltRomRegister();
        case kRegMemoryMapping:
            return _machine ? _machine->ReadMemoryMapping() : _regs[reg];
        case kRegCpuSpeed:
            return static_cast<uint8_t>((_regs[reg] & 3) | ((_regs[reg] & 3) << 4));  // programmed | actual
        case 0xB0:
        case 0xB1:
        case 0xB2:
            return _machine ? _machine->ReadExtendedInput(reg) : 0;
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
        case 0x34:
            return _sprites.ReadMirrorSprite((_regs[0x09] & 0x10) != 0);
        case 0x61:
            return _copper.ReadAddressLow();
        case 0x62:
            return _copper.ReadControl();
        case 0x64:
            return _copper.ReadOffset();
        case 0x60:
        case 0x63:
            return 0;
        case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
        case 0x75: case 0x76: case 0x77: case 0x78: case 0x79:
            return 0;  // write-only mirrors
        case 0x69:
            return static_cast<uint8_t>((_video.ReadDisplayControl() & ~0x40) | ((_machine && _machine->ShadowScreen()) ? 0x40 : 0));
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
        case 0x09:
            return static_cast<uint8_t>(_regs[reg] & ~0x08);  // bit 3 reads 0 (zxnext.vhd: mono & tie & '0' & hdmi & scanlines)
        case 0x28:
            return _video.StoredPaletteValue();  // 6004: nr_stored_palette_value, not the keymap address that was written
        case 0x85:
        case 0x89:
            return static_cast<uint8_t>(_regs[reg] & 0x8F);  // 6139 / 6151: reset type & "000" & the four port enables
        case 0x8A:
            return static_cast<uint8_t>(_regs[reg] & 0x3F);  // "00" & nr_8a_bus_port_propagate
        case 0x6E:
        case 0x6F:
            return static_cast<uint8_t>(_regs[reg] & 0xBF);  // bit 6 is not stored (base_7 & '0' & base)
        case 0x10:
            return static_cast<uint8_t>((_regs[reg] & 0x1F) << 2);  // the core id; the DRIVE / M1 buttons read 0
        case 0x90:
            return static_cast<uint8_t>(_regs[reg] & 0xFC);  // the output enables of GPIO 0-7 keep bits 1:0 clear (zxnext.vhd 5536)
        case 0x93:
            return static_cast<uint8_t>(_regs[reg] & 0x0F);
        case 0x98:
        case 0x99:
        case 0x9A:
        case 0x9B:
        {
            // a read is the PIN state (i_GPIO), not the output latch: an enabled output shows its value, the others what the Pi drives
            // (nothing is wired to the header here yet: low)
            const unsigned bank = reg - 0x98;
            uint8_t level = _gpioInput ? _gpioInput(bank) : 0;
            const uint8_t enable = Read(static_cast<uint8_t>(0x90 + bank));
            const uint8_t value = static_cast<uint8_t>(_regs[reg] & (bank == 3 ? 0x0F : 0xFF));
            return static_cast<uint8_t>((value & enable) | (level & ~enable));
        }
        case 0xA0:
            return static_cast<uint8_t>(_regs[reg] & 0x39);  // "00" & pi peripheral bits 5:3 & "00" & bit 0 (zxnext.vhd 6189)
        case 0xA2:
            return static_cast<uint8_t>((_regs[reg] & 0xDD) | 0x02);  // bit 5 reads 0 and bit 1 reads 1 (6192)
        case 0xA8:
            return static_cast<uint8_t>(_regs[reg] & 0x01);  // the ESP's GPIO0 output enable
        case 0xA9:
            // the pins: GPIO2 is pulled up, GPIO0 is what the board drives while NR #A8 enables it, else pulled up (6201)
            return static_cast<uint8_t>(0x04 | ((_regs[0xA8] & 1) ? (_regs[reg] & 1) : 1));
        case 0x08:
            return static_cast<uint8_t>((_regs[reg] & 0x7F) | ((_machine && _machine->PagingLocked()) ? 0 : 0x80));  // bit 7: port #7FFD is not locked
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
    // A write of the program comes after what the copper has due: the copper runs to the beam first (and the picture follows it), so the
    // journal and the breakpoints see the writes in the order of their time
    if (_beforeCpuWrite && !_copperWrite)
        _beforeCpuWrite();
    if (_journal.Enabled()) [[unlikely]]
    {
        uint32_t frame = 0, t = 0;
        uint16_t pc = 0;
        if (_journalClock)
            _journalClock(frame, t, pc);
        int16_t beamLine = -1, beamHc = -1;
        uint64_t clock = 0;
        if (_copperWrite || _copper.BeamNow(clock))
        {
            if (_copperWrite)
                clock = _copper.LastMoveAt();  // the copper's own time, not the CPU's when the picture caught it up
            beamLine = static_cast<int16_t>(_copper.BeamLine(clock));
            beamHc = static_cast<int16_t>(_copper.BeamHc(clock));
        }
        _journal.Record(frame, t, pc, _copperWrite ? NextRegSource::Copper : _source, reg, value, _regs[reg], beamLine, beamHc);
    }
    if (_accessHook) [[unlikely]]
        _accessHook(true, reg, value, _copperWrite ? NextRegSource::Copper : _source);
    // the sound follows NR #06 / #08 / #09 once their value is stored, whichever path stores it
    struct AudioNotify
    {
        NextBoard* board;
        uint8_t reg;
        ~AudioNotify()
        {
            if ((reg == 0x06 || reg == 0x08 || reg == 0x09) && board->_machine)
                board->_machine->AudioConfigChanged();
            else if ((reg == 0xA8 || reg == 0xA9 || reg == 0x0B) && board->_machine)
                board->_machine->BoardPinsChanged();
        }
    } audioNotify{this, reg};
    if ((reg == 0x2C || reg == 0x2D || reg == 0x2E) && _machine)
        _machine->DacMirrorWrite(reg, value);
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
        case 0x60:
            _copper.WriteData(value);
            return;
        case 0x61:
            _copper.WriteAddressLow(value);
            return;
        case 0x62:
            _copper.WriteControl(value);
            return;
        case 0x63:
            _copper.WriteWord(value);
            return;
        case 0x64:
            _copper.WriteOffset(value);
            _regs[reg] = value;
            if (_interrupts)
                _interrupts->SetCounterOffset(value);
            return;
        case 0x12:
        case 0x13:
            _regs[reg] = value;
            RefreshLayer2Mapping();
            return;
        case 0x34:
            _regs[reg] = value;
            _sprites.WriteMirrorSprite(value, (_regs[0x09] & 0x10) != 0);
            return;
        case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
            _sprites.WriteMirrorAttribute(reg - 0x35, value, false, (_regs[0x09] & 0x10) != 0);
            return;
        case 0x75: case 0x76: case 0x77: case 0x78: case 0x79:
            _sprites.WriteMirrorAttribute(reg - 0x75, value, true, (_regs[0x09] & 0x10) != 0);
            return;
        case 0x1C:
            _video.WriteClipControl(value);
            return;
        case 0x69:
            _video.WriteDisplayControl(value);
            if (_machine)
                _machine->SetShadowScreen((value & 0x40) != 0);
            _regs[reg] = value;
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
            {
                // bit 7 the expansion bus reset is held; bits 3:2 read 1 once their NMI was generated and until written 0
                uint8_t generated = _regs[reg] & 0x0C & value;
                if ((value & 0x04) && _machine && _machine->GenerateDriveNmi())
                    generated |= 0x04;
                if ((value & 0x08) && _machine && _machine->GenerateMultifaceNmi())
                    generated |= 0x08;
                _regs[reg] = static_cast<uint8_t>((value & 0x80) | generated);
            }
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
            if ((_regs[reg] & 0x10) && !(value & 0x10))
                _sprites.ReleaseMirror();  // the mirror stops following the port: it keeps the sprite number it had
            _regs[reg] = value;
            if ((value & 0x08) && _machine)
                _machine->ClearDivMmcMapram();
            return;
        case kRegPeripheral3:
            // zxnext.vhd 5192-5198: the Multiface type (bits 7:6) and the SD card swap (bit 5) are written in config mode only; the DivMMC
            // automap enable (bit 4) is the automap_reset input (4112)
            _regs[reg] = _memory->InConfigMode() ? value : static_cast<uint8_t>((value & 0x1F) | (_regs[reg] & 0xE0));
            if (_machine)
                _machine->DivMmcEnableChanged();
            return;
        case 0x83:  // the internal port enables: bit 0 is the DivMMC's port
            _regs[reg] = value;
            if (_machine)
                _machine->DivMmcEnableChanged();
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
            if ((value & 0x80) && _machine)
                _machine->UnlockPaging();
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

void NextBoard::RefreshLayer2Mapping()
{
    NextMemory::Layer2View view;
    view.read = _video.Layer2MapRead();
    view.write = _video.Layer2MapWrite();
    view.segment = _video.Layer2MapSegment();
    view.bank = static_cast<uint8_t>((_video.Layer2MapShadow() ? _regs[0x13] : _regs[0x12]) & 0x7F);
    view.offset = _video.Layer2Offset();
    _memory->SetLayer2View(view);
}

void NextBoard::AfterInstruction()
{
    // The copper is a bus master of the machine: it runs to the beam after every instruction, whether or not a picture is drawn
    _copper.Poll();
    if (_machine)
    {
        _machine->OnInstructionDone();
        _machine->StepDma();
    }
    if (!_resetPending)
        return;
    _resetPending = false;
    if (!_resetHard)
        {
        // zxnext.vhd: '0' & t(2) & (t(1) or t(0))
        const unsigned t2 = (_resetType >> 2) & 1, t1 = (_resetType >> 1) & 1, t0 = _resetType & 1;
        _resetType = static_cast<uint8_t>((t2 << 1) | (t1 | t0));
    }
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
