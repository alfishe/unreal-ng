#include "stdafx.h"

#include "divmmcpaging.h"

#include <cstdio>
#include <cstring>

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "debugger/ttd/engine/ttdregiontracker.h"
#include "emulator/slots/portowners.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{

constexpr bool IsEntry(uint16_t address)
{
    switch (address)
    {
        case 0x0000:
        case 0x0008:
        case 0x0038:
        case 0x0066:
        case 0x04C6:
        case 0x0562:
            return true;
        default:
            return false;
    }
}

constexpr bool IsTrDosEntry(uint16_t address)
{
    return (address >> 8) == 0x3D;
}

constexpr bool IsOffArea(uint16_t address)
{
    return address >= 0x1FF8 && address <= 0x1FFF;
}

}  // namespace

DivMmcPaging::DivMmcPaging(EmulatorContext* context) : _context(context)
{
    windowStart = 0x0000;
    windowEnd = 0x4000;
    observesReads = true;
    _rom = std::make_unique<uint8_t[]>(kRomSize);
    _ram = std::make_unique<uint8_t[]>(kRamSize);
    std::memset(_rom.get(), 0xFF, kRomSize);
    std::memset(_ram.get(), 0x00, kRamSize);
    // The exchange is instant here; the byte time (16 T at 3.5 MHz) only scales exports
    _spiTap.SetByteTimeNs(16.0 * 1000.0 / 3.5);
    for (unsigned i = 0; i < 2; i++)
    {
        _spiTap.SetDevice(static_cast<uint8_t>(i), i ? "sd1" : "sd0");
        _card[i].SetBusTap(&_spiTap, static_cast<uint8_t>(i));
        _card[i].SetClock([this]() { return _context && _context->pPortDecoder ? _context->pPortDecoder->EmulatedMicroseconds() : 0; });
        _card[i].AttachHistory(_context, [context = _context, i]() {
            return slots::PortOwnerName(context, 0x00E7, "divmmc") + (i ? ".sd1" : ".sd0");
        });
    }
}

DivMmcPaging::~DivMmcPaging()
{
    Detach();
}

bool DivMmcPaging::Attach(bool ownPorts)
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
    _ownPorts = ownPorts;
    if (_ownPorts && (!decoder->RegisterFullDecodeLowBytePort(kPortControl, this) ||
                      !decoder->RegisterFullDecodeLowBytePort(kPortSpiSelect, this) ||
                      !decoder->RegisterFullDecodeLowBytePort(kPortSpiData, this)))
    {
        decoder->UnregisterFullDecodeLowBytePort(kPortControl, this);
        decoder->UnregisterFullDecodeLowBytePort(kPortSpiSelect, this);
        decoder->UnregisterFullDecodeLowBytePort(kPortSpiData, this);
        LOGERROR("DivMmcPaging: ports #E3 / #E7 / #EB are taken");
        return false;
    }
    // One try tells a machine with no room for the overlay; from then on it is installed only while mapped
    if (!core->AddBusOverlay(this))
    {
        if (_ownPorts)
        {
            decoder->UnregisterFullDecodeLowBytePort(kPortControl, this);
            decoder->UnregisterFullDecodeLowBytePort(kPortSpiSelect, this);
            decoder->UnregisterFullDecodeLowBytePort(kPortSpiData, this);
        }
        return false;
    }
    _overlayInstalled = true;
    _core = core;
    _savedHookEnd = z80->machineM1HookEnd;
    z80->machineM1Hook = this;
    _hookInstalled = true;
    _spiTap.Attach(_context);
    _attached = true;
    UpdateBus();
    return true;
}

void DivMmcPaging::Detach()
{
    if (!_attached)
        return;
    _attached = false;
    _spiTap.Detach();
    // The core that took the board (the machine's release clears pCore before the cards go)
    Core* core = _core;
    _core = nullptr;
    Z80* z80 = core->GetZ80();
    if (_hookInstalled && z80->machineM1Hook == this)
    {
        z80->machineM1Hook = nullptr;
        z80->machineM1HookEnd = _savedHookEnd;
    }
    _hookInstalled = false;
    if (_overlayInstalled)
        core->RemoveBusOverlay(this);
    _overlayInstalled = false;
    if (PortDecoder* decoder = _context->pPortDecoder; decoder && _ownPorts)
    {
        decoder->UnregisterFullDecodeLowBytePort(kPortControl, this);
        decoder->UnregisterFullDecodeLowBytePort(kPortSpiSelect, this);
        decoder->UnregisterFullDecodeLowBytePort(kPortSpiData, this);
    }
    _mapcond = false;
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
    const bool larger = size == kRomSize && std::fgetc(file) != EOF;
    std::fclose(file);
    return !larger && LoadRom(buffer, size);
}

void DivMmcPaging::Reset()
{
    // divmmc.vhd: /RESET clears card(0), card(1) (deselected) and the SPI engine; #E3 and the latch have no reset
    SpiSelect(0xFF);
    _spiRx = 0xFF;
}

void DivMmcPaging::PowerOn()
{
    Reset();
    _control = 0;
    _mapram = false;
    _mapcond = false;
    _automap = false;
    for (SdCardSpi& card : _card)
        card.powerOn();
    UpdateBus();
}

void DivMmcPaging::UnmapForProgram()
{
    _control &= static_cast<uint8_t>(~kConmem);
    _mapcond = false;
    _automap = false;
    UpdateBus();
}

void DivMmcPaging::SetAutomapMode(AutomapMode mode)
{
    _mode = mode;
    UpdateBus();
}

