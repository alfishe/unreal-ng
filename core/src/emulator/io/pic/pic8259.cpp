#include "stdafx.h"

#include "pic8259.h"

void Pic8259::PowerOn()
{
    _s = State{};
    _s.imr = 0xFF;
    _s.lowest = 7;
}

void Pic8259::Write(bool a0, uint8_t value)
{
    if (!a0)
    {
        if (value & 0x10)
        {
            // ICW1: the init sequence starts over (datasheet "Initialization Command Words")
            const uint8_t lines = _s.lines;
            _s = State{};
            _s.icw1 = value;
            _s.lowest = 7;
            _s.initStep = 2;
            _s.lines = lines;
            // The edge detectors re-arm: a pin already high requests again only after it falls and rises
            if (LevelTriggered())
                _s.irr = lines;
            return;
        }
        if (value & 0x08)
        {
            // OCW3: read register select, poll, special mask
            if (value & 0x02)
                _s.mode = static_cast<uint8_t>((_s.mode & ~kModeReadIsr) | ((value & 0x01) ? kModeReadIsr : 0));
            if (value & 0x04)
                _s.mode |= kModePoll;
            if (value & 0x40)
                _s.mode = static_cast<uint8_t>((_s.mode & ~kModeSpecialMask) | ((value & 0x20) ? kModeSpecialMask : 0));
            return;
        }
        Ocw2(value);
        return;
    }

    switch (_s.initStep)
    {
        case 2:
            _s.icw2 = value;
            _s.initStep = (_s.icw1 & 0x02) ? ((_s.icw1 & 0x01) ? 4 : 0) : 3;
            return;
        case 3:
            _s.icw3 = value;
            _s.initStep = (_s.icw1 & 0x01) ? 4 : 0;
            return;
        case 4:
            _s.icw4 = value;
            _s.initStep = 0;
            return;
        default:
            _s.imr = value;   // OCW1
            return;
    }
}

uint8_t Pic8259::Peek(bool a0) const
{
    if (a0)
        return _s.imr;
    return (_s.mode & kModeReadIsr) ? _s.isr : _s.irr;
}

uint8_t Pic8259::Read(bool a0)
{
    if (!a0 && (_s.mode & kModePoll))
    {
        // The poll word: D7 = a request, D2-D0 its level; the read acknowledges it
        _s.mode &= static_cast<uint8_t>(~kModePoll);
        const int level = PendingLevel();
        if (level < 0)
            return 0x00;
        Acknowledge();
        return static_cast<uint8_t>(0x80 | level);
    }
    return Peek(a0);
}

void Pic8259::SetInput(uint8_t level, bool high)
{
    const auto bit = static_cast<uint8_t>(1u << (level & 7));
    const bool was = (_s.lines & bit) != 0;
    _s.lines = static_cast<uint8_t>(high ? (_s.lines | bit) : (_s.lines & ~bit));
    if (LevelTriggered())
        _s.irr = static_cast<uint8_t>(high ? (_s.irr | bit) : (_s.irr & ~bit));
    else if (high && !was)
        _s.irr |= bit;
}

int Pic8259::HighestInService() const
{
    for (uint8_t rank = 0; rank < 8; ++rank)
    {
        const uint8_t level = LevelAt(rank);
        if (_s.isr & (1u << level))
            return level;
    }
    return -1;
}

int Pic8259::PendingLevel() const
{
    if (_s.initStep != 0 || _s.icw1 == 0)
        return -1;
    const auto requests = static_cast<uint8_t>(_s.irr & ~_s.imr);
    if (!requests)
        return -1;
    const bool special = (_s.mode & kModeSpecialMask) != 0;
    for (uint8_t rank = 0; rank < 8; ++rank)
    {
        const uint8_t level = LevelAt(rank);
        const auto bit = static_cast<uint8_t>(1u << level);
        // Fully nested: a level in service holds off itself and every lower one. Special mask: only itself
        if ((_s.isr & bit) && !(special && (_s.imr & bit)))
        {
            if (!special)
                return -1;
            continue;
        }
        if (requests & bit)
            return level;
    }
    return -1;
}

uint8_t Pic8259::Acknowledge()
{
    const int pending = PendingLevel();
    // No request left by the acknowledge (it fell): the chip answers with IR7's vector (datasheet "spurious")
    const auto level = static_cast<uint8_t>(pending < 0 ? 7 : pending);
    const auto bit = static_cast<uint8_t>(1u << level);
    if (pending >= 0)
    {
        if (!LevelTriggered())
            _s.irr &= static_cast<uint8_t>(~bit);
        if (AutoEoi())
        {
            if (_s.mode & kModeRotateAeoi)
                _s.lowest = level;
        }
        else
            _s.isr |= bit;
    }
    return static_cast<uint8_t>((_s.icw2 & 0xF8) | level);
}

void Pic8259::Eoi(uint8_t level)
{
    _s.isr &= static_cast<uint8_t>(~(1u << (level & 7)));
}

void Pic8259::Ocw2(uint8_t value)
{
    const auto named = static_cast<uint8_t>(value & 0x07);
    switch (value >> 5)
    {
        case 1:   // non-specific EOI
        {
            const int level = HighestInService();
            if (level >= 0)
                Eoi(static_cast<uint8_t>(level));
            return;
        }
        case 3:   // specific EOI
            Eoi(named);
            return;
        case 5:   // rotate on non-specific EOI
        {
            const int level = HighestInService();
            if (level >= 0)
            {
                Eoi(static_cast<uint8_t>(level));
                _s.lowest = static_cast<uint8_t>(level);
            }
            return;
        }
        case 4:   // rotate in automatic EOI mode: set
            _s.mode |= kModeRotateAeoi;
            return;
        case 0:   // rotate in automatic EOI mode: clear
            _s.mode &= static_cast<uint8_t>(~kModeRotateAeoi);
            return;
        case 7:   // rotate on specific EOI
            Eoi(named);
            _s.lowest = named;
            return;
        case 6:   // set priority: the named level becomes the lowest
            _s.lowest = named;
            return;
        default:  // 2: no operation
            return;
    }
}
