// z84sio.cpp - the Z84C15's SIO, asynchronous mode (z84c15.h; Zilog Z80 SIO data sheet).

#include "z84c15.h"

namespace Z84Lib
{

void Z84Sio::Reset()
{
    _version++;  // StateVersion
    ResetChannel(0);
    ResetChannel(1);
}

void Z84Sio::ResetChannel(uint8_t ch)
{
    _version++;  // StateVersion
    Channel& c = _ch[ch & 1];
    const uint8_t vector = c.wr[2];  // WR2 (the vector) survives a channel reset
    c = Channel{};
    c.wr[2] = vector;
}

void Z84Sio::LatchTopStatus(Channel& c)
{
    if (c.fifoCount && (c.overrun & EntryFlag(0)))
        c.overrun |= kOverrunLatch;
}

bool Z84Sio::Receive(uint8_t ch, uint8_t value)
{
    _version++;  // StateVersion
    Channel& c = _ch[ch & 1];
    if (c.fifoCount >= kFifoDepth)
    {
        // The completed character overwrites the newest one in the FIFO and carries the overrun flag
        c.fifo[kFifoDepth - 1] = value;
        c.overrun |= EntryFlag(kFifoDepth - 1);
        return false;
    }
    c.overrun &= static_cast<uint8_t>(~EntryFlag(c.fifoCount));
    c.fifo[c.fifoCount++] = value;
    if (c.rxFirstArmed && ((c.wr[1] >> 3) & 3) == 1)
    {
        c.rxFirstArmed = 0;
        c.rxFirstIp = 1;
    }
    return true;
}

uint8_t Z84Sio::ReadData(uint8_t ch)
{
    _version++;  // StateVersion
    Channel& c = _ch[ch & 1];
    c.rxFirstIp = 0;
    if (c.fifoCount == 0)
        return c.lastData;
    c.lastData = c.fifo[0];
    // Interrupt on the first character: a special receive condition holds the FIFO until Error Reset
    if (((c.wr[1] >> 3) & 3) == 1 && (c.overrun & kOverrunLatch))
        return c.lastData;
    for (uint8_t i = 1; i < c.fifoCount; i++)
        c.fifo[i - 1] = c.fifo[i];
    c.fifoCount--;
    // The entry flags move with their characters; the new top's status reaches RR1
    const uint8_t entries = static_cast<uint8_t>((c.overrun >> 1) & 0x07);
    c.overrun = static_cast<uint8_t>((c.overrun & kOverrunLatch) | (((entries >> 1) & 0x03) << 1));
    LatchTopStatus(c);
    return c.lastData;
}

void Z84Sio::WriteData(uint8_t ch, uint8_t value)
{
    _version++;  // StateVersion
    if (_transmit)
        _transmit(ch & 1, value);
}

uint8_t Z84Sio::ReadControl(uint8_t ch)
{
    _version++;  // StateVersion
    Channel& c = _ch[ch & 1];
    const uint8_t reg = c.pointer;
    c.pointer = 0;
    switch (reg)
    {
        case 0:
            // bit 0 Rx character available, bit 2 Tx buffer empty (transmit is instant)
            return static_cast<uint8_t>((c.fifoCount ? 0x01 : 0x00) | 0x04);
        case 1:
            return static_cast<uint8_t>(0x01 | ((c.overrun & kOverrunLatch) ? 0x20 : 0x00));  // all sent; overrun
        case 2:
            // The vector (both channels read channel B's WR2 here), status-modified when WR1B bit 2 is set
            if (_ch[1].wr[1] & 0x04)
            {
                for (uint8_t i = 0; i < 2; i++)
                {
                    if (RxIp(i))
                        return RxVector(i);
                }
            }
            return _ch[1].wr[2];
        default:
            return 0x00;
    }
}

void Z84Sio::WriteControl(uint8_t ch, uint8_t value)
{
    _version++;  // StateVersion
    Channel& c = _ch[ch & 1];
    if (c.pointer != 0)
    {
        c.wr[c.pointer & 7] = value;
        if ((c.pointer & 7) == 1 && ((value >> 3) & 3) == 1)
            c.rxFirstArmed = 1;  // mode 01: the first character from now on interrupts
        c.pointer = 0;
        return;
    }

    // WR0: bits 2-0 point at the next register, bits 5-3 a command
    c.wr[0] = value;
    c.pointer = value & 0x07;
    switch ((value >> 3) & 0x07)
    {
        case 3:  // channel reset
            ResetChannel(ch);
            break;
        case 4:  // enable INT on next Rx character
            c.rxFirstArmed = 1;
            break;
        case 6:  // error reset: the latch clears; characters still in the FIFO keep their own flags
            c.overrun &= static_cast<uint8_t>(~kOverrunLatch);
            break;
        case 7:  // return from INT (channel A only): the SIO's RETI
            if ((ch & 1) == 0 && onReturnFromInt)
                onReturnFromInt();
            break;
        default:
            break;
    }
}

uint8_t Z84Sio::Read(uint8_t port)
{
    const uint8_t ch = (port >> 1) & 1;
    return (port & 1) ? ReadControl(ch) : ReadData(ch);
}

void Z84Sio::Write(uint8_t port, uint8_t value)
{
    const uint8_t ch = (port >> 1) & 1;
    if (port & 1)
        WriteControl(ch, value);
    else
        WriteData(ch, value);
}

bool Z84Sio::RxIp(uint8_t ch) const
{
    const Channel& c = _ch[ch & 1];
    switch ((c.wr[1] >> 3) & 3)
    {
        case 1: return c.rxFirstIp != 0;
        case 2:
        case 3: return c.fifoCount != 0;
        default: return false;
    }
}

uint8_t Z84Sio::RxVector(uint8_t ch) const
{
    const uint8_t base = _ch[1].wr[2];
    if (!(_ch[1].wr[1] & 0x04))
        return base;
    // Status affects vector, bits 3-1: channel B receive 010, channel A receive 110
    const uint8_t code = (ch & 1) ? 0x02 : 0x06;
    return static_cast<uint8_t>((base & 0xF1) | (code << 1));
}

}  // namespace Z84Lib
