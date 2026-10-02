#include "z84pio.h"

void Z84Pio::Reset()
{
    for (Port& port : _port)
    {
        const uint8_t inputs = port.inputs;
        port = Port{};
        port.inputs = inputs;
    }
}

uint8_t Z84Pio::ReadData(uint8_t p) const
{
    const Port& port = _port[p & 1];
    switch (port.mode)
    {
        case 0:
            return port.output;
        case 3:
            return static_cast<uint8_t>((port.output & ~port.direction) | (port.inputs & port.direction));
        default:
            return port.inputs;
    }
}

void Z84Pio::WriteControl(uint8_t p, uint8_t value)
{
    Port& port = _port[p & 1];
    if (port.next == 1)
    {
        port.direction = value;
        port.next = 0;
        return;
    }
    if (port.next == 2)
    {
        port.mask = value;
        port.next = 0;
        return;
    }

    if ((value & 0x0F) == 0x0F)
    {
        port.mode = static_cast<uint8_t>(value >> 6);
        if (port.mode == 3)
            port.next = 1;
    }
    else if ((value & 0x0F) == 0x07)
    {
        port.intControl = value;
        if (value & 0x10)
            port.next = 2;
    }
    else if ((value & 0x0F) == 0x03)
    {
        port.intControl = static_cast<uint8_t>((port.intControl & 0x7F) | (value & 0x80));  // enable / disable only
    }
    else if (!(value & 0x01))
    {
        port.vector = value;
    }
}

uint8_t Z84Pio::Read(uint8_t port)
{
    const uint8_t p = (port >> 1) & 1;
    if (port & 1)
        return 0xFF;  // control registers are write-only
    return ReadData(p);
}

void Z84Pio::Write(uint8_t port, uint8_t value)
{
    const uint8_t p = (port >> 1) & 1;
    if (port & 1)
        WriteControl(p, value);
    else
        _port[p].output = value;
}
