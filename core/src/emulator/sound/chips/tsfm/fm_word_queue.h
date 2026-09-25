#pragma once

#include <stdafx.h>

#include <array>
#include <cstdint>

/// @file fm_word_queue.h
/// @brief Fixed-capacity FIFO of (T-state, DAC word) pairs - the hand-off
/// between the TSFM chip core and the output stage (design §5.1, §6.2).
///
/// The core loop pushes one word per FM sample, timestamped on the frame's
/// T-state axis. The output stage (P6) drains it on the 437.5 kHz half-tick
/// grid; when synthesis is suppressed the device clears it in
/// handleFrameStart instead. Frame rollover shifts every live timestamp by
/// the same delta (rebase), so words pending across the boundary stay on the
/// new frame's axis without being re-examined.

/// One queued FM DAC sample
struct FmWord
{
    uint64_t t = 0;     // frame-relative T-state; words carried over from the previous frame are negative (two's complement, compare as int64_t)
    int16_t word = 0;   // YM3014 16-bit DAC word (left channel)
};

class FmWordQueue
{
public:
    /// 4096 words ~= 4.1 frames of headroom at /6 (996 words per Pentagon
    /// frame) - the render loop consumes a frame's worth per frame and keeps
    /// only the words inside its constant lag (a few) queued across the
    /// boundary; the suppressed path clears at each frame start
    static constexpr size_t kCapacity = 4096;

    bool empty() const
    {
        return _count == 0;
    }

    size_t size() const
    {
        return _count;
    }

    /// Oldest live word
    const FmWord& front() const
    {
        return _items[_head];
    }

    /// i-th oldest live word (0 = front)
    const FmWord& at(size_t index) const
    {
        return _items[(_head + index) % kCapacity];
    }

    /// Enqueue; when full the oldest word is overwritten (the queue only
    /// fills when nobody drains - suppressed production - and the ring then
    /// bounds memory without any allocation)
    void push(uint64_t t, int16_t word)
    {
        _items[(_head + _count) % kCapacity] = FmWord{t, word};
        if (_count < kCapacity)
            _count++;
        else
            _head = (_head + 1) % kCapacity;  // was full: oldest replaced
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

    /// Shift every live timestamp by -shift (frame rollover, design §5.2)
    void rebase(int32_t shift)
    {
        for (size_t i = 0; i < _count; i++)
        {
            FmWord& w = _items[(_head + i) % kCapacity];
            w.t = uint64_t(int64_t(w.t) - int64_t(shift));
        }
    }

private:
    std::array<FmWord, kCapacity> _items{};
    size_t _head = 0;   // index of the oldest live word
    size_t _count = 0;
};
