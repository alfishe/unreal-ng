#include "stdafx.h"

#include "nextctc.h"

#include <algorithm>

void NextCtc::Reset()
{
    for (Channel& c : _ch)
        c = Channel();
    _vector = 0;
    _quietUntil = 0;
}

uint64_t NextCtc::Ticks(const Channel& c, uint64_t from, uint64_t to) const
{
    if (to <= c.runStart)
        return 0;
    if (from < c.runStart)
        from = c.runStart;
    const uint64_t p = Prescale(c);
    return (to - c.runStart) / p - (from - c.runStart) / p;
}

uint16_t NextCtc::CountAt(const Channel& c, uint64_t now) const
{
    const uint64_t elapsed = Ticks(c, c.phase, now);
    return static_cast<uint16_t>(c.counter > elapsed ? c.counter - elapsed : 0);
}

/// The system clock at which a running timer channel reaches zero next: the prescaler ticks are at runStart + k * prescale
/// (the free-running 8-bit prescaler of ctc_chan.vhd, held at 0 while the channel is not running) and the counter reaches 0
/// at its `counter`-th tick after the last count
uint64_t NextCtc::NextZero(const Channel& c) const
{
    const uint64_t p = Prescale(c);
    const uint64_t base = c.phase > c.runStart ? (c.phase - c.runStart) / p : 0;
    return c.runStart + (base + c.counter) * p;
}

void NextCtc::StartTimer(Channel& c, uint64_t now)
{
    c.waitTrigger = false;
    c.running = true;
    c.runStart = now;
    c.phase = now;
}

void NextCtc::ZeroCount(unsigned channel, uint64_t when)
{
    Channel& c = _ch[channel];
    c.zeros++;
    c.counter = c.tc;
    c.phase = when;
    if ((c.control & kInterrupt) && onInterrupt)
        onInterrupt(channel);
    if (onZeroCount)
        onZeroCount(channel);
    // ZC/TO feeds the next channel's trigger (channel 3 feeds channel 0); the pulse is registered (ctc_chan.vhd zc_to_d) and
    // the edge detector sees it one clock later on the rising edge, two on the falling edge (control bit 4 of the target)
    Channel& t = _ch[(channel + 1) % kChannels];
    if (t.pendingCount < 4)
        t.pending[t.pendingCount++] = when + ((t.control & kRisingEdge) ? 1 : 2);
}

void NextCtc::CountEdge(unsigned channel, uint64_t when)
{
    Channel& c = _ch[channel];
    if (!c.running)
        return;
    if (c.counter > 0 && --c.counter == 0)
        ZeroCount(channel, when);
}

void NextCtc::Advance(uint64_t now)
{
    if (now < _now)
    {
        _now = now;  // the clock was restarted (reset): nothing to catch up
        return;
    }
    if (now < _quietUntil)
    {
        _now = now;  // the per-instruction call between two events
        return;
    }
    // Process the zero counts and the ZC/TO pulses in time order across the channels
    for (;;)
    {
        int next = -1;
        bool isPulse = false;
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
                    isPulse = false;
                }
            }
            if (c.pendingCount && c.pending[0] <= now && c.pending[0] < nextAt)
            {
                next = static_cast<int>(i);
                nextAt = c.pending[0];
                isPulse = true;
            }
        }
        if (next < 0)
            break;
        if (isPulse)
        {
            Channel& c = _ch[next];
            for (unsigned k = 1; k < c.pendingCount; k++)
                c.pending[k - 1] = c.pending[k];
            c.pendingCount--;
            Trigger(static_cast<unsigned>(next), nextAt);
        }
        else
            ZeroCount(static_cast<unsigned>(next), nextAt);
    }
    _now = now;
    // the earliest thing the loop above would act on from here: nothing was left at or before `now`
    uint64_t quietUntil = UINT64_MAX;
    for (const Channel& c : _ch)
    {
        if (c.running && !(c.control & kCounterMode))
            quietUntil = std::min(quietUntil, NextZero(c));
        if (c.pendingCount)
            quietUntil = std::min(quietUntil, c.pending[0]);
    }
    _quietUntil = _eventGate ? quietUntil : 0;
}

void NextCtc::Trigger(unsigned channel, uint64_t now)
{
    _quietUntil = 0;
    Channel& c = _ch[channel];
    if (c.control & kCounterMode)
        CountEdge(channel, now);
    else if (c.waitTrigger)  // a timer waiting for its trigger enters its run state: the prescaler starts from 0
        StartTimer(c, now);
}

void NextCtc::Write(unsigned channel, uint8_t value, uint64_t now)
{
    Advance(now);
    _quietUntil = 0;
    const unsigned index = channel % kChannels;
    Channel& c = _ch[index];
    if (c.waitTc)
    {
        // ctc_chan.vhd iowr_tc: the register only. A running channel (RUN_TC) goes on counting and takes the new constant
        // at its next reload; an idle one loads its counter and starts (a timer with the trigger bit waits for it)
        c.waitTc = false;
        c.tc = value ? value : 256;
        if (c.running)
            return;
        c.counter = c.tc;
        if ((c.control & kCounterMode) || !(c.control & kTriggerClk))
            StartTimer(c, now);
        else
        {
            c.waitTrigger = true;
            c.running = false;
        }
        return;
    }
    if (value & kControl)
    {
        const uint8_t old = c.control;
        // the running timer's count is kept as of now before the prescaler or the mode changes under it
        if (c.running && !(old & kCounterMode))
            c.counter = CountAt(c, now);
        if (c.running)
            c.phase = now;
        // changing the trigger edge selection (bit 4) counts as a clock edge (ctc_chan.vhd clk_edge_change), seen by the
        // state the channel is in before the write
        if ((old ^ value) & kRisingEdge)
        {
            if (c.running && (old & kCounterMode))
                CountEdge(index, now);
            else if (c.waitTrigger && !(value & (kSoftReset | kTimeConstantFollows)))
                StartTimer(c, now);
        }
        c.control = value;
        if (value & (kSoftReset | kTimeConstantFollows))
            c.waitTrigger = false;  // RESET / RESET_TC: the trigger wait is over, the time constant starts it again
        if (value & kSoftReset)
        {
            // soft reset: counting stops and the counter takes the time constant register again
            c.running = false;
            c.counter = c.tc;
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
        return static_cast<uint8_t>(CountAt(c, now));  // the down counter between the prescaler ticks
    return static_cast<uint8_t>(c.counter);
}
