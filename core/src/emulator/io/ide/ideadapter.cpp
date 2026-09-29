#include "stdafx.h"

#include "ideadapter.h"

#include "emulator/emulatorcontext.h"
#include "emulator/io/ide/idecontroller.h"

using namespace ata;

/// region <Common>

AtaChannel* IdeAdapter::Channel() const
{
    IdeController* ide = _context ? _context->pIdeController : nullptr;
    return ide && ide->Enabled() ? &ide->Channel() : nullptr;
}

IDE_SCHEME IdeAdapter::Scheme() const
{
    IdeController* ide = _context ? _context->pIdeController : nullptr;
    return ide ? ide->Scheme() : IDE_NONE;
}

uint8_t IdeAdapter::ReadRegister(uint8_t reg)
{
    return Channel()->ReadRegister(reg);
}

void IdeAdapter::WriteRegister(uint8_t reg, uint8_t value)
{
    Channel()->WriteRegister(reg, value);
}

uint8_t IdeAdapter::ReadDataLow()
{
    const uint16_t word = Channel()->ReadData();
    _s.readLatch = static_cast<uint8_t>(word >> 8);
    return static_cast<uint8_t>(word & 0xFF);
}

uint16_t IdeAdapter::DmaReadWord()
{
    AtaChannel* channel = Channel();
    if (!channel)
        return 0xFFFF;
    const uint16_t word = channel->ReadData();
    _s.readLatch = static_cast<uint8_t>(word >> 8);
    return word;
}

void IdeAdapter::DmaWriteWord(uint16_t word)
{
    if (AtaChannel* channel = Channel())
        channel->WriteData(word);
}

void IdeAdapter::ResetUnits()
{
    if (AtaChannel* channel = Channel())
        channel->HardReset();
}

uint8_t IdeAdapter::AtmIntrqBit()
{
    AtaChannel* channel = Channel();
    AtaDevice* unit = channel ? channel->Unit(channel->Selected()) : nullptr;
    if (!unit || !unit->IsPresent() || (unit->State().control & DeviceControl::nIEN))
        return 0x40;
    return unit->State().intrq ? 0x40 : 0x00;
}

bool IdeAdapter::In(uint16_t port, const Gate& gate, uint8_t& value)
{
    if (!Channel())
        return false;
    switch (Scheme())
    {
        case IDE_NEMO: return !gate.dosPorts && NemoIn(port, false, value);
        case IDE_NEMO_A8: return !gate.dosPorts && NemoIn(port, true, value);
        case IDE_NEMO_DIVIDE: return EvoIn(port, value);
        case IDE_ATM:
            if ((port & 0x8202) == 0x0200)
            {
                // The #7FFD-class read: the IDE / DAC status, INTRQ on bit 6 (ungated)
                value = static_cast<uint8_t>(AtmIntrqBit() | 0x3F);
                return true;
            }
            return gate.dosPorts && AtmIn(port, value);
        case IDE_PROFI: return gate.profiExt && ProfiIn(port, value);
        case IDE_DIVIDE: return !gate.dosPorts && DivideIn(port, value);
        default: return false;  // SMUC: through the Scorpion decoder
    }
}

bool IdeAdapter::Out(uint16_t port, const Gate& gate, uint8_t value)
{
    if (!Channel())
        return false;
    switch (Scheme())
    {
        case IDE_NEMO: return !gate.dosPorts && NemoOut(port, false, value);
        case IDE_NEMO_A8: return !gate.dosPorts && NemoOut(port, true, value);
        case IDE_NEMO_DIVIDE: return EvoOut(port, value);
        case IDE_ATM: return gate.dosPorts && AtmOut(port, value);
        case IDE_PROFI: return gate.profiExt && ProfiOut(port, value);
        case IDE_DIVIDE: return !gate.dosPorts && DivideOut(port, value);
        default: return false;
    }
}

/// endregion </Common>

/// region <Nemo, Nemo-A8>

bool IdeAdapter::NemoIn(uint16_t port, bool a8Latch, uint8_t& value)
{
    if (port & 0x06)
        return false;  // A2 = A1 = 0 select the board
    const bool latch = a8Latch ? (port & 0x100) : (port & 0x01);
    if (latch)
    {
        value = _s.readLatch;
        return true;
    }
    _s.readLatch = 0xFF;
    const uint16_t select = port & 0x18;  // A4 A3: 10 = CS0, 01 = CS1
    if (select == 0x08)
    {
        value = (port & 0xE0) == 0xC0 ? ReadRegister(Control) : 0xFF;
        return true;
    }
    if (select != 0x10)
    {
        value = 0xFF;  // no chip select, or both
        return true;
    }
    const uint8_t reg = static_cast<uint8_t>((port >> 5) & 0x07);
    value = reg ? ReadRegister(reg) : ReadDataLow();
    return true;
}

