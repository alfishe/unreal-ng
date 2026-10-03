#include "emulator/io/network/atm2ioesp.h"

#include "emulator/io/serial/serialpeer.h"

namespace
{
/// A genuine TL16C550C at 1.8432 MHz. On the card only CTS' comes from the ESP (its RTS); DCD' and DSR' are
/// tied to GND, RI' to +5 V, INTRPT / OUT1 / OUT2 / DTR' go nowhere (traced on the Rev 2 gerbers)
Uart16550::Params CardUart()
{
    Uart16550::Params params = Uart16550::DefaultParams(Uart16550::Flavor::Chip16550);
    params.ctsOnly = true;
    return params;
}
}  // namespace

Atm2IoEsp::Atm2IoEsp(EmulatorContext* context, std::unique_ptr<ISerialPeer> peer, uint8_t baseAddress)
    : _base(static_cast<uint8_t>(baseAddress & 0xF8)),
      // CT2..CT0 pick the register (the "port" handed to the ComPort below)
      _com(context, CardUart(), std::move(peer), [](uint16_t port) { return static_cast<int>(port & 0x07); })
{
}

uint8_t Atm2IoEsp::Read(uint16_t offset)
{
    return _com.portDeviceInMethod(offset & 0x07);
}

void Atm2IoEsp::Write(uint16_t offset, uint8_t value)
{
    _com.portDeviceOutMethod(offset & 0x07, value);
}

uint8_t Atm2IoEsp::Peek(uint16_t offset) const
{
    // The registers as the UART shows them (the receive buffer is not popped: its byte is not in the view)
    const Uart16550::View v = _com.Uart().GetView();
    const bool dlab = (v.lcr & 0x80) != 0;
    switch (offset & 0x07)
    {
        case 0: return dlab ? static_cast<uint8_t>(v.divisor & 0xFF) : 0xFF;
        case 1: return dlab ? static_cast<uint8_t>(v.divisor >> 8) : v.ier;
        case 2: return v.iir;
        case 3: return v.lcr;
        case 4: return v.mcr;
        case 5: return v.lsr;
        case 6: return v.msr;
        default: return v.scr;
    }
}
