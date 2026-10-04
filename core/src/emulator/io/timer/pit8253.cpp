#include "stdafx.h"

#include "pit8253.h"

#include <numeric>

Pit8253::Pit8253(uint32_t clockHz, uint32_t baseClockHz)
    : _clockHz(clockHz ? clockHz : 1), _baseHz(baseClockHz ? baseClockHz : 1)
{
    const uint64_t g = std::gcd(static_cast<uint64_t>(_clockHz), static_cast<uint64_t>(_baseHz));
    _num = _clockHz / g;
    _den = _baseHz / g;
}

void Pit8253::PowerOn(uint64_t now)
{
    _state = State();
    _state.lastNow = now;
}

uint32_t Pit8253::Numeric(const Counter& c, uint16_t raw)
{
    if (!BcdOf(c))
        return raw ? raw : 65536u;
    // BCD: four decimal digits (a digit above 9 counts as its binary value, as the chip's decade counters would)
    const uint32_t value = ((raw >> 12) & 0x0F) * 1000u + ((raw >> 8) & 0x0F) * 100u + ((raw >> 4) & 0x0F) * 10u + (raw & 0x0F);
    return value ? value : 10000u;
}

uint16_t Pit8253::Raw(const Counter& c, uint32_t numeric)
{
    const uint32_t n = numeric % Modulus(c);
    if (!BcdOf(c))
        return static_cast<uint16_t>(n);
    return static_cast<uint16_t>(((n / 1000) << 12) | (((n / 100) % 10) << 8) | (((n / 10) % 10) << 4) | (n % 10));
}

uint16_t Pit8253::CountValue(const Counter& c) const
{
    if (ModeOf(c) == 3 && c.counting)
    {
        // The element counts down by two per pulse within each half; an odd count is approximated the same way
        const uint32_t odd = (c.reload & 1) && c.out ? 1u : 0u;
        return Raw(c, c.phaseLeft * 2 - odd);
    }
    return Raw(c, c.ce);
}

uint16_t Pit8253::PeekCount(uint8_t counter) const
{
    return CountValue(_state.counter[counter % 3]);
}

uint32_t Pit8253::OutputPeriod(uint8_t counter) const
{
    const Counter& c = _state.counter[counter % 3];
    const uint8_t mode = ModeOf(c);
    if ((mode != 2 && mode != 3) || !c.gate || !c.hasCount)
        return 0;
    if (c.counting)
        return c.reload;
    return c.loadPending ? Numeric(c, c.cr) : 0;
}

void Pit8253::Advance(uint64_t now)
{
    if (now < _state.lastNow)
    {
        // The caller's clock restarted: count from here
        _state.lastNow = now;
        _state.fraction = 0;
        return;
    }
    const uint64_t elapsed = now - _state.lastNow;
    if (elapsed == 0)
        return;
    _state.lastNow = now;
    const uint64_t scaled = elapsed * _num + _state.fraction;
    const uint64_t pulses = scaled / _den;
    _state.fraction = scaled % _den;
    if (pulses == 0)
        return;
    for (Counter& c : _state.counter)
        AdvanceCounter(c, pulses);
}

void Pit8253::Load(Counter& c)
{
    // The CLK pulse that moves the count register into the counting element
    c.loadPending = 0;
    c.counting = 1;
    c.fired = 0;
    c.newCount = 0;
    const uint32_t n = Numeric(c, c.cr);
    switch (ModeOf(c))
    {
        case 2:
            c.reload = n;
            c.ce = n;
            c.out = 1;
            break;
        case 3:
            c.reload = n;
            c.out = 1;
            c.phaseLeft = HighHalf(n);
            break;
        case 1:
            c.ce = n % Modulus(c);
            c.out = 0;   // the one-shot's pulse starts
            break;
        default:   // 0, 4, 5
            c.ce = n % Modulus(c);
            break;
    }
}

void Pit8253::AdvanceCounter(Counter& c, uint64_t pulses)
{
    const uint8_t mode = ModeOf(c);
    const bool gated = c.gate != 0;
    if (c.loadPending)
    {
        // Modes 2 / 3 wait for the gate to load; modes 0 / 4 load anyway (the gate holds only the counting)
        if ((mode == 2 || mode == 3) && !gated)
            return;
        Load(c);
        --pulses;
    }
    if (!c.counting || pulses == 0)
        return;

    switch (mode)
    {
        case 0:
        case 1:
        case 4:
        case 5:
        {
            if ((mode == 0 || mode == 4) && !gated)
                return;
            const uint32_t m = Modulus(c);
            if (!c.fired)
            {
                const uint64_t toZero = c.ce == 0 ? m : c.ce;
                if (pulses >= toZero)
                {
                    c.fired = 1;
                    if (mode == 0 || mode == 1)
                        c.out = 1;
                    else
                        c.out = pulses == toZero ? 0 : 1;   // the strobe lasts the one pulse at zero
                }
            }
            else
                c.out = 1;   // a strobe ended (modes 4 / 5); modes 0 / 1 stay high
            c.ce = static_cast<uint32_t>((c.ce + m - pulses % m) % m);
            break;
        }
        case 2:
        {
            if (!gated)
                return;
            while (pulses > 0)
            {
                if (pulses < c.ce)
                {
                    c.ce -= static_cast<uint32_t>(pulses);
                    break;
                }
                // The element passed 1 and reloads
                pulses -= c.ce;
                if (c.newCount)
                {
                    c.reload = Numeric(c, c.cr);
                    c.newCount = 0;
                }
                c.ce = c.reload;
                pulses %= c.reload;
            }
            c.out = c.ce == 1 ? 0 : 1;
            break;
        }
        case 3:
        {
            if (!gated)
                return;
            while (pulses > 0)
            {
                if (pulses < c.phaseLeft)
                {
                    c.phaseLeft -= static_cast<uint32_t>(pulses);
                    break;
                }
                pulses -= c.phaseLeft;
                c.out ^= 1;
                if (c.newCount)
                {
                    // A count written while counting is taken at the end of the current half-cycle
                    c.reload = Numeric(c, c.cr);
                    c.newCount = 0;
                }
                c.phaseLeft = c.out ? HighHalf(c.reload) : LowHalf(c.reload);
                if (c.out)
                    pulses %= HighHalf(c.reload) + LowHalf(c.reload);   // whole periods change nothing
            }
            break;
        }
        default:
            break;
    }
}

