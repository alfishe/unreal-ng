#include "stdafx.h"

#include "divmmcpaging.h"

#include <cstdio>
#include <cstring>

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"

DivMmcPaging::DivMmcPaging(EmulatorContext* context) : _context(context)
{
    windowStart = 0x0000;
    windowEnd = 0x4000;
    observesReads = true;
    _rom = std::make_unique<uint8_t[]>(kRomSize);
    _ram = std::make_unique<uint8_t[]>(kBanks * kBankSize);
    std::memset(_rom.get(), 0xFF, kRomSize);
}

DivMmcPaging::~DivMmcPaging()
{
    Detach();
}

bool DivMmcPaging::Attach()
{
    if (_attached)
        return true;
    Core* core = _context ? _context->pCore : nullptr;
    PortDecoder* decoder = _context ? _context->pPortDecoder : nullptr;
    if (!core || !core->GetZ80() || !decoder)
        return false;
    Z80* z80 = core->GetZ80();
    if (z80->machineM1Hook)
    {
        LOGERROR("DivMmcPaging: the machine's M1 observer is taken; the automap cannot be installed");
        return false;
    }
    if (!decoder->RegisterFullDecodeLowBytePort(kPortControl, this) ||
        !decoder->RegisterFullDecodeLowBytePort(kPortSpiSelect, this) ||
        !decoder->RegisterFullDecodeLowBytePort(kPortSpiData, this))
    {
        decoder->UnregisterFullDecodeLowBytePort(kPortControl, this);
        decoder->UnregisterFullDecodeLowBytePort(kPortSpiSelect, this);
        decoder->UnregisterFullDecodeLowBytePort(kPortSpiData, this);
        LOGERROR("DivMmcPaging: ports #E3 / #E7 / #EB are taken");
        return false;
    }
    if (!core->AddBusOverlay(this))
    {
        decoder->UnregisterFullDecodeLowBytePort(kPortControl, this);
        decoder->UnregisterFullDecodeLowBytePort(kPortSpiSelect, this);
        decoder->UnregisterFullDecodeLowBytePort(kPortSpiData, this);
        return false;
    }
    z80->machineM1Hook = this;
    _attached = true;
    return true;
}

void DivMmcPaging::Detach()
{
    if (!_attached)
        return;
    _attached = false;
    Core* core = _context->pCore;
    Z80* z80 = core->GetZ80();
    if (z80->machineM1Hook == this)
        z80->machineM1Hook = nullptr;
    core->RemoveBusOverlay(this);
    if (PortDecoder* decoder = _context->pPortDecoder)
    {
        decoder->UnregisterFullDecodeLowBytePort(kPortControl, this);
        decoder->UnregisterFullDecodeLowBytePort(kPortSpiSelect, this);
        decoder->UnregisterFullDecodeLowBytePort(kPortSpiData, this);
    }
    _automap = false;
}

bool DivMmcPaging::LoadRom(const uint8_t* data, size_t size)
{
    if (!data || size != kRomSize)
        return false;
    std::memcpy(_rom.get(), data, kRomSize);
    _hasRom = true;
    return true;
}

bool DivMmcPaging::LoadRomFile(const std::string& path)
{
    FILE* file = FileHelper::OpenFile(FileHelper::NormalizePath(path), "rb");
    if (!file)
        return false;
    uint8_t buffer[kRomSize];
    const size_t size = std::fread(buffer, 1, sizeof buffer, file);
    std::fclose(file);
    return LoadRom(buffer, size);
}

void DivMmcPaging::Reset()
{
    _control = 0;
    _mapram = false;
    _automap = false;
    _selected = -1;
    _spiRx = 0xFF;
}

/// region <Ports>

uint8_t DivMmcPaging::portDeviceInMethod(uint16_t port)
{
    if (static_cast<uint8_t>(port) == kPortSpiData)
    {
        // A read clocks a transfer of #FF; the byte of the previous one is what the CPU gets
        const uint8_t result = _spiRx;
        _spiRx = _selected >= 0 ? _card[_selected].exchange(0xFF) : 0xFF;
        return result;
    }
    return 0xFF;  // #E3 and #E7 are write-only
}

void DivMmcPaging::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    switch (static_cast<uint8_t>(port))
    {
        case kPortControl:
            _control = value & (kConmem | kBankMask);
            if (value & kMapram)
                _mapram = true;  // sticky
            break;
        case kPortSpiSelect:
            SpiSelect(value);
            break;
        case kPortSpiData:
            _spiRx = _selected >= 0 ? _card[_selected].exchange(value) : 0xFF;
            break;
        default:
            break;
    }
}

void DivMmcPaging::SpiSelect(uint8_t value)
{
    // Active low: bit 0 card 0, bit 1 card 1
    int line = -1;
    if (!(value & 0x01))
        line = 0;
    else if (!(value & 0x02))
        line = 1;
    if (line == _selected)
        return;
    if (_selected >= 0)
        _card[_selected].select(false);
    _selected = line;
    if (line >= 0)
        _card[line].select(true);
}

/// endregion

/// region <Memory>

uint8_t DivMmcPaging::ReadMapped(uint16_t addr) const
{
    if (addr < kRomSize)
    {
        // CONMEM beats MAPRAM: the EEPROM; with MAPRAM alone bank 3 stands in for it
        if (_mapram && !Conmem())
            return _ram[3 * kBankSize + addr];
        return _rom[addr];
    }
    return _ram[(_control & kBankMask) * kBankSize + (addr - kBankSize)];
}

uint8_t DivMmcPaging::onRead(uint16_t addr, uint8_t normal, bool, bool)
{
    return Mapped() ? ReadMapped(addr) : normal;
}

void DivMmcPaging::onWrite(uint16_t addr, uint8_t value, bool)
{
    if (!Mapped())
        return;
    if (addr < kRomSize)
        return;  // the EEPROM (jumper E) and, with MAPRAM, bank 3 are read-only here
    if (_mapram && (_control & kBankMask) == 3)
        return;  // MAPRAM write-protects bank 3 wherever it shows
    _ram[(_control & kBankMask) * kBankSize + (addr - kBankSize)] = value;
}

/// endregion

/// region <Automap>

void DivMmcPaging::BeforeMachineM1(uint16_t address)
{
    // #3D00-#3DFF: the TR-DOS entry maps in at once, the opcode at #3Dxx already comes from the board
    if (_automapEnabled && _hasRom && (address >> 8) == 0x3D)
        _automap = true;
}

void DivMmcPaging::OnMachineM1(uint16_t address)
{
    if (!_automapEnabled || !_hasRom)
        return;
    switch (address)
    {
        case 0x0000:
        case 0x0008:
        case 0x0038:
        case 0x0066:
        case 0x04C6:
        case 0x0562:
            _automap = true;  // after this fetch: the instruction at the entry still came from the Spectrum ROM
            return;
        default:
            break;
    }
    if (address >= 0x1FF8 && address <= 0x1FFF)
        _automap = false;
}

/// endregion
