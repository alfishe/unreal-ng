#include "stdafx.h"

#include "zcontrollerspi.h"

#include "emulator/io/spi/spidevice.h"

void ZControllerSpi::SetDevice(SpiDevice* device)
{
    _device = device;
    if (_device)
        _device->select(IsSelected());
}

void ZControllerSpi::Reset()
{
    SetState(State{});
}

void ZControllerSpi::WriteConfig(uint8_t value)
{
    const uint8_t csN = (value >> 1) & 0x01;
    if (csN == _state.csN)
        return;
    _state.csN = csN;
    if (_device)
        _device->select(IsSelected());
}

void ZControllerSpi::WriteData(uint8_t value)
{
    _state.rxLatch = Exchange(value);
}

uint8_t ZControllerSpi::ReadData()
{
    const uint8_t previous = _state.rxLatch;
    _state.rxLatch = Exchange(0xFF);
    return previous;
}

void ZControllerSpi::SetState(const State& state)
{
    _state = state;
    if (_device)
        _device->select(IsSelected());
}

uint8_t ZControllerSpi::Exchange(uint8_t mosi)
{
    // The FPGA clocks every byte out whether or not the card is selected; a
    // deselected card ignores it and leaves MISO pulled up
    return _device ? _device->exchange(mosi) : 0xFF;
}
