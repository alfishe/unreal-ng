// z84pio.cpp - the Z84C15's PIO (z84c15.h; Zilog Z80 PIO data sheet).

#include "z84c15.h"

namespace Z84Lib
{

void Z84Pio::Reset()
{
    _version++;  // StateVersion
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

void Z84Pio::SetInputs(uint8_t p, uint8_t value)
{
    _version++;  // StateVersion
    _port[p & 1].inputs = value;
    Evaluate(p);
}

/// Mode 3: the monitored lines (input, not masked) against the AND / OR, high / low
/// condition; a request on the edge where it becomes true
void Z84Pio::Evaluate(uint8_t p)
{
    _version++;  // StateVersion
    Port& port = _port[p & 1];
    if (port.mode != 3)
    {
        port.condition = 0;
        return;
    }
    const uint8_t monitored = static_cast<uint8_t>(port.direction & ~port.mask);
    const uint8_t active = static_cast<uint8_t>(((port.intControl & 0x20) ? port.inputs : ~port.inputs) & monitored);
    const bool andMode = (port.intControl & 0x40) != 0;
    const bool condition = monitored != 0 && (andMode ? active == monitored : active != 0);
    if (condition && !port.condition && (port.intControl & 0x80))
        port.ip = 1;
    port.condition = condition ? 1 : 0;
}

void Z84Pio::WriteControl(uint8_t p, uint8_t value)
{
    _version++;  // StateVersion
    Port& port = _port[p & 1];
    if (port.next == 1)
    {
        port.direction = value;
        port.next = 0;
        Evaluate(p);
        return;
    }
    if (port.next == 2)
    {
        port.mask = value;
        port.next = 0;
        Evaluate(p);
        return;
    }

    if ((value & 0x0F) == 0x0F)
    {
        port.mode = static_cast<uint8_t>(value >> 6);
        if (port.mode == 3)
            port.next = 1;
        Evaluate(p);
    }
    else if ((value & 0x0F) == 0x07)
    {
        port.intControl = value;
        if (value & 0x10)
            port.next = 2;
        if (!(value & 0x80))
            port.ip = 0;
        Evaluate(p);
    }
    else if ((value & 0x0F) == 0x03)
    {
        port.intControl = static_cast<uint8_t>((port.intControl & 0x7F) | (value & 0x80));  // enable / disable only
        if (!(value & 0x80))
            port.ip = 0;
    }
    else if (!(value & 0x01))
    {
        port.vector = value;
    }
}

uint8_t Z84Pio::Read(uint8_t port)
{
    _version++;  // StateVersion
    const uint8_t p = (port >> 1) & 1;
    if (port & 1)
        return 0xFF;  // control registers are write-only
    return ReadData(p);
}

void Z84Pio::Write(uint8_t port, uint8_t value)
{
    _version++;  // StateVersion
    const uint8_t p = (port >> 1) & 1;
    if (port & 1)
        WriteControl(p, value);
    else
        _port[p].output = value;
}

}  // namespace Z84Lib
