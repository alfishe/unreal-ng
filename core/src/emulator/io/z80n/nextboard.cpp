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
    const uint8_t dtLock = hard ? 0 : static_cast<uint8_t>(_regs[kRegMachineType] & 0x08);  // the dt lock survives a soft reset
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
        for (uint8_t r : {0x15, 0x68, 0x6A, 0x6B, 0x6C, 0x70, 0x71, 0xA0, 0xA2, 0xD8, 0xD9, 0xDA})
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
    if (hard)
        LatchVideoFrameSync();
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
    _regs[kRegMachineType] = static_cast<uint8_t>((_timing << 4) | dtLock | type);  // NR #03 reads timing, lock and type
    if (_timing == 4)
        _regs[0x05] &= static_cast<uint8_t>(~0x04);  // the Pentagon is always 50 Hz
    if (_machine)
    {
        _machine->SetCpuSpeed(1);
        _machine->ResetContentionDisabled();
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
    // nr_03_config_mode changes on NR #03 writes only (zxnext.vhd 5146-5151): a soft reset keeps it, a power-on one starts in it
    const bool config = hard ? type == 0 : _memory->InConfigMode();
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
            return static_cast<uint8_t>((_regs[reg] & 0x8C) | ((_regs[0xDA] & 3) ? 0x10 : 0) | (_resetType & 3));  // bit 4 the I/O trap flag (5891)
        case kRegAltRom:
            return _memory->AltRomRegister();
        case kRegMemoryMapping:
            return _machine ? _machine->ReadMemoryMapping() : _regs[reg];
        case kRegConfigMapping:
            return 0;  // zxnext.vhd has no read case for NR #04: the multiplexer's default
        case kRegPeripheral3:
            return static_cast<uint8_t>(_regs[reg] & 0xFB);  // {mf type, sd swap, automap, mouse reverse, 0, mouse dpi}
        case kRegMachineType:
            // zxnext.vhd: nr_palette_sub_idx & machine timing & dt lock & machine type
            return static_cast<uint8_t>((_regs[reg] & 0x7F) | (_video.PaletteSubIndex() ? 0x80 : 0));
        case kRegCpuSpeed:
            // zxnext.vhd: "00" & cpu_speed & "00" & nr_07_cpu_speed - bits 5:4 the speed in force, 1:0 the one programmed
            return static_cast<uint8_t>((_regs[reg] & 3) | (ActiveCpuSpeed() << 4));
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
        case 0x05:
            return static_cast<uint8_t>((_regs[reg] & 0xFA) | _eff05);  // 5896: bits 2 and 0 are eff_nr_05_5060 / eff_nr_05_scandouble_en
        case 0x09:
            // 5909: mono & tie & '0' & hdmi & eff_nr_09_scanlines (bit 3 reads 0, bits 1:0 as the last frame sync took them)
            return static_cast<uint8_t>((_regs[reg] & 0xF4) | _eff09);
        case 0x81:
            // 6126: i_BUS_ROMCS_n & bits 6:3, bit 2 = 0, the speed bits 00. ROMCS_n is the edge connector's pin: with
            // nothing on the bus the real board (core 3.1.5, NextReg_defaults) reads it 0, the register reads #00
            return static_cast<uint8_t>(_regs[reg] & 0x78);
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
        case 0xDA:
            return static_cast<uint8_t>(_regs[reg] & 3);  // the cause: 01 read of #2FFD, 10 read of #3FFD, 11 write of #3FFD (6272)
        case 0x90:
            return static_cast<uint8_t>(_regs[reg] & 0xFC);  // the output enables of GPIO 0-7 keep bits 1:0 clear (zxnext.vhd 5536)
        case 0x93:
            return static_cast<uint8_t>(_regs[reg] & 0x0F);
        case 0x98:
        case 0x99:
        case 0x9A:
        case 0x9B:
            // a read is the PIN state (i_GPIO), not the output latch (6176-6189)
            return static_cast<uint8_t>((GpioPins() >> (8 * (reg - 0x98))) & (reg == 0x9B ? 0x0F : 0xFF));
        case 0x2C:
            _i2sLow = static_cast<uint8_t>(PiAudioLeft() & 3);
            return static_cast<uint8_t>(PiAudioLeft() >> 2);  // 6007: the Pi's I2S sample, not what the DAC mirror was written
        case 0x2D:
            return static_cast<uint8_t>(_i2sLow << 6);
        case 0x2E:
            _i2sLow = static_cast<uint8_t>(PiAudioRight() & 3);
            return static_cast<uint8_t>(PiAudioRight() >> 2);
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
            // bit 7: port #7FFD is not locked; bit 6: the contention switch in effect (zxnext.vhd 5906)
            if (!_machine)
                return static_cast<uint8_t>(_regs[reg] | 0x80);
            return static_cast<uint8_t>((_regs[reg] & 0x3F) | (_machine->PagingLocked() ? 0 : 0x80) | (_machine->ContentionDisabled() ? 0x40 : 0));
        default:
            break;
    }
    uint8_t value;
    if (_interrupts && _interrupts->ReadNr(reg, value))
        return value;
    return _regs[reg];
}

