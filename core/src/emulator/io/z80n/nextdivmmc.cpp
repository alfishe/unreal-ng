#include "stdafx.h"

#include "nextdivmmc.h"

#include "emulator/io/z80n/nextboard.h"

void NextDivMmc::Note(NextDivMmcKind kind, uint16_t address, bool instant)
{
    if (!_journal.Enabled()) [[likely]]
        return;
    NextDivMmcEvent e;
    if (fillClock)
        fillClock(e);
    e.kind = kind;
    e.address = address;
    e.instant = instant;
    e.mapped = Mapped();
    e.control = ReadPort();
    _journal.Record(e);
    if (kind == NextDivMmcKind::MapIn && address == 0x0008 && fillCall)
    {
        NextDivMmcEvent call = e;
        call.kind = NextDivMmcKind::EsxCall;
        fillCall(call);
        _journal.Record(call);
    }
}

void NextDivMmc::Reset()
{
    _control = 0;
    _automap = false;
    _buttonNmi = false;
    Publish();
    Note(NextDivMmcKind::Reset);
}

bool NextDivMmc::PortEnabled() const
{
    return (_board->Stored(0x83) & 0x01) != 0;  // internal_port_enable(8)
}

bool NextDivMmc::AutomapEnabled() const
{
    return PortEnabled() && (_board->Stored(NextBoard::kRegPeripheral3) & 0x10);  // NR #0A bit 4: divmmc_automap_reset is the inverse
}

void NextDivMmc::OnEnableChanged()
{
    if (!AutomapEnabled())
    {
        const bool was = _automap;
        _automap = false;  // divmmc.vhd 108-148: hold, held, button_nmi cleared, the combinational automap forced low
        _buttonNmi = false;
        Publish();
        if (was)
            Note(NextDivMmcKind::Disabled);
        return;
    }
    Publish();
}

void NextDivMmc::ReleaseForProgramStart()
{
    _automap = false;
    _buttonNmi = false;
    _control = static_cast<uint8_t>(_control & ~0x80);
    Publish();
    Note(NextDivMmcKind::ProgramStart);
}

void NextDivMmc::PressButton()
{
    if (AutomapEnabled())
    {
        _buttonNmi = true;
        Note(NextDivMmcKind::Button);
    }
}

void NextDivMmc::OnRetn()
{
    _buttonNmi = false;
    if (_automap)
    {
        _automap = false;
        Publish();
        Note(NextDivMmcKind::Retn);
    }
}

void NextDivMmc::Publish()
{
    NextMemory::DivMmcView view;
    view.mapped = Mapped();
    view.mapram = Mapram();  // divmmc.vhd 94-95: bank 3 replaces the ROM whatever CONMEM says
    view.bank = Bank();
    _memory->SetDivMmcView(view);
}

void NextDivMmc::WritePort(uint8_t value)
{
    if (!PortEnabled())
        return;  // zxnext.vhd 2608: port_e3 = lsb and port_divmmc_io_en
    _control = static_cast<uint8_t>((value & 0x8F) | (value & 0x40) | (_control & 0x40));  // MAPRAM only sets
    Publish();
    Note(NextDivMmcKind::Port);
}

void NextDivMmc::ClearMapram()
{
    _control = static_cast<uint8_t>(_control & ~0x40);
    Publish();
    Note(NextDivMmcKind::MapramClear);
}

NextDivMmc::Hit NextDivMmc::Classify(uint16_t address) const
{
    if (_memory->MultifaceActive())
        return Hit::None;  // zxnext.vhd: while the Multiface memory is in, sram_pre_override is "000" - the DivMMC's automap is not active
    if (!AutomapEnabled())
        return Hit::None;  // NR #0A bit 4 / the port enable: automap_reset
    // sram_divmmc_automap_rom3_en (zxnext.vhd 3138): the fetch sees ROM through the MMU (RAM in slot 0 / 1 is the program's own code, config
    // mode is "110"), no Layer 2 read mapping stands over it, and the 48K BASIC ROM is the one selected
    const bool rom3 = _memory->MmuShowsRomAt(address) && isBasicRomPaged && isBasicRomPaged() && !_memory->Layer2ReadsAt(address);
    const uint8_t enable = _board->Stored(kRegAutomapEnable);
    const uint8_t valid = _board->Stored(kRegAutomapValid);
    const uint8_t timing = _board->Stored(kRegAutomapTiming);
    const uint8_t extra = _board->Stored(kRegAutomapExtra);

    if (address == 0x0066)
    {
        // the NMI entry maps in only for the button's NMI (zxnext.vhd 2907-2908): NR #BB bit 1 instant, bit 0 delayed
        if (!_buttonNmi)
            return Hit::None;
        return (extra & 0x02) ? Hit::Instant : (extra & 0x01) ? Hit::Delayed : Hit::None;
    }
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
        Note(NextDivMmcKind::MapIn, address, true);
    }
}

void NextDivMmc::OnMachineM1(uint16_t address)
{
    switch (Classify(address))
    {
        case Hit::Delayed:
        case Hit::Instant:
            if (address == 0x0066)
                _buttonNmi = false;  // automap_held takes the button over
            if (!_automap)
            {
                _automap = true;
                Publish();
                Note(NextDivMmcKind::MapIn, address, false);
            }
            break;
        case Hit::MapOut:
            if (_automap)
            {
                _automap = false;
                Publish();
                Note(NextDivMmcKind::MapOut, address);
            }
            break;
        default:
            break;
    }
}
