// z84ctc.cpp - the Z84C15's CTC (z84c15.h; Zilog Z80 CTC data sheet).

#include "z84c15.h"

namespace Z84Lib
{

void Z84Ctc::Reset()
{
    for (ChannelState& ch : _ch)
        ch = ChannelState{};
}

uint64_t Z84Ctc::ZeroCounts(const ChannelState& ch, uint64_t now) const
{
    const uint64_t period = ch.timeConstant ? ch.timeConstant : 256u;
    const uint64_t prescaler = (ch.control & 0x20) ? 256u : 16u;
    return (now - ch.loadClock) / (prescaler * period);
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
        ch.zeroCounts = 0;
        return;
    }

    if (value & 0x01)
    {
        const bool wasEnabled = (ch.control & 0x80) != 0;
        ch.control = value;
        if (value & 0x02)
            ch.running = 0;  // software reset: stops until the next time constant
        if (value & 0x04)
            ch.awaitingConstant = 1;
        if (!(value & 0x80))
            ch.ip = 0;  // interrupt disabled: a pending request is dropped
        else if (!wasEnabled && ch.running && !(value & 0x40))
            ch.zeroCounts = ZeroCounts(ch, Now());  // only zero counts from now on request
    }
    else if ((channel & 3) == 0)
    {
        _vector = value & 0xF8;
    }
}

void Z84Ctc::Poll()
{
    bool any = false;
    for (const ChannelState& ch : _ch)
        any = any || ((ch.control & 0x80) && ch.running && !(ch.control & 0x40));
    if (!any)
        return;

    const uint64_t now = Now();
    for (ChannelState& ch : _ch)
    {
        if (!(ch.control & 0x80) || !ch.running || (ch.control & 0x40))
            continue;
        const uint64_t counts = ZeroCounts(ch, now);
        if (counts > ch.zeroCounts)
        {
            ch.zeroCounts = counts;
            ch.ip = 1;
        }
    }
}

}  // namespace Z84Lib
