// z84ctc.cpp - the Z84C15's CTC (z84c15.h; Zilog Z80 CTC data sheet PS0181).
//
// Lazy model: a running channel is an anchor (a clock for a timer, an input
// edge count for a counter) plus the count it had there. Everything else - the
// count now, the zero counts so far, the clock of the next one - follows from
// the clock and the input when asked. A control word or a clock speed change
// "folds" the channel: it is re-anchored at now with its count, its zero
// counts and (timer) the phase of its last step kept.

#include "z84c15.h"

#include <numeric>

namespace Z84Lib
{

using Detail::MulDivCeil;
using Detail::MulDivFloor;

namespace
{
constexpr uint64_t kNever = UINT64_MAX;
constexpr uint8_t kInterrupt = 0x80;
constexpr uint8_t kCounter = 0x40;
constexpr uint8_t kTriggerStart = 0x08;
constexpr uint8_t kConstantFollows = 0x04;
constexpr uint8_t kReset = 0x02;

uint32_t Loaded(uint8_t value)
{
    return value ? value : 256u;
}
}  // namespace

void Z84Ctc::Reset()
{
    _version++;  // StateVersion
    for (ChannelState& ch : _ch)
        ch = ChannelState{};
    _nextDue = kNever;
}

/// region <Time base and inputs>

void Z84Ctc::SetUnitsPerSecond(uint64_t unitsPerSecond)
{
    _version++;  // StateVersion
    const uint64_t now = Now();
    for (uint8_t i = 0; i < 4; i++)
        Fold(i, now);
    _unitsPerSecond = unitsPerSecond;
    for (uint8_t i = 0; i < 4; i++)
        SetTrigger(i, _trg[i]);
}

void Z84Ctc::SetSystemClockPeriod(uint32_t num, uint32_t den)
{
    _version++;  // StateVersion
    if (!num || !den || (num == _clkNum && den == _clkDen))
        return;
    // The timers' steps so far at the old rate, then on at the new one
    const uint64_t now = Now();
    for (uint8_t i = 0; i < 4; i++)
        Fold(i, now);
    _clkNum = num;
    _clkDen = den;
    UpdateNextDue();
}

void Z84Ctc::RestoreSystemClockPeriod(uint32_t num, uint32_t den)
{
    _version++;  // StateVersion
    _clkNum = num ? num : 1;
    _clkDen = den ? den : 1;
}

void Z84Ctc::SetTrigger(uint8_t channel, Trigger trigger)
{
    _version++;  // StateVersion
    const uint8_t i = channel & 3;
    if (trigger.kind == TriggerKind::Cascade && trigger.source >= i)
        trigger = Trigger{};  // only a lower channel: the inputs never form a loop
    if (trigger.kind == TriggerKind::Clock && !trigger.hz)
        trigger = Trigger{};

    // A counter keeps its count across the change: re-anchored on the new input's edge count
    const uint64_t now = Now();
    Fold(i, now);
    _trg[i] = trigger;
    _trgNum[i] = 0;
    _trgDen[i] = 1;
    if (trigger.kind == TriggerKind::Clock && _unitsPerSecond)
    {
        const uint64_t g = std::gcd(static_cast<uint64_t>(trigger.hz), _unitsPerSecond);
        _trgNum[i] = trigger.hz / g;
        _trgDen[i] = _unitsPerSecond / g;
    }
    ChannelState& ch = _ch[i];
    if (ch.running && ((ch.control & kCounter) || ch.waitingTrigger))
        ch.anchor = InputEdges(i, now);
    UpdateNextDue();
}

/// endregion </Time base and inputs>

/// region <Evaluation>

uint64_t Z84Ctc::InputEdges(uint8_t channel, uint64_t now) const
{
    const Trigger& t = _trg[channel];
    switch (t.kind)
    {
        case TriggerKind::Clock:
            return _trgNum[channel] ? MulDivFloor(now, _trgNum[channel], _trgDen[channel]) : 0;
        case TriggerKind::Cascade:
            return ZeroCountsAt(t.source, now);
        default:
            return 0;
    }
}

uint64_t Z84Ctc::EdgeTime(uint8_t channel, uint64_t edge) const
{
    const Trigger& t = _trg[channel];
    switch (t.kind)
    {
        case TriggerKind::Clock:
            return _trgNum[channel] ? MulDivCeil(edge, _trgDen[channel], _trgNum[channel]) : kNever;
        case TriggerKind::Cascade:
            return ZeroTime(t.source, edge);
        default:
            return kNever;
    }
}

Z84Ctc::ChannelState Z84Ctc::Effective(uint8_t channel, uint64_t now) const
{
    ChannelState ch = _ch[channel];
    if (ch.running && ch.waitingTrigger)
    {
        const uint64_t start = EdgeTime(channel, ch.anchor + 1);
        if (start <= now)
        {
            ch.waitingTrigger = 0;
            ch.anchor = start;
        }
    }
    return ch;
}

uint64_t Z84Ctc::Steps(uint8_t channel, const ChannelState& ch, uint64_t now) const
{
    if (ch.control & kCounter)
    {
        const uint64_t edges = InputEdges(channel, now);
        return edges > ch.anchor ? edges - ch.anchor : 0;
    }
    if (now <= ch.anchor)
        return 0;
    return MulDivFloor(now - ch.anchor, _clkDen, Prescaler(ch) * _clkNum);
}

uint32_t Z84Ctc::CountAt(uint8_t channel, const ChannelState& ch, uint64_t now) const
{
    const uint32_t d = Loaded(ch.down);
    if (!ch.running || ch.waitingTrigger)
        return d;
    const uint64_t s = Steps(channel, ch, now);
    if (s < d)
        return static_cast<uint32_t>(d - s);
    const uint32_t tc = Loaded(ch.timeConstant);
    return tc - static_cast<uint32_t>((s - d) % tc);
}

uint64_t Z84Ctc::ZeroCountsAt(uint8_t channel, uint64_t now) const
{
    const ChannelState ch = Effective(channel, now);
    if (!ch.running || ch.waitingTrigger)
        return ch.zeroBase;
    const uint64_t s = Steps(channel, ch, now);
    const uint32_t d = Loaded(ch.down);
    if (s < d)
        return ch.zeroBase;
    return ch.zeroBase + 1 + (s - d) / Loaded(ch.timeConstant);
}

uint64_t Z84Ctc::ZeroTime(uint8_t channel, uint64_t k) const
{
    const ChannelState& ch = _ch[channel];
    if (k <= ch.zeroBase)
        return 0;
    if (!ch.running)
        return kNever;

    // Zero count k is down-counter step s after the anchor
    const uint64_t s = Loaded(ch.down) + (k - ch.zeroBase - 1) * Loaded(ch.timeConstant);
    uint64_t start = ch.anchor;
    if (ch.waitingTrigger)
    {
        start = EdgeTime(channel, ch.anchor + 1);
        if (start == kNever)
            return kNever;
    }
    else if (ch.control & kCounter)
    {
        return EdgeTime(channel, ch.anchor + s);
    }
    const uint64_t span = MulDivCeil(s, Prescaler(ch) * _clkNum, _clkDen);
    return span > kNever - start ? kNever : start + span;
}

void Z84Ctc::Fold(uint8_t channel, uint64_t now)
{
    ChannelState ch = Effective(channel, now);
    if (!ch.running || ch.waitingTrigger)
    {
        _ch[channel] = ch;
        return;
    }
    const uint64_t s = Steps(channel, ch, now);
    const uint32_t count = CountAt(channel, ch, now);
    ch.zeroBase = ZeroCountsAt(channel, now);
    ch.down = static_cast<uint8_t>(count);  // 256 -> 0
    if (ch.control & kCounter)
        ch.anchor += s;
    else
        ch.anchor += MulDivCeil(s, Prescaler(ch) * _clkNum, _clkDen);  // the clock of the last step: the phase stays
    _ch[channel] = ch;
}

void Z84Ctc::UpdateNextDue()
{
    uint64_t due = kNever;
    for (uint8_t i = 0; i < 4; i++)
    {
        const ChannelState& ch = _ch[i];
        if ((ch.control & kInterrupt) && ch.running)
        {
            const uint64_t t = ZeroTime(i, ch.zeroSeen + 1);
            if (t < due)
                due = t;
        }
    }
    _nextDue = due;
}

/// endregion </Evaluation>

/// region <Live view>

uint8_t Z84Ctc::Count(uint8_t channel) const
{
    const uint8_t i = channel & 3;
    const uint64_t now = Now();
    return static_cast<uint8_t>(CountAt(i, Effective(i, now), now));
}

uint64_t Z84Ctc::ZeroCounts(uint8_t channel) const
{
    return ZeroCountsAt(channel & 3, Now());
}

double Z84Ctc::OutputHz(uint8_t channel) const
{
    const uint8_t i = channel & 3;
    const ChannelState ch = Effective(i, Now());
    if (!ch.running || ch.waitingTrigger)
        return 0.0;
    const double tc = Loaded(ch.timeConstant);
    if (!(ch.control & kCounter))
    {
        if (!_unitsPerSecond)
            return 0.0;
        return static_cast<double>(_unitsPerSecond) * _clkDen / (static_cast<double>(Prescaler(ch)) * _clkNum) / tc;
    }
    const Trigger& t = _trg[i];
    if (t.kind == TriggerKind::Clock && _trgNum[i])
        return t.hz / tc;
    if (t.kind == TriggerKind::Cascade)
        return OutputHz(t.source) / tc;
    return 0.0;
}

/// endregion </Live view>

uint8_t Z84Ctc::Read(uint8_t channel)
{
    return Count(channel);
}

void Z84Ctc::Write(uint8_t channel, uint8_t value)
{
    _version++;  // StateVersion
    const uint8_t i = channel & 3;
    ChannelState& ch = _ch[i];
    const uint64_t now = Now();

    if (ch.awaitingConstant)
    {
        Fold(i, now);
        ch.timeConstant = value;
        ch.awaitingConstant = 0;
        if (!ch.running)
        {
            // Stopped (reset, or never started): the constant loads the counter and starts it
            ch.running = 1;
            ch.down = value;
            ch.control &= static_cast<uint8_t>(~kReset);
            if (ch.control & kCounter)
            {
                ch.waitingTrigger = 0;
                ch.anchor = InputEdges(i, now);
            }
            else if (ch.control & kTriggerStart)
            {
                ch.waitingTrigger = 1;  // the next CLK/TRG edge starts the timer
                ch.anchor = InputEdges(i, now);
            }
            else
            {
                ch.waitingTrigger = 0;
                ch.anchor = now;
            }
            ch.zeroSeen = ch.zeroBase;
        }
        // Running: the count goes on to zero, the new constant is reloaded there (the fold kept the count)
        UpdateNextDue();
        return;
    }

    if (value & 0x01)
    {
        const bool wasEnabled = (ch.control & kInterrupt) != 0;
        const bool wasCounter = (ch.control & kCounter) != 0;
        Fold(i, now);
        ch.control = value;
        if (value & kReset)
        {
            ch.running = 0;  // software reset: stops with its count until the next time constant
            ch.waitingTrigger = 0;
        }
        else if (ch.running && !ch.waitingTrigger && wasCounter != ((value & kCounter) != 0))
        {
            ch.anchor = (value & kCounter) ? InputEdges(i, now) : now;  // the count goes on from the other input
        }
        if (value & kConstantFollows)
            ch.awaitingConstant = 1;
        if (!(value & kInterrupt))
            ch.ip = 0;  // interrupt disabled: a pending request is dropped
        else if (!wasEnabled)
            ch.zeroSeen = ZeroCountsAt(i, now);  // only zero counts from now on request
        UpdateNextDue();
    }
    else if (i == 0)
    {
        _vector = value & 0xF8;
    }
}

void Z84Ctc::Poll()
{
    // Asked at every instruction boundary while a channel can interrupt: one clock read and a compare
    // until the earliest zero count is due
    if (_nextDue == kNever)
        return;
    const uint64_t now = Now();
    if (now < _nextDue)
        return;

    for (uint8_t i = 0; i < 4; i++)
    {
        ChannelState& ch = _ch[i];
        if (!(ch.control & kInterrupt) || !ch.running)
            continue;
        const uint64_t counts = ZeroCountsAt(i, now);
        if (counts > ch.zeroSeen)
        {
            ch.zeroSeen = counts;
            ch.ip = 1;
        }
    }
    UpdateNextDue();
}

}  // namespace Z84Lib
