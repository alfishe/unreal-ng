#include "stdafx.h"

#include "zcontrollerspi.h"

#include "emulator/bus/bustap.h"
#include "emulator/io/spi/spidevice.h"

void ZControllerSpi::SetDevice(SpiDevice* device)
{
    _devices[0] = device;
    if (device)
        device->select(IsSlotSelected(0));
}

void ZControllerSpi::AttachDevice(uint8_t slot, SpiDevice* device, uint8_t configMask, bool activeHigh)
{
    if (slot == 0 || slot >= kSlots)
        return;
    _devices[slot] = device;
    _masks[slot] = device ? configMask : 0;
    _activeHigh[slot] = activeHigh;
    _multi = false;
    for (uint8_t i = 1; i < kSlots; i++)
        _multi = _multi || _devices[i] != nullptr;
    if (device)
        device->select(IsSlotSelected(slot));
}

void ZControllerSpi::Reset()
{
    SetState(State{});
}

void ZControllerSpi::WriteConfig(uint8_t value)
{
    const uint8_t previous = _state.config;
    _state.config = value;
    const bool report = _tap && _tap->Active();
    for (uint8_t slot = 0; slot < kSlots; slot++)
    {
        const bool now = SelectedBy(slot, value);
        if (now == SelectedBy(slot, previous))
            continue;
        if (_devices[slot])
            _devices[slot]->select(now);
        // Slot 0 is the SD socket, reported even when empty; the exchange is instant, never mid-byte
        if (report && (slot == 0 || _devices[slot])) [[unlikely]]
            _tap->SpiSelect(slot, now);
    }
}

void ZControllerSpi::WriteData(uint8_t value)
{
    _state.rxLatch = Exchange(value);
    if (_tap && _tap->Active()) [[unlikely]]
        Report(value, _state.rxLatch, false);
}

uint8_t ZControllerSpi::ReadData()
{
    const uint8_t previous = _state.rxLatch;
    _state.rxLatch = Exchange(0xFF);
    if (_tap && _tap->Active()) [[unlikely]]
        Report(0xFF, _state.rxLatch, true);
    return previous;
}

uint8_t ZControllerSpi::DrivingSlot() const
{
    // The FPGA's MISO mux: a selected device in slot 1..3 wins over the SD card (top.v:1173-1178)
    for (uint8_t slot = kSlots - 1; slot > 0; slot--)
        if (IsSlotSelected(slot))
            return slot;
    return IsSlotSelected(0) ? 0 : BusTap::kNoSlot;
}

void ZControllerSpi::Report(uint8_t mosi, uint8_t miso, bool read)
{
    _tap->SpiByte(DrivingSlot(), mosi, miso, read);
}

void ZControllerSpi::SetState(const State& state)
{
    _state = state;
    AnnounceAll();
}

void ZControllerSpi::AnnounceAll()
{
    for (uint8_t slot = 0; slot < kSlots; slot++)
        if (_devices[slot])
            _devices[slot]->select(IsSlotSelected(slot));
}

uint8_t ZControllerSpi::Exchange(uint8_t mosi)
{
    // The FPGA clocks every byte out whether or not the card is selected; a
    // deselected card ignores it and leaves MISO pulled up
    const uint8_t sd = _devices[0] ? _devices[0]->exchange(mosi) : 0xFF;
    if (!_multi) [[likely]]
        return sd;

    // A selected device in slot 1..3 drives MISO (top.v:1173-1178)
    uint8_t miso = sd;
    for (uint8_t slot = 1; slot < kSlots; slot++)
        if (_devices[slot] && IsSlotSelected(slot))
            miso = _devices[slot]->exchange(mosi);
    return miso;
}