void DivMmcPaging::UpdateBus()
{
    if (!_attached)
        return;
    Core* core = _core;
    const bool mapped = Mapped();
    if (mapped && !_overlayInstalled)
    {
        // Core::AddBusOverlay logs a refusal (all overlay slots taken since Attach): the board then reads as absent
        _overlayInstalled = core->AddBusOverlay(this);
    }
    else if (!mapped && _overlayInstalled)
    {
        core->RemoveBusOverlay(this);
        _overlayInstalled = false;
    }
    // A latch change waits for the next opcode fetch wherever it is; otherwise only the automap addresses matter
    if (_hookInstalled)
        core->GetZ80()->machineM1HookEnd = _mapcond != _automap ? 0x10000 : kHookEnd;
}

/// region <Ports>

uint8_t DivMmcPaging::portDeviceInMethod(uint16_t port)
{
    if (static_cast<uint8_t>(port) == kPortSpiData)
    {
        // A read clocks a transfer of #FF; the byte of the previous one is what the CPU gets
        const uint8_t result = _spiRx;
        _spiRx = _selected >= 0 ? _card[_selected].exchange(0xFF) : 0xFF;
        if (_spiTap.Active()) [[unlikely]]
            _spiTap.SpiByte(TapSlot(), 0xFF, _spiRx, true);
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
            UpdateBus();
            break;
        case kPortSpiSelect:
            SpiSelect(value);
            break;
        case kPortSpiData:
            _spiRx = _selected >= 0 ? _card[_selected].exchange(value) : 0xFF;
            if (_spiTap.Active()) [[unlikely]]
                _spiTap.SpiByte(TapSlot(), value, _spiRx, false);
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
    const bool tap = _spiTap.Active();
    if (_selected >= 0)
    {
        _card[_selected].select(false);
        if (tap) [[unlikely]]
            _spiTap.SpiSelect(static_cast<uint8_t>(_selected), false);
    }
    _selected = line;
    if (line >= 0)
    {
        _card[line].select(true);
        if (tap) [[unlikely]]
            _spiTap.SpiSelect(static_cast<uint8_t>(line), true);
    }
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
        return;  // the EEPROM (writes need the jumper: not modelled) and, with MAPRAM, bank 3 are read-only here
    if (_mapram && !Conmem() && (_control & kBankMask) == 3)
        return;  // divmmc.vhd ramwr: MAPRAM write-protects bank 3 unless CONMEM is set
    const size_t offset = (_control & kBankMask) * kBankSize + (addr - kBankSize);
    _ram[offset] = value;
    if (_ramTracker) [[unlikely]]
        _ramTracker->Mark(offset);
}

/// endregion

/// region <Automap>

bool DivMmcPaging::BasicRomPaged() const
{
    Memory* memory = _context ? _context->pMemory : nullptr;
    return memory && memory->IsBank0ROM() && memory->IsCurrentROM48k();
}

DivMmcPaging::Latch DivMmcPaging::NextLatch(uint16_t address) const
{
    // No firmware and no MAPRAM: a board with nothing to map stays out
    if (!_hasRom && !_mapram)
        return {_mapcond, _automap};
    bool entry = address < kHookEnd && IsEntry(address);
    bool trdos = address < kHookEnd && IsTrDosEntry(address);
    if ((entry || trdos) && _mode == AutomapMode::Rom3 && !BasicRomPaged())
        entry = trdos = false;
    Latch next;
    next.mapcond = entry || trdos || (_mapcond && !IsOffArea(address));
    next.automap = _mapcond || trdos;
    return next;
}

void DivMmcPaging::BeforeMachineM1(uint16_t address)
{
    // The falling /MREQ of the fetch: #3Dxx maps this fetch in already, the rest acts from the next one
    const Latch next = NextLatch(address);
    if (next.mapcond == _mapcond && next.automap == _automap)
        return;
    _mapcond = next.mapcond;
    _automap = next.automap;
    UpdateBus();
}

void DivMmcPaging::OnMachineM1(uint16_t address)
{
    (void)address;
}

bool DivMmcPaging::RepeatM1IsInert(uint16_t address) const
{
    const Latch next = NextLatch(address);
    return next.mapcond == _mapcond && next.automap == _automap;
}

/// endregion

/// region <State>

void DivMmcPaging::SaveState(uint8_t* dst) const
{
    std::memset(dst, 0, kStateSize);
    dst[0] = kStateVersion;
    dst[1] = _control;
    dst[2] = _mapram ? 1 : 0;
    dst[3] = _mapcond ? 1 : 0;
    dst[4] = _automap ? 1 : 0;
    dst[5] = static_cast<uint8_t>(_mode);
    dst[6] = _selected >= 0 ? static_cast<uint8_t>(_selected) : 0xFF;
    dst[7] = _spiRx;
    _card[0].saveState(dst + kStateCardOffset);
    _card[1].saveState(dst + kStateCardOffset + SdCardSpi::STATE_SIZE);
}

bool DivMmcPaging::LoadState(const uint8_t* src)
{
    if (src == nullptr || src[0] != kStateVersion)
        return false;
    // The cards first: the select below re-announces the line to the selected one (a deselect would drop the
    // command it is receiving, so the others are left as their blobs say)
    _card[0].loadState(src + kStateCardOffset);
    _card[1].loadState(src + kStateCardOffset + SdCardSpi::STATE_SIZE);
    _control = static_cast<uint8_t>(src[1] & (kConmem | kBankMask));
    _mapram = src[2] != 0;
    _mapcond = src[3] != 0;
    _automap = src[4] != 0;
    // The mode is the card option (the session's configuration): it is not taken from the blob
    _selected = src[6] <= 1 ? src[6] : -1;
    if (_selected >= 0)
        _card[_selected].select(true);
    _spiRx = src[7];
    UpdateBus();
    return true;
}

/// endregion