uint32_t NextBoard::GpioPins() const
{
    uint32_t enable = (static_cast<uint32_t>(_regs[0x90] & 0xFC)) | (static_cast<uint32_t>(_regs[0x91]) << 8) |
                      (static_cast<uint32_t>(_regs[0x92]) << 16) | (static_cast<uint32_t>(_regs[0x93] & 0x0F) << 24);
    uint32_t out = static_cast<uint32_t>(_regs[0x98]) | (static_cast<uint32_t>(_regs[0x99]) << 8) | (static_cast<uint32_t>(_regs[0x9A]) << 16) |
                   (static_cast<uint32_t>(_regs[0x9B] & 0x0F) << 24);
    uint32_t in = 0x0C;  // GPIO 2 and 3: the Pi pulls its I2C pair up
    if (_gpioInput)
    {
        in = 0;
        for (unsigned bank = 0; bank < 4; bank++)
            in |= static_cast<uint32_t>(_gpioInput(bank)) << (8 * bank);
    }
    auto own = [&](unsigned pin, bool driven, bool level) {
        const uint32_t bit = 1u << pin;
        enable = driven ? (enable | bit) : (enable & ~bit);
        out = level ? (out | bit) : (out & ~bit);
    };
    const uint8_t a0 = _regs[0xA0], a2 = _regs[0xA2];
    if (a0 & 0x01)  // SPI0: chip selects 7 / 8 idle high, MOSI 10 and SCK 11 idle low, MISO 9 an input
    {
        own(7, true, true);
        own(8, true, true);
        own(10, true, false);
        own(11, true, false);
        own(9, false, false);
    }
    if (a0 & 0x10)  // UART 1: TX on GPIO15 (the Pi orientation, bit 5) or 14, idle high; the other pin is RX (idle high)
    {
        const bool rxtx = (a0 & 0x20) != 0;
        own(rxtx ? 15 : 14, true, true);
        own(rxtx ? 14 : 15, false, false);
        if (!_gpioInput)
            in |= 1u << (rxtx ? 14 : 15);
    }
    if (a2 & 0xC0)  // I2S: SCK and WS are the Pi's, the data pin of the direction is the Next's (level not modelled: 0)
    {
        own(18, false, false);
        own(19, false, false);
        const bool inout = (a2 & 0x10) != 0;
        own(20, inout, false);
        own(21, !inout, false);
    }
    return (out & enable) | (in & ~enable);
}

void NextBoard::SetI2sSample(unsigned left, unsigned right)
{
    _i2sLeft = left & 0x3FF;
    _i2sRight = right & 0x3FF;
    UpdatePiEar();
    if (_machine)
        _machine->PiAudioChanged(PiAudioLeft(), PiAudioRight());
}

unsigned NextBoard::PiAudioLeft() const
{
    // 2358: silence when the I2S is off, the left is muted or the EAR mode takes the audio; the left sample (or the right one on a right-only setup)
    const uint8_t a2 = _regs[0xA2];
    if (!(a2 & 0xC0) || (a2 & 0x08) || (a2 & 0x01))
        return 0x200;
    return (a2 & 0x80) ? _i2sLeft : _i2sRight;
}

unsigned NextBoard::PiAudioRight() const
{
    const uint8_t a2 = _regs[0xA2];
    if (!(a2 & 0xC0) || (a2 & 0x04) || (a2 & 0x01))
        return 0x200;
    return (a2 & 0x40) ? _i2sRight : _i2sLeft;
}

