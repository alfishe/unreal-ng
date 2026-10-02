#include "z84sio.h"

void Z84Sio::Reset()
{
    ResetChannel(0);
    ResetChannel(1);
}

void Z84Sio::ResetChannel(uint8_t ch)
{
    Channel& c = _ch[ch & 1];
    const uint8_t vector = c.wr[2];  // WR2 (the vector) survives a channel reset
    c = Channel{};
    c.wr[2] = vector;
}

bool Z84Sio::Receive(uint8_t ch, uint8_t value)
{
    Channel& c = _ch[ch & 1];
    if (c.fifoCount >= kFifoDepth)
    {
        c.overrun = 1;
        return false;
    }
    c.fifo[c.fifoCount++] = value;
    return true;
}

uint8_t Z84Sio::ReadData(uint8_t ch)
{
    Channel& c = _ch[ch & 1];
    if (c.fifoCount == 0)
        return c.lastData;
    c.lastData = c.fifo[0];
    for (uint8_t i = 1; i < c.fifoCount; i++)
        c.fifo[i - 1] = c.fifo[i];
    c.fifoCount--;
    return c.lastData;
}

void Z84Sio::WriteData(uint8_t ch, uint8_t value)
{
    if (_transmit)
        _transmit(ch & 1, value);
}

uint8_t Z84Sio::ReadControl(uint8_t ch)
{
    Channel& c = _ch[ch & 1];
    const uint8_t reg = c.pointer;
    c.pointer = 0;
    switch (reg)
    {
        case 0:
            // bit 0 Rx character available, bit 2 Tx buffer empty (transmit is instant)
            return static_cast<uint8_t>((c.fifoCount ? 0x01 : 0x00) | 0x04);
        case 1:
            return static_cast<uint8_t>(0x01 | (c.overrun ? 0x20 : 0x00));  // all sent; overrun
        case 2:
            return _ch[1].wr[2];  // the vector (both channels read channel B's WR2 here)
        default:
            return 0x00;
    }
}

void Z84Sio::WriteControl(uint8_t ch, uint8_t value)
{
    Channel& c = _ch[ch & 1];
    if (c.pointer != 0)
    {
        c.wr[c.pointer & 7] = value;
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
        case 6:  // error reset
            c.overrun = 0;
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