bool IdeAdapter::NemoOut(uint16_t port, bool a8Latch, uint8_t value)
{
    if (port & 0x06)
        return false;
    const bool latch = a8Latch ? (port & 0x100) : (port & 0x01);
    if (latch)
    {
        _s.writeLatch = value;
        return true;
    }
    const uint16_t select = port & 0x18;
    if (select == 0x08)
    {
        if ((port & 0xE0) == 0xC0)
            WriteRegister(Control, value);
        return true;
    }
    if (select != 0x10)
        return true;
    const uint8_t reg = static_cast<uint8_t>((port >> 5) & 0x07);
    if (reg)
        WriteRegister(reg, value);
    else
        Channel()->WriteData(static_cast<uint16_t>((_s.writeLatch << 8) | value));
    return true;
}

/// endregion </Nemo, Nemo-A8>

/// region <ZX-Evo NemoIDE (NEMO-DIVIDE)>

bool IdeAdapter::EvoIn(uint16_t port, uint8_t& value)
{
    const uint8_t low = static_cast<uint8_t>(port);
    if (low == 0xC8)
    {
        _s.readPair = _s.writePair = _s.writeHigh = 0;
        value = ReadRegister(Control);
        return true;
    }
    const bool cs0 = (low & 0x1E) == 0x10;   // rrr1000x
    const bool alias = (low & 0x1F) == 0x08; // rrr01000: RTL aliases of CS0 (#C8 is CS1, above)
    if (!cs0 && !alias)
        return false;

    if (low == 0x11)
    {
        _s.readPair = _s.writePair = _s.writeHigh = 0;
        value = _s.readLatch;
        return true;
    }
    _s.writePair = _s.writeHigh = 0;
    if (low == 0x10)
    {
        // Divide read: the first #10 reads the word, the second returns its high byte
        if (!_s.readPair)
        {
            value = ReadDataLow();
            _s.readPair = 1;
        }
        else
        {
            value = _s.readLatch;
            _s.readPair = 0;
        }
        return true;
    }
    _s.readPair = 0;
    const uint8_t reg = static_cast<uint8_t>((low >> 5) & 0x07);
    if (reg)
        value = ReadRegister(reg);
    else
        value = static_cast<uint8_t>(Channel()->ReadData() & 0xFF);  // #08: a data access whose high byte is lost
    return true;
}

bool IdeAdapter::EvoOut(uint16_t port, uint8_t value)
{
    const uint8_t low = static_cast<uint8_t>(port);
    if (low == 0xC8)
    {
        _s.readPair = _s.writePair = _s.writeHigh = 0;
        WriteRegister(Control, value);
        return true;
    }
    const bool cs0 = (low & 0x1E) == 0x10;
    const bool alias = (low & 0x1F) == 0x08;
    if (!cs0 && !alias)
        return false;

    _s.readPair = 0;
    if (low == 0x11)
    {
        _s.writeLatch = value;  // Nemo order: the high byte first
        _s.writeHigh = 1;
        _s.writePair = 0;
        return true;
    }
    if (low == 0x10)
    {
        if (_s.writeHigh)
        {
            Channel()->WriteData(static_cast<uint16_t>((_s.writeLatch << 8) | value));
            _s.writeHigh = _s.writePair = 0;
        }
        else if (!_s.writePair)
        {
            _s.writeLatch = value;  // divide order: the low byte first
            _s.writePair = 1;
        }
        else
        {
            Channel()->WriteData(static_cast<uint16_t>((value << 8) | _s.writeLatch));
            _s.writePair = 0;
        }
        return true;
    }
    _s.writePair = _s.writeHigh = 0;
    const uint8_t reg = static_cast<uint8_t>((low >> 5) & 0x07);
    if (reg)
        WriteRegister(reg, value);
    else
        Channel()->WriteData(value);
    return true;
}

/// endregion </ZX-Evo NemoIDE (NEMO-DIVIDE)>

/// region <ATM Turbo 2+>

bool IdeAdapter::AtmIn(uint16_t port, uint8_t& value)
{
    if ((port & 0x1F) != 0x0F)
        return false;
    if (port & 0x100)
    {
        value = _s.readLatch;
        return true;
    }
    const uint8_t reg = static_cast<uint8_t>((port >> 5) & 0x07);
    value = reg ? ReadRegister(reg) : ReadDataLow();
    return true;
}

