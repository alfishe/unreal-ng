#include "z80pio.h"

/// region <Reset and modes>

void Z80Pio::Reset()
{
    ResetPort(_port[0]);
    ResetPort(_port[1]);
}

void Z80Pio::ResetPort(Port& p)
{
    // The peripheral's levels are not the chip's; the vector register is not reset (UM0081 "Reset")
    const uint8_t lines = p.lines;
    const uint8_t vector = p.vector;
    p = Port{};
    p.lines = lines;
    p.vector = vector;
}

void Z80Pio::SetMode(uint8_t index, uint8_t mode)
{
    Port& p = _port[index];
    switch (mode)
    {
        case kModeOutput:
            p.mode = mode;
            p.rdy = true;  // the output register is valid for the peripheral (MAME set_mode)
            break;
        case kModeInput:
            p.mode = mode;
            break;
        case kModeBidirectional:
            if (index == 0)  // port A only; port B ignores the word (MAME: "invalid mode")
                p.mode = mode;
            break;
        case kModeBitControl:
            if (index == 0 || _port[0].mode != kModeBidirectional)
                p.rdy = false;
            p.ie = false;  // until the direction byte is written
            p.match = false;
            p.next = kNextDirection;
            p.mode = mode;
            break;
        default:
            break;
    }
}

/// endregion </Reset and modes>

/// region <CPU side>

uint8_t Z80Pio::PeekData(uint8_t port) const
{
    const Port& p = _port[port & 1];
    switch (p.mode)
    {
        case kModeOutput:
            return p.output;
        case kModeInput:
            return p.stb ? p.input : p.lines;  // while /STB is low the latch is transparent
        case kModeBidirectional:
            return p.input;
        case kModeBitControl:
        default:
            return static_cast<uint8_t>((p.lines & p.direction) | (p.output & ~p.direction));
    }
}

uint8_t Z80Pio::ReadData(uint8_t port)
{
    const uint8_t index = port & 1;
    Port& p = _port[index];
    const uint8_t value = PeekData(index);
    switch (p.mode)
    {
        case kModeInput:
            if (!p.stb)
                p.input = p.lines;
            p.rdy = true;  // the CPU took the byte: the peripheral may strobe the next one
            break;
        case kModeBidirectional:
            _port[1].rdy = true;  // port B's handshake lines serve port A's input side
            break;
        case kModeBitControl:
            p.input = p.lines;
            break;
        default:
            break;
    }
    return value;
}

void Z80Pio::WriteData(uint8_t port, uint8_t value)
{
    Port& p = _port[port & 1];
    p.output = value;
    switch (p.mode)
    {
        case kModeOutput:
        case kModeBidirectional:
            p.rdy = true;  // a new byte for the peripheral
            break;
        default:
            break;
    }
}

void Z80Pio::WriteControl(uint8_t port, uint8_t value)
{
    const uint8_t index = port & 1;
    Port& p = _port[index];
    switch (p.next)
    {
        case kNextDirection:
            p.direction = value;
            p.ie = (p.icw & 0x80) != 0;
            p.next = kNextAny;
            return;
        case kNextMask:
            p.mask = value;
            p.ie = (p.icw & 0x80) != 0;
            p.next = kNextAny;
            return;
        default:
            break;
    }

    if ((value & 0x01) == 0)
    {
        p.vector = value;
        return;
    }
    switch (value & 0x0F)
    {
        case 0x0F:
            SetMode(index, static_cast<uint8_t>(value >> 6));
            break;
        case 0x07:
            p.icw = value;
            if (value & 0x10)
            {
                // The mask follows: no interrupt until it is written, a pending one is dropped (MAME)
                p.ie = false;
                p.ip = false;
                p.match = false;
                p.next = kNextMask;
            }
            else
            {
                p.ie = (value & 0x80) != 0;
            }
            break;
        case 0x03:
            p.icw = static_cast<uint8_t>((value & 0x80) | (p.icw & 0x7F));
            p.ie = (value & 0x80) != 0;
            break;
        default:
            break;  // not a control word of the PIO: ignored
    }
}

/// endregion </CPU side>

/// region <Peripheral side>

void Z80Pio::SetLines(uint8_t port, uint8_t value)
{
    Port& p = _port[port & 1];
    p.lines = value;
    if (p.mode == kModeBitControl)
        EvaluateMatch(p);
}