void NextBoard::UpdatePiEar()
{
    // 2361-2373: a Schmitt trigger on the top two bits of the raw samples, only with the I2S on and bit 0 of NR #A2 set
    const uint8_t a2 = _regs[0xA2];
    if (!(a2 & 0xC0) || !(a2 & 0x01))
    {
        _piEar = false;
        return;
    }
    const unsigned threshold = ((_i2sLeft >> 8) | (_i2sRight >> 8)) & 3;
    if (_piEar && threshold == 0)
        _piEar = false;
    else if (!_piEar && threshold == 3)
        _piEar = true;
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
            else if ((reg == 0xA8 || reg == 0xA9 || reg == 0x0B || reg == kRegResetType) && board->_machine)
                board->_machine->BoardPinsChanged();
            else if (reg == 0xA2)
            {
                board->UpdatePiEar();  // NR #A2 turns the Pi's I2S into the mixer's input and the EAR
                if (board->_machine)
                    board->_machine->PiAudioChanged(board->PiAudioLeft(), board->PiAudioRight());
            }
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
        case 0x28:  // NR #28-#2B: the PS/2 keymap is not modelled, the key joystick map is (zxnext.vhd 6297-6322)
            _regs[reg] = value;
            _keyJoystick.WriteAddressHigh(value);
            return;
        case 0x29:
            _regs[reg] = value;
            _keyJoystick.WriteAddressLow(value);
            return;
        case 0x2B:
            _regs[reg] = value;
            _keyJoystick.WriteData(value);
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
        case 0x11:
            // zxnext.vhd 5208-5217 on the issue 2 core (g_video_inc "10"): written in config mode only, 7 (HDMI) is VGA 0, else bit 0
            // picks VGA 0 / 1. A change moves the PLL to the new CLK_28 and runs the top level's power-on reset (zxnext_top_issue2.vhd
            // 771-783): the core sees `reset` as on a soft one, NR #11 and config mode stay, so the boot ROM starts again
            if (_memory->InConfigMode())
            {
                const uint8_t timing = (value & 7) == 7 ? 0 : static_cast<uint8_t>(value & 1);
                if (timing != _regs[reg])
                {
                    _regs[reg] = timing;
                    _resetPending = true;
                    _resetHard = false;
                    _resetClock = true;
                }
            }
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
                _resetClock = false;
            }
            {
                // bit 7 the expansion bus reset is held; bits 3:2 read 1 once their NMI was generated and until written 0
                uint8_t generated = _regs[reg] & 0x0C & value;
                if ((value & 0x04) && _machine && _machine->GenerateDriveNmi())
                    generated |= 0x04;
                if ((value & 0x08) && _machine && _machine->GenerateMultifaceNmi())
                    generated |= 0x08;
                _regs[reg] = static_cast<uint8_t>((value & 0x80) | generated);
                if (!(value & 0x10))
                    _regs[0xDA] = 0;  // writing bit 4 as 0 clears the trap cause (3880)
            }
            return;
        case kRegMachineType:
        {
            // zxnext.vhd 5121-5151, verbatim: a write switches the boot ROM off; bits 6:4 pick the timing only with bit 7, an open
            // lock and bit 3 = 0 (000 -> 001, 101-111 -> 011); bit 3 toggles the lock; the type (1-4 only) is taken in config mode;
            // 111 enters config mode, any other non-zero value leaves it
            _memory->SetBootRomEnabled(false);
            uint8_t lock = _regs[reg] & 0x08;
            if ((value & 0x80) && !lock && !(value & 0x08))
            {
                static constexpr uint8_t kTimingRemap[8] = {1, 1, 2, 3, 4, 3, 3, 3};
                const uint8_t wanted = kTimingRemap[(value >> 4) & 7];
                if (wanted != _timing)
                {
                    _timing = wanted;
                    if (_machine)
                        _machine->SetMachineTiming(_timing);
                }
            }
            lock ^= value & 0x08;
            if (_timing == 4)
                _regs[0x05] &= static_cast<uint8_t>(~0x04);
            uint8_t type = _regs[reg] & 7;
            if (_memory->InConfigMode() && (value & 7) >= 1 && (value & 7) <= 4)
                type = value & 7;
            if ((value & 7) == 7)
                _memory->SetConfigMode(true);
            else if ((value & 7) != 0)
                _memory->SetConfigMode(false);
            _regs[reg] = static_cast<uint8_t>((_timing << 4) | lock | type);
            _memory->SetMachineType(type == 0 ? 2 : type);
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
            _regs[reg] = _memory->InConfigMode() ? static_cast<uint8_t>(value & 0xFB) : static_cast<uint8_t>((value & 0x1B) | (_regs[reg] & 0xE0));
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
        case 0x05:  // the Pentagon is always 50 Hz (zxnext.vhd nr_05_5060): bit 2 does not stay set under its timing
            _regs[reg] = _timing == 4 ? static_cast<uint8_t>(value & ~0x04) : value;
            return;
        case kRegCpuSpeed:
            _regs[reg] = value & 0x03;
            ApplyCpuSpeed();
            return;
        case 0x80:  // the expansion bus enable: while it is on, the CPU runs at the bus speed, hard-wired to 3.5 MHz (expbus_speed)
            _regs[reg] = value;
            ApplyCpuSpeed();
            return;
        case kRegConfigMapping:
            _regs[reg] = value;  // Issue 5 PCB (zxnext.vhd gen_romram_5): all 8 bits; the older boards keep 7
            _memory->SetConfigBank(value);
            return;
        default:
            _regs[reg] = value;
            return;
    }
}

uint8_t NextBoard::ActiveCpuSpeed() const
{
    return (_regs[0x80] & 0x80) ? 0 : static_cast<uint8_t>(_regs[kRegCpuSpeed] & 3);
}

void NextBoard::ApplyCpuSpeed()
{
    if (_machine)
        _machine->SetCpuSpeed(static_cast<uint8_t>(1u << ActiveCpuSpeed()));
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
    if (_resetClock)
    {
        // nr_02_reset_type follows nr_02_soft_reset only: a power-on reset of the top level leaves it
        _resetClock = false;
        if (_machine)
        {
            _machine->SystemClockChanged();
            _machine->PerformReset(false);
        }
        return;
    }
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
