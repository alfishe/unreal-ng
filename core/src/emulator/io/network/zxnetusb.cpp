#include "emulator/io/network/zxnetusb.h"

#include <cstring>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/io/network/virtualnetwork.h"

ZxNetUsb::ZxNetUsb(VirtualNetwork* network, Core* core) : _chip(network), _network(network), _core(core)
{
    _chip.onNetEvent = [this]() { UpdateIntLine(); };
    Reset();
}

ZxNetUsb::~ZxNetUsb()
{
    // An unplugged card releases /INT
    if (_core && _core->GetZ80())
        _core->GetZ80()->SetDeviceIntLine(Z80::kDeviceIntZxNetUsb, false);
    DetachFromPorts();
    if (_core && _windowInstalled)
        _core->RemoveBusOverlay(&_window);
}

void ZxNetUsb::UpdateWindow()
{
    const bool wanted = ChipInMemory() && _core;
    if (wanted)
    {
        _window.windowStart = static_cast<uint16_t>((_p82 & 0x03) << 14);
        _window.windowEnd = static_cast<uint32_t>(_window.windowStart) + 0x4000;
        if (!_windowInstalled)
            _windowInstalled = _core->AddBusOverlay(&_window);
    }
    else if (_windowInstalled)
    {
        _core->RemoveBusOverlay(&_window);
        _windowInstalled = false;
    }
}

uint16_t ZxNetUsb::WindowChipAddress(uint16_t address) const
{
    const uint16_t offset = address & 0x3FFF;
    const uint16_t invert = (_p82 & kModeInvertA0) ? 1 : 0;
    if (offset < 0x2000)
        return static_cast<uint16_t>((offset & 0x03FF) ^ invert);          // the chip's 1 KB space, mirrored
    const uint16_t socket = static_cast<uint16_t>((offset >> 9) & 0x07);
    const uint16_t fifo = offset < 0x3000 ? 0x2E : 0x30;                   // TX / RX FIFO register pair
    return static_cast<uint16_t>(0x200 + socket * 0x40 + fifo + ((offset & 1) ^ invert));
}

uint8_t ZxNetUsb::RomWindow::onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged)
{
    (void)isExecution;
    // The card answers reads only while ROM is selected there (/CSROM): the
    // #0000 window with ROM paged in
    if (!_card.ChipRunning() || windowStart != 0 || !romPaged)
        return normal;
    const uint8_t value = _card._chip.Read(_card.WindowChipAddress(addr));
    _card.UpdateIntLine();
    return value;
}

void ZxNetUsb::RomWindow::onWrite(uint16_t addr, uint8_t value, bool romPaged)
{
    (void)romPaged;
    if (_card.ChipRunning())
    {
        _card._chip.Write(_card.WindowChipAddress(addr), value);
        _card.UpdateIntLine();
    }
}

bool ZxNetUsb::AttachToPorts(PortDecoder* decoder)
{
    DetachFromPorts();
    if (!decoder)
        return false;
    if (!decoder->RegisterFullDecodeLowBytePort(kPortLowByte, this))
        return false;
    _decoder = decoder;
    return true;
}

void ZxNetUsb::DetachFromPorts()
{
    if (_decoder)
    {
        _decoder->UnregisterFullDecodeLowBytePort(kPortLowByte, this);
        _decoder = nullptr;
    }
}

void ZxNetUsb::Reset()
{
    _p83 = 0;
    _p82 = 0;
    _p81 = 0;
    _chip.Reset();   // held in reset: every socket closed
    UpdateWindow();
    UpdateIntLine();
}

bool ZxNetUsb::InterruptActive() const
{
    const bool w5300Int = ChipRunning() && _chip.InterruptActive() && (_p83 & kCtlW5300IntEna);
    return w5300Int && (_p83 & kCtlZxIntEna);
}

void ZxNetUsb::UpdateIntLine()
{
    if (_core && _core->GetZ80())
        _core->GetZ80()->SetDeviceIntLine(Z80::kDeviceIntZxNetUsb, InterruptActive());
}

uint16_t ZxNetUsb::ChipAddress(uint16_t port) const
{
    const uint16_t invert = (_p82 & kModeInvertA0) ? 1 : 0;
    return static_cast<uint16_t>(((_p81 & 0x0F) << 6) | ((port >> 8) & 0x3E) | (((port >> 8) & 0x01) ^ invert));
}

uint8_t ZxNetUsb::portDeviceInMethod(uint16_t port)
{
    if (port & 0x8000)
    {
        switch ((port >> 8) & 0x03)
        {
            case 3:
            {
                const bool w5300Int = ChipRunning() && _chip.InterruptActive();
                uint8_t value = static_cast<uint8_t>(_p83 & 0x7E);
                if (w5300Int)
                    value |= 0x01;
                if (InterruptActive())
                    value |= 0x80;
                return value;
            }
            case 2:
                return static_cast<uint8_t>(_p82 & kModeSl811Ms);   // VBUS (b7) not present
            case 1:
                return static_cast<uint8_t>(_p81 & 0x0F);
            default:
                return 0xFF;   // SL811 address register: no USB host fitted
        }
    }

    if (!ChipInPorts())
        return 0xFF;           // SL811 data: absent
    if (!ChipRunning())
        return 0xFF;           // W5300 held in reset
    const uint8_t value = _chip.Read(ChipAddress(port));
    UpdateIntLine();
    return value;
}

void ZxNetUsb::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    if (port & 0x8000)
    {
        switch ((port >> 8) & 0x03)
        {
            case 3:
            {
                const bool wasRunning = ChipRunning();
                _p83 = static_cast<uint8_t>(value & 0x7E);
                if (wasRunning != ChipRunning())
                    _chip.Reset();   // /RESET edge: the chip starts (or stops) from its reset state
                UpdateIntLine();     // the enable bits gate the line
                return;
            }
            case 2:
                _p82 = value;
                UpdateWindow();
                return;
            case 1:
                _p81 = value;
                return;
            default:
                return;            // SL811 address: no USB host fitted
        }
    }

    if (ChipInPorts() && ChipRunning())
    {
        _chip.Write(ChipAddress(port), value);
        UpdateIntLine();
    }
}

bool ZxNetUsb::SaveState(netstate::Adapters& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.version = netstate::kVersion;
    out.present = 1;
    out.p83 = _p83;
    out.p82 = _p82;
    out.p81 = _p81;
    bool complete = _chip.SaveState(out);
    if (_network)
        complete = _network->SaveState(out.network) && complete;
    out.incomplete = complete ? 0 : 1;
    return complete;
}

bool ZxNetUsb::LoadState(const netstate::Adapters& in, const W5300::ByteSource& bytes)
{
    if (in.version != netstate::kVersion || !in.present)
        return false;
    _p83 = in.p83;
    _p82 = in.p82;
    _p81 = in.p81;
    UpdateWindow();
    if (_network)
        _network->LoadState(in.network, &_chip);
    const bool complete = _chip.LoadState(in, bytes);
    UpdateIntLine();
    return complete;
}