void Pit8253::CountWritten(Counter& c)
{
    c.hasCount = 1;
    switch (ModeOf(c))
    {
        case 0:
            // OUT goes low and the new count is loaded at the next pulse
            c.out = 0;
            c.counting = 0;
            c.loadPending = 1;
            break;
        case 4:
            // The strobe is armed again with the new count
            c.out = 1;
            c.counting = 0;
            c.loadPending = 1;
            break;
        case 2:
        case 3:
            if (c.counting)
                c.newCount = 1;
            else
                c.loadPending = 1;
            break;
        default:
            // Modes 1 / 5: the count is used at the next gate trigger
            break;
    }
}

void Pit8253::Write(uint8_t reg, uint8_t value, uint64_t now)
{
    Advance(now);
    reg &= 0x03;
    if (reg == kControl)
    {
        const uint8_t sc = static_cast<uint8_t>(value >> 6);
        if (sc == 3)
            return;   // the 8254's read-back command: an 8253 does nothing
        Counter& c = _state.counter[sc];
        if ((value & 0x30) == 0)
        {
            // Counter latch command: freeze the element until it is read (a second latch before the read is ignored)
            if (!c.latched)
            {
                c.latch = CountValue(c);
                c.latched = 1;
            }
            return;
        }
        // A new control word: the counter stops and waits for its count; OUT low in mode 0, high otherwise
        c.control = static_cast<uint8_t>(value & 0x3F);
        c.out = ModeOf(c) == 0 ? 0 : 1;
        c.hasCount = 0;
        c.counting = 0;
        c.loadPending = 0;
        c.newCount = 0;
        c.fired = 0;
        c.writeMsb = 0;
        c.readMsb = 0;
        c.latched = 0;
        return;
    }

    Counter& c = _state.counter[reg];
    switch (RwOf(c))
    {
        case 1:   // LSB only: the MSB is 0
            c.cr = value;
            CountWritten(c);
            break;
        case 2:   // MSB only: the LSB is 0
            c.cr = static_cast<uint16_t>(value << 8);
            CountWritten(c);
            break;
        case 3:
            if (!c.writeMsb)
            {
                c.lsb = value;
                c.writeMsb = 1;
                if (ModeOf(c) == 0)
                    c.counting = 0;   // mode 0: writing the first byte stops the count
                break;
            }
            c.writeMsb = 0;
            c.cr = static_cast<uint16_t>(c.lsb | (value << 8));
            CountWritten(c);
            break;
        default:
            break;   // no control word yet: the counter takes no count
    }
}

uint8_t Pit8253::Read(uint8_t reg, uint64_t now)
{
    Advance(now);
    reg &= 0x03;
    if (reg == kControl)
        return 0xFF;
    Counter& c = _state.counter[reg];
    const uint16_t value = c.latched ? c.latch : CountValue(c);
    uint8_t result = 0;
    switch (RwOf(c))
    {
        case 2:
            result = static_cast<uint8_t>(value >> 8);
            c.latched = 0;
            break;
        case 3:
            if (!c.readMsb)
            {
                c.readMsb = 1;
                result = static_cast<uint8_t>(value);
                break;
            }
            c.readMsb = 0;
            result = static_cast<uint8_t>(value >> 8);
            c.latched = 0;
            break;
        default:   // 1 (and a counter never programmed): the LSB
            result = static_cast<uint8_t>(value);
            c.latched = 0;
            break;
    }
    return result;
}

void Pit8253::SetGate(uint8_t counter, bool level, uint64_t now)
{
    Advance(now);
    Counter& c = _state.counter[counter % 3];
    const bool rising = level && !c.gate;
    c.gate = level ? 1 : 0;
    const uint8_t mode = ModeOf(c);
    if (!level && (mode == 2 || mode == 3))
    {
        // Gate low: OUT high at once, the count holds; the next rising edge reloads
        c.out = 1;
        if (c.counting)
        {
            c.counting = 0;
            c.loadPending = 1;
        }
        return;
    }
    if (rising && c.hasCount && (mode == 1 || mode == 5 || mode == 2 || mode == 3))
        c.loadPending = 1;   // the trigger: the count (re)loads at the next pulse
}
