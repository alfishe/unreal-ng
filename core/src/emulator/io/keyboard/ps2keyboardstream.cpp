#include "stdafx.h"

#include "ps2keyboardstream.h"

#include <algorithm>
#include <cstring>

namespace
{
uint32_t MicrosToTStates(uint32_t micros, uint32_t baseHz)
{
    return static_cast<uint32_t>((static_cast<uint64_t>(micros) * baseHz + 500000u) / 1000000u);
}
}  // namespace

Ps2KeyboardStream::Ps2KeyboardStream()
{
    SetBaseClock(3500000);
}

void Ps2KeyboardStream::SetBaseClock(uint32_t baseHz)
{
    if (baseHz == 0)
        baseHz = 3500000;
    _byteT = std::max<uint32_t>(1, MicrosToTStates(kByteMicros, baseHz));
    _delayT = MicrosToTStates(kTypematicDelayMicros, baseHz);
    _periodT = std::max<uint32_t>(1, MicrosToTStates(kTypematicPeriodMicros, baseHz));
}

bool Ps2KeyboardStream::IsHeld(PcKey key) const
{
    const uint8_t index = static_cast<uint8_t>(key);
    if (key == PcKey::None || key >= PcKey::Count)
        return false;
    return (_state.held[index >> 3] & (1u << (index & 7))) != 0;
}

void Ps2KeyboardStream::OnPcKey(PcKey key, bool pressed)
{
    if (key == PcKey::None || key >= PcKey::Count)
        return;

    const uint64_t now = Now();
    Advance(now);

    const uint8_t index = static_cast<uint8_t>(key);
    uint8_t& bits = _state.held[index >> 3];
    const uint8_t mask = static_cast<uint8_t>(1u << (index & 7));
    if (pressed == ((bits & mask) != 0) && key != PcKey::Pause)
        return;  // no change: a second press of a held key is the typematic's job
    bits = static_cast<uint8_t>(pressed ? (bits | mask) : (bits & ~mask));
    if (key == PcKey::Pause)
        bits = static_cast<uint8_t>(bits & ~mask);  // Pause has no break: never "held"

    Enqueue(pckey::Ps2Set2Bytes(key, pressed), now);

    if (pressed && key != PcKey::Pause)
    {
        _state.repeatKey = index;
        _state.repeatAt = now + _delayT;
    }
    else if (!pressed && index == _state.repeatKey)
    {
        _state.repeatKey = 0;
    }
}

void Ps2KeyboardStream::ReleaseAllPcKeys()
{
    for (int n = 1; n < static_cast<int>(PcKey::Count); n++)
    {
        if (_state.held[n >> 3] & (1u << (n & 7)))
            OnPcKey(static_cast<PcKey>(n), false);
    }
}

void Ps2KeyboardStream::Push(uint8_t value, uint64_t at)
{
    if (_state.count == 0)
        _state.nextByteAt = std::max(at, _state.lastByteAt) + _byteT;
    _state.queue[(_state.head + _state.count) % kQueueSize] = value;
    _state.count++;
}

void Ps2KeyboardStream::Enqueue(const std::vector<uint8_t>& bytes, uint64_t at)
{
    // A key's bytes go together or not at all (half a sequence would confuse the host)
    if (_state.overflow || bytes.size() > static_cast<size_t>(kQueueSize - _state.count))
    {
        _state.overflow = 1;
        return;
    }
    for (uint8_t b : bytes)
        Push(b, at);
}

uint64_t Ps2KeyboardStream::NextEventAt() const
{
    uint64_t next = UINT64_MAX;
    if (_state.count)
        next = _state.nextByteAt;
    if (_state.repeatKey)
        next = std::min(next, _state.repeatAt);
    return next;
}

void Ps2KeyboardStream::Advance()
{
    Advance(Now());
}

void Ps2KeyboardStream::Advance(uint64_t now)
{
    for (;;)
    {
        const uint64_t byteAt = _state.count ? _state.nextByteAt : UINT64_MAX;
        const uint64_t repeatAt = _state.repeatKey ? _state.repeatAt : UINT64_MAX;
        if (std::min(byteAt, repeatAt) > now)
            break;

        if (repeatAt < byteAt)
        {
            // A repeat while the queue is still sending waits for it like any other make
            Enqueue(pckey::Ps2Set2Bytes(static_cast<PcKey>(_state.repeatKey), true), repeatAt);
            _state.repeatAt += _periodT;
            continue;
        }

        const uint8_t value = _state.queue[_state.head];
        _state.head = static_cast<uint8_t>((_state.head + 1) % kQueueSize);
        _state.count--;
        _state.lastByteAt = byteAt;
        if (_state.count)
            _state.nextByteAt = byteAt + _byteT;
        if (_state.overflow && _state.count < kQueueSize)
        {
            _state.overflow = 0;
            Push(kOverflowCode, byteAt);
        }
        if (_sink)
            _sink(value, byteAt);
    }
}

void Ps2KeyboardStream::Rebase(uint64_t now)
{
    _state.lastByteAt = now;
    if (_state.count)
        _state.nextByteAt = now + _byteT;
    if (_state.repeatKey)
        _state.repeatAt = now + _delayT;
}

void Ps2KeyboardStream::Clear()
{
    std::memset(&_state, 0, sizeof(_state));
}
