#include "z84ctc.h"

void Z84Ctc::Reset()
{
    for (ChannelState& ch : _ch)
        ch = ChannelState{};
}

uint8_t Z84Ctc::Read(uint8_t channel)
{
    const ChannelState& ch = _ch[channel & 3];
    const uint32_t period = ch.timeConstant ? ch.timeConstant : 256u;
    if (!ch.running || (ch.control & 0x40))
        return static_cast<uint8_t>(period);  // stopped, or a counter without input: the loaded value

    const uint32_t prescaler = (ch.control & 0x20) ? 256u : 16u;
    const uint64_t steps = (Now() - ch.loadClock) / prescaler;
    const uint32_t count = period - static_cast<uint32_t>(steps % period);
    return static_cast<uint8_t>(count);
}

void Z84Ctc::Write(uint8_t channel, uint8_t value)
{
    ChannelState& ch = _ch[channel & 3];
    if (ch.awaitingConstant)
    {
        ch.timeConstant = value;
        ch.awaitingConstant = 0;
        ch.running = 1;
        ch.loadClock = Now();
        return;
    }

    if (value & 0x01)
    {
        ch.control = value;
        if (value & 0x02)
            ch.running = 0;  // software reset: stops until the next time constant
        if (value & 0x04)
            ch.awaitingConstant = 1;
    }
    else if ((channel & 3) == 0)
    {
        _vector = value & 0xF8;
    }
}