void Z80Pio::SetStrobe(uint8_t port, bool high)
{
    const uint8_t index = port & 1;
    Port& p = _port[index];
    const bool rising = !p.stb && high;
    const uint8_t mode = (_port[0].mode == kModeBidirectional) ? kModeBidirectional : p.mode;
    switch (mode)
    {
        case kModeOutput:
            if (rising && p.rdy)
            {
                p.ip = true;
                p.rdy = false;
            }
            break;
        case kModeInput:
            if (!high)
            {
                p.input = p.lines;  // /STB low: the lines into the input register
            }
            else if (rising)
            {
                p.ip = true;
                p.rdy = false;
            }
            break;
        case kModeBidirectional:
            // /ASTB strobes the output side, /BSTB the input side (into port A's register); both interrupt as
            // port A / port B at the rising edge
            if (p.rdy)
            {
                if (index == 1 && !high)
                    _port[0].input = _port[0].lines;
                if (rising)
                {
                    p.ip = true;
                    p.rdy = false;
                }
            }
            break;
        default:
            break;  // mode 3 has no handshake
    }
    p.stb = high;
}

void Z80Pio::EvaluateMatch(Port& p)
{
    const uint8_t monitored = static_cast<uint8_t>(~p.mask);
    const uint8_t data =
        static_cast<uint8_t>(((p.lines & p.direction) | (p.output & ~p.direction)) & monitored);
    bool match = false;
    switch (p.icw & 0x60)
    {
        case 0x00:  // OR, active low: any monitored line low
            match = data != monitored;
            break;
        case 0x20:  // OR, active high
            match = data != 0;
            break;
        case 0x40:  // AND, active low: every monitored line low
            match = data == 0;
            break;
        default:  // AND, active high
            match = data == monitored;
            break;
    }
    if (!p.match && match && !p.ius)
        p.ip = true;
    p.match = match;
}

/// endregion </Peripheral side>

/// region <Daisy chain>

uint8_t Z80Pio::DaisyState() const
{
    uint8_t state = 0;
    for (const Port& p : _port)
    {
        if (p.ius)
            return static_cast<uint8_t>(state | kDaisyIeo);  // port B (and the chain below) waits
        if (p.ie && p.ip)
            state |= kDaisyInt;
    }
    return state;
}

bool Z80Pio::Acknowledge(uint8_t& vector)
{
    for (Port& p : _port)
    {
        if (p.ius)
            return false;
        if (p.ie && p.ip)
        {
            p.ip = false;
            p.ius = true;
            vector = p.vector;
            return true;
        }
    }
    return false;
}

void Z80Pio::Reti()
{
    for (Port& p : _port)
    {
        if (p.ius)
        {
            p.ius = false;
            return;
        }
    }
}

/// endregion </Daisy chain>

/// region <State>

void Z80Pio::SaveState(uint8_t* dst) const
{
    uint8_t* q = dst;
    for (const Port& p : _port)
    {
        *q++ = p.mode;
        *q++ = p.next;
        *q++ = p.input;
        *q++ = p.output;
        *q++ = p.direction;
        *q++ = p.mask;
        *q++ = p.icw;
        *q++ = p.vector;
        *q++ = p.lines;
        *q++ = static_cast<uint8_t>((p.ie ? 0x01 : 0) | (p.ip ? 0x02 : 0) | (p.ius ? 0x04 : 0) | (p.rdy ? 0x08 : 0) |
                                    (p.stb ? 0x10 : 0) | (p.match ? 0x20 : 0));
    }
}

void Z80Pio::LoadState(const uint8_t* src)
{
    const uint8_t* q = src;
    for (Port& p : _port)
    {
        p.mode = static_cast<uint8_t>(*q++ & 0x03);
        p.next = *q++;
        if (p.next > kNextMask)
            p.next = kNextAny;
        p.input = *q++;
        p.output = *q++;
        p.direction = *q++;
        p.mask = *q++;
        p.icw = *q++;
        p.vector = *q++;
        p.lines = *q++;
        const uint8_t flags = *q++;
        p.ie = (flags & 0x01) != 0;
        p.ip = (flags & 0x02) != 0;
        p.ius = (flags & 0x04) != 0;
        p.rdy = (flags & 0x08) != 0;
        p.stb = (flags & 0x10) != 0;
        p.match = (flags & 0x20) != 0;
    }
}

/// endregion </State>