bool IdeAdapter::AtmOut(uint16_t port, uint8_t value)
{
    if ((port & 0x1F) != 0x0F)
        return false;
    if (port & 0x100)
    {
        _s.writeLatch = value;
        return true;
    }
    const uint8_t reg = static_cast<uint8_t>((port >> 5) & 0x07);
    if (reg)
        WriteRegister(reg, value);
    else
        Channel()->WriteData(static_cast<uint16_t>((_s.writeLatch << 8) | value));
    return true;
}

/// endregion </ATM Turbo 2+>

/// region <SMUC>

uint8_t IdeAdapter::SmucIn(uint16_t port, uint8_t system)
{
    if (!Channel())
        return 0xFF;
    // #D8BE is the latch whatever #FFBA says (MAME smuc.cpp, Xpeccy hdd.c;
    // UnrealSpeccy alone lets bit 7 take it over)
    if (!(port & 0x2000))
        return _s.readLatch;
    if (system & 0x80)
        return (port & 0x100) ? 0xFF : ReadRegister(Control);  // #FEBE: alternate status
    const uint8_t reg = static_cast<uint8_t>((port >> 8) & 0x07);
    return reg ? ReadRegister(reg) : ReadDataLow();
}

void IdeAdapter::SmucOut(uint16_t port, uint8_t system, uint8_t value)
{
    if (!Channel())
        return;
    if (!(port & 0x2000))
    {
        _s.writeLatch = value;  // #D8BE, whatever #FFBA says
        return;
    }
    if (system & 0x80)
    {
        if (!(port & 0x100))
            WriteRegister(Control, value);  // #FEBE: device control
        return;
    }
    const uint8_t reg = static_cast<uint8_t>((port >> 8) & 0x07);
    if (reg)
        WriteRegister(reg, value);
    else
        Channel()->WriteData(static_cast<uint16_t>((_s.writeLatch << 8) | value));
}

/// endregion </SMUC>

/// region <Profi>

bool IdeAdapter::ProfiIn(uint16_t port, uint8_t& value)
{
    if ((port & 0x9F) != 0x8B)
        return false;
    const uint8_t low = static_cast<uint8_t>(port);
    const uint8_t reg = static_cast<uint8_t>((port >> 8) & 0x07);
    if (low == 0xEB)
    {
        value = _s.readLatch;  // the high byte of the last data read
        return true;
    }
    if (low == 0xCB)
    {
        value = reg ? ReadRegister(reg) : ReadDataLow();
        return true;
    }
    return false;  // #AB, #8B: nothing drives the bus
}

bool IdeAdapter::ProfiOut(uint16_t port, uint8_t value)
{
    if ((port & 0x9F) != 0x8B)
        return false;
    const uint8_t low = static_cast<uint8_t>(port);
    const uint8_t reg = static_cast<uint8_t>((port >> 8) & 0x07);
    if (low == 0xCB)
    {
        _s.writeLatch = value;  // the high byte waits for #xxEB
        return true;
    }
    if (low == 0xEB)
    {
        if (reg)
            WriteRegister(reg, value);
        else
            Channel()->WriteData(static_cast<uint16_t>((_s.writeLatch << 8) | value));
        return true;
    }
    if (low == 0xAB)
    {
        if (reg == 6)
            WriteRegister(Control, value);
        return true;
    }
    return false;
}

/// endregion </Profi>

/// region <DivIDE>

bool IdeAdapter::DivideIn(uint16_t port, uint8_t& value)
{
    // A7 A5 A1 A0 = 1 0 1 1 with A6 = 0: #E3 / #E7 / #EB are DivIDE's paging,
    // not IDE registers (pico-spec; UnrealSpeccy's #A3 mask catches them)
    if ((port & 0xE3) != 0xA3)
        return false;
    const uint8_t reg = static_cast<uint8_t>((port >> 2) & 0x07);
    if (reg == 0)
    {
        _s.readPair ^= 1;
        value = _s.readPair ? ReadDataLow() : _s.readLatch;
        return true;
    }
    _s.readPair = 0;
    value = ReadRegister(reg);
    return true;
}

bool IdeAdapter::DivideOut(uint16_t port, uint8_t value)
{
    if ((port & 0xE3) != 0xA3)
        return false;
    const uint8_t reg = static_cast<uint8_t>((port >> 2) & 0x07);
    if (reg == 0)
    {
        _s.writePair ^= 1;
        if (_s.writePair)
            _s.writeLatch = value;  // the low byte first
        else
            Channel()->WriteData(static_cast<uint16_t>((value << 8) | _s.writeLatch));
        return true;
    }
    _s.writePair = 0;
    WriteRegister(reg, value);
    return true;
}

/// endregion </DivIDE>
