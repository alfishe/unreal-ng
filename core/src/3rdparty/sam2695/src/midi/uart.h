// libsam2695 - serial MIDI IN receiver (8N1, 31 250 baud).
//
// A standard 16x-oversampling UART: the receive clock runs free on an absolute grid of
// kMidiBaud * kUartOversample ticks per second (tick k at host time k * H / 500000). The first tick
// that sees the line low starts a frame; every bit (start, 8 data LSB first, stop) is decided by a
// majority of the samples at ticks 7, 8 and 9 of its 16, i.e. around the bit's middle. A low start
// bit at the middle is a false start (ignored); a low stop bit is a framing error: the byte is dropped
// and counted, and the receiver waits for the line to return high. A good byte is delivered at its stop
// bit's middle sample. The datasheet does not describe the receiver; the tolerance this gives is
// +-(0.5 - 1/16) / 9.5 = +-4.6 % of the bit time (README "UART timing").
#pragma once

#include "common/wideint.h"
#include "sam2695/sam2695config.h"

#include <cstdint>

namespace sam2695
{

class Uart
{
public:
    static constexpr uint64_t kTickRate = static_cast<uint64_t>(kMidiBaud) * kUartOversample; // 500 kHz

    void Configure(uint64_t hostTickRate) { _hostRate = hostTickRate; }
    void Reset();

    // Process every receive-clock tick before host time t with the current line level. `sink(time,
    // byte)` receives each completed byte with the host time of its stop-bit middle.
    template <class Sink>
    void AdvanceTo(uint64_t t, Sink&& sink);

    // The line changes to `level` at host time t (ticks at or after t see the new level).
    template <class Sink>
    void SetLevel(uint64_t t, bool level, Sink&& sink);

    bool Level() const { return _level; }
    uint64_t Bytes() const { return _bytes; }
    uint64_t FramingErrors() const { return _framingErrors; }
    uint64_t FalseStarts() const { return _falseStarts; }
    void ClearCounters() { _bytes = _framingErrors = _falseStarts = 0; }

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(_level);
        ar(_state);
        ar(_startTick);
        ar(_nextTick);
        ar(_bitIndex);
        ar(_votes);
        ar(_ones);
        ar(_shift);
        ar(_bytes);
        ar(_framingErrors);
        ar(_falseStarts);
    }

private:
    enum class State : uint8_t
    {
        Idle,
        Receiving,
        WaitHigh
    };

    // tick k is strictly before host time t
    bool TickBefore(uint64_t k, uint64_t t) const { return Less(Mul64(k, _hostRate), Mul64(t, kTickRate)); }
    void StartFrame(uint64_t tick)
    {
        _state = State::Receiving;
        _startTick = tick;
        _bitIndex = 0;
        _votes = 0;
        _ones = 0;
        _shift = 0;
        _nextTick = tick + 7;
    }

    uint64_t _hostRate = 3500000;
    bool _level = true;
    State _state = State::Idle;
    uint64_t _startTick = 0;
    uint64_t _nextTick = 0;
    uint8_t _bitIndex = 0;
    uint8_t _votes = 0;
    uint8_t _ones = 0;
    uint8_t _shift = 0;
    uint64_t _bytes = 0;
    uint64_t _framingErrors = 0;
    uint64_t _falseStarts = 0;
};

inline void Uart::Reset()
{
    _state = _level ? State::Idle : State::WaitHigh;
    _bitIndex = _votes = _ones = _shift = 0;
    ClearCounters();
}

template <class Sink>
void Uart::AdvanceTo(uint64_t t, Sink&& sink)
{
    while (_state == State::Receiving && TickBefore(_nextTick, t))
    {
        const uint64_t tick = _nextTick;
        _votes++;
        _ones = static_cast<uint8_t>(_ones + (_level ? 1 : 0));
        if (_votes < 3)
        {
            _nextTick = tick + 1;
            continue;
        }
        const bool bit = _ones >= 2;
        _votes = 0;
        _ones = 0;
        if (_bitIndex == 0)
        {
            if (bit)
            {
                // false start: a glitch shorter than half a bit
                _falseStarts++;
                _state = State::Idle;
                if (!_level)
                    StartFrame(tick + 1);
                continue;
            }
        }
        else if (_bitIndex <= 8)
        {
            _shift = static_cast<uint8_t>(_shift | ((bit ? 1u : 0u) << (_bitIndex - 1)));
        }
        else
        {
            if (bit)
            {
                _bytes++;
                const uint64_t middle = _startTick + 16u * 9u + 8u;
                sink(MulDivCeil(middle, _hostRate, kTickRate), _shift);
                _state = State::Idle;
                if (!_level)
                    StartFrame(tick + 1); // the next start bit began before the stop-bit middle
            }
            else
            {
                // a high line means it rose again inside the bad stop bit: the next falling edge starts a frame
                _framingErrors++;
                _state = _level ? State::Idle : State::WaitHigh;
            }
            continue;
        }
        _bitIndex++;
        _nextTick = _startTick + 16u * _bitIndex + 7u;
    }
}

template <class Sink>
void Uart::SetLevel(uint64_t t, bool level, Sink&& sink)
{
    AdvanceTo(t, sink);
    if (level == _level)
        return;
    _level = level;
    if (!level && _state == State::Idle)
        StartFrame(MulDivCeil(t, kTickRate, _hostRate));
    else if (level && _state == State::WaitHigh)
        _state = State::Idle;
}

} // namespace sam2695
