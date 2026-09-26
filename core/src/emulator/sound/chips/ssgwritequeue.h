#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/// Constant lag of a TurboSound-slot device's render cursor behind the CPU's
/// T-state timeline. The render loop's position against the CPU clock saws
/// within about one output sample of SSG ticks (<= ~112 T at 44.1 kHz), and an
/// anchor can land anywhere on that sawtooth; lagging by more than twice that
/// keeps every tick behind the newest timed event (an SSG register write, an
/// FM DAC word), so each lands on the tick of its own T-state. A constant
/// shift, ~73 us, no jitter. Shared by both devices so they stay bit-identical
constexpr int64_t kTurboSoundRenderLagT = 256;

/// One SSG register write, timed on the device's render timeline
struct SsgWrite
{
    int64_t t = 0;      // frame-relative T-state (negative: carried over from the previous frame)
    uint8_t reg = 0;
    uint8_t value = 0;
};

/// Fixed-capacity FIFO of timed SSG register writes. The CPU sees a write at
/// once (SoundChip_AY8910::latchRegister); the render loop applies it to the
/// generators (applyRegister) on the tick its T-state falls in, instead of
/// wherever rendering happens to stand when the OUT executes (up to one output
/// sample, 22.7 us at 44.1 kHz, away). Pending writes are machine state: the
/// devices serialize them for TTD.
class SsgWriteQueue
{
public:
    /// A write waits at most the render lag plus one output sample of ticks
    /// (< 400 T): under 40 writes at the densest OUT rate. The owner applies
    /// the oldest write early when full rather than dropping it
    static constexpr size_t kCapacity = 64;

    bool empty() const
    {
        return _count == 0;
    }

    bool full() const
    {
        return _count == kCapacity;
    }

    size_t size() const
    {
        return _count;
    }

    const SsgWrite& front() const
    {
        return _items[_head];
    }

    /// i-th oldest pending write (0 = front)
    const SsgWrite& at(size_t index) const
    {
        return _items[(_head + index) % kCapacity];
    }

    /// Enqueue; the caller makes room first (full() -> apply and pop the front)
    void push(const SsgWrite& write)
    {
        if (_count == kCapacity)
            return;
        _items[(_head + _count) % kCapacity] = write;
        _count++;
    }

    void pop()
    {
        _head = (_head + 1) % kCapacity;
        _count--;
    }

    void clear()
    {
        _head = 0;
        _count = 0;
    }

    /// Shift every pending timestamp by -shift (frame rollover)
    void rebase(int64_t shift)
    {
        for (size_t i = 0; i < _count; i++)
            _items[(_head + i) % kCapacity].t -= shift;
    }

private:
    std::array<SsgWrite, kCapacity> _items{};
    size_t _head = 0;
    size_t _count = 0;
};
