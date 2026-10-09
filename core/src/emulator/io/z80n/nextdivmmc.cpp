#include "stdafx.h"

#include "nextdivmmc.h"

#include "emulator/io/z80n/nextboard.h"

void NextDivMmc::Reset()
{
    _control = 0;
    _automap = false;
    Publish();
}

void NextDivMmc::Publish()
{
    NextMemory::DivMmcView view;
    view.mapped = Mapped();
    view.bank3AtZero = Mapram() && (_control & 0x80) == 0;  // CONMEM beats MAPRAM: the ROM
    view.bank = Bank();
    _memory->SetDivMmcView(view);
}

void NextDivMmc::WritePort(uint8_t value)
{
    _control = static_cast<uint8_t>((value & 0x8F) | (value & 0x40) | (_control & 0x40));  // MAPRAM only sets
    Publish();
}

void NextDivMmc::ClearMapram()
{
    _control = static_cast<uint8_t>(_control & ~0x40);
    Publish();
}

NextDivMmc::Hit NextDivMmc::Classify(uint16_t address) const
{
    if (!(_board->Stored(NextBoard::kRegPeripheral3) & 0x10))
        return Hit::None;  // NR #0A bit 4: the automap is off
    const bool rom3 = isBasicRomPaged && isBasicRomPaged();
    const uint8_t enable = _board->Stored(kRegAutomapEnable);
    const uint8_t valid = _board->Stored(kRegAutomapValid);
    const uint8_t timing = _board->Stored(kRegAutomapTiming);
    const uint8_t extra = _board->Stored(kRegAutomapExtra);

    if (address < 0x40 && (address & 7) == 0)
    {
        const unsigned n = address >> 3;
        if ((enable >> n) & 1 && (((valid >> n) & 1) || rom3))
            return ((timing >> n) & 1) ? Hit::Instant : Hit::Delayed;
        return Hit::None;
    }
    if ((address >> 8) == 0x3D)
        return ((extra & 0x80) && rom3) ? Hit::Instant : Hit::None;
    if (address >= 0x1FF8 && address <= 0x1FFF)
        return (extra & 0x40) ? Hit::MapOut : Hit::None;
    if (!rom3)
        return Hit::None;
    switch (address)
    {
        case 0x056A: return (extra & 0x20) ? Hit::Delayed : Hit::None;
        case 0x04D7: return (extra & 0x10) ? Hit::Delayed : Hit::None;
        case 0x0562: return (extra & 0x08) ? Hit::Delayed : Hit::None;
        case 0x04C6: return (extra & 0x04) ? Hit::Delayed : Hit::None;
        default: return Hit::None;
    }
}

void NextDivMmc::BeforeMachineM1(uint16_t address)
{
    if (Classify(address) == Hit::Instant && !_automap)
    {
        _automap = true;  // the opcode of this fetch already comes from the DivMMC
        Publish();
    }
}

void NextDivMmc::OnMachineM1(uint16_t address)
{
    switch (Classify(address))
    {
        case Hit::Delayed:
        case Hit::Instant:
            if (!_automap)
            {
                _automap = true;
                Publish();
            }
            break;
        case Hit::MapOut:
            if (_automap)
            {
                _automap = false;
                Publish();
            }
            break;
        default:
            break;
    }
}
