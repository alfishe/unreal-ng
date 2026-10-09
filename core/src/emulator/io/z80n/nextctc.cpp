#include "stdafx.h"

#include "nextctc.h"

void NextCtc::Reset()
{
    for (Channel& c : _ch)
        c = Channel();
    _vector = 0;
}

/// The system clock at which a running timer channel reaches zero next
uint64_t NextCtc::NextZero(const Channel& c) const
{
    // phase is the clock of the last prescaler wrap (a decrement); the counter reaches 0 after `counter` more wraps
    return c.phase + static_cast<uint64_t>(c.counter) * Prescale(c);
}

void NextCtc::ZeroCount(unsigned channel, uint64_t when)
{
    Channel& c = _ch[channel];
    c.zeros++;
    c.counter = c.tc;
    c.phase = when;
    if ((c.control & kInterrupt) && onInterrupt)
        onInterrupt(channel);
    // ZC/TO feeds the next channel's trigger (channel 3 feeds channel 0)
    Trigger((channel + 1) % kChannels, when);
}

void NextCtc::Advance(uint64_t now)
{
    if (now < _now)
    {
        _now = now;  // the clock was restarted (reset): nothing to catch up
        return;
    }
    // Process the zero counts in time order across the timer channels
    for (;;)
    {
        int next = -1;
        uint64_t nextAt = now + 1;
        for (unsigned i = 0; i < kChannels; i++)
        {
            const Channel& c = _ch[i];
            if (c.running && !(c.control & kCounterMode))
            {
                const uint64_t at = NextZero(c);
                if (at <= now && at < nextAt)
                {
                    next = static_cast<int>(i);
                    nextAt = at;
                }
            }
        }
        if (next < 0)
            break;
        ZeroCount(static_cast<unsigned>(next), nextAt);
    }
    _now = now;
}

void NextCtc::Trigger(unsigned channel, uint64_t now)
{
    Channel& c = _ch[channel];
    if (c.control & kCounterMode)
    {
        if (!c.running)
            return;
        if (--c.counter == 0)
            ZeroCount(channel, now);
        return;
    }
    if (c.waitTrigger)  // a timer waiting for its trigger starts on the next prescaler edge
    {
        c.waitTrigger = false;
        c.running = true;
        c.phase = now;
    }
}

void NextCtc::Write(unsigned channel, uint8_t value, uint64_t now)
{
    Advance(now);
    Channel& c = _ch[channel % kChannels];
    if (c.waitTc)
    {
        c.waitTc = false;
        c.tc = value ? value : 256;
        c.counter = c.tc;
        if (c.control & kCounterMode)
            c.running = true;
        else if (c.control & kTriggerClk)
        {
            c.waitTrigger = true;
            c.running = false;
        }
        else
        {
            c.running = true;
            c.phase = now;
        }
        return;
    }
    if (value & kControl)
    {
        c.control = value;
        if (value & kSoftReset)
        {
            c.running = false;
            c.waitTrigger = false;
        }
        c.waitTc = (value & kTimeConstantFollows) != 0;
        return;
    }
    _vector = value & 0xF8;  // the interrupt vector: written through channel 0 on a real CTC
}

uint8_t NextCtc::Read(unsigned channel, uint64_t now)
{
    Advance(now);
    const Channel& c = _ch[channel % kChannels];
    if (c.running && !(c.control & kCounterMode))
    {
        // the down counter between the prescaler wraps
        const uint64_t elapsed = (now - c.phase) / Prescale(c);
        const uint64_t value = c.counter > elapsed ? c.counter - elapsed : 0;
        return static_cast<uint8_t>(value);
    }
    return static_cast<uint8_t>(c.counter);
}
