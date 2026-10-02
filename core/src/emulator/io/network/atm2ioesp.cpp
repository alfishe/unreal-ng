#include "emulator/io/network/atm2ioesp.h"

#include "emulator/io/serial/serialpeer.h"

Atm2IoEsp::Atm2IoEsp(EmulatorContext* context, std::unique_ptr<ISerialPeer> peer, uint8_t baseAddress)
    : _base(static_cast<uint8_t>(baseAddress & 0xF8)),
      // A genuine TL16C550C at 1.8432 MHz; CT2..CT0 pick the register (the "port" handed to the ComPort below)
      _com(context, Uart16550::DefaultParams(Uart16550::Flavor::Chip16550), std::move(peer),
           [](uint16_t port) { return static_cast<int>(port & 0x07); })
{
}

uint8_t Atm2IoEsp::Read(uint8_t busAddress)
{
    return _com.portDeviceInMethod(busAddress & 0x07);
}

void Atm2IoEsp::Write(uint8_t busAddress, uint8_t value)
{
    _com.portDeviceOutMethod(busAddress & 0x07, value);
}
