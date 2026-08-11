#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

/// @brief Lock-free SPSC tap for native-rate (pre-decimation) TurboSound samples
///
/// Producer: emulation thread — SoundChip_TurboSound::handleStep() pushes one
/// stereo frame per generator tick (PSG_CLOCK_RATE / 8 ≈ 218.75 kHz).
/// Consumer: DSD encoder worker thread — pops frames, converts to DSD, writes DSF.
///
/// Cost when inactive: one relaxed atomic load + predictable branch per tick.
/// The frame buffer is allocated on first activate() and kept until destruction,
/// so a producer racing a deactivate() never touches freed memory.
class NativeAudioTap
{
public:
    static constexpr size_t DEFAULT_CAPACITY_FRAMES = 65536;  // ~300 ms at 218.75 kHz

    /// Consumer side: allocate ring (if needed) and open the tap.
    /// Not thread-safe against itself — call from one control thread only.
    void activate(size_t capacityFrames = DEFAULT_CAPACITY_FRAMES)
    {
        if (_capacity != capacityFrames)
        {
            // Round up to power of two for cheap masking
            size_t cap = 1;
            while (cap < capacityFrames)
                cap <<= 1;

            _buffer.assign(cap * 2, 0.0f);
            _capacity = cap;
        }

        _readIndex.store(0, std::memory_order_relaxed);
        _writeIndex.store(0, std::memory_order_relaxed);
        _overruns.store(0, std::memory_order_relaxed);
        _active.store(true, std::memory_order_release);
    }

    /// Consumer side: close the tap. The producer may complete one in-flight
    /// push afterwards — harmless, the buffer stays allocated.
    void deactivate()
    {
        _active.store(false, std::memory_order_release);
    }

    bool isActive() const
    {
        return _active.load(std::memory_order_relaxed);
    }

    /// Producer side (emulation thread). Drops the frame if the ring is full.
    inline void push(float left, float right)
    {
        size_t w = _writeIndex.load(std::memory_order_relaxed);
        size_t r = _readIndex.load(std::memory_order_acquire);

        if (w - r >= _capacity)
        {
            _overruns.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        size_t idx = (w & (_capacity - 1)) * 2;
        _buffer[idx] = left;
        _buffer[idx + 1] = right;

        _writeIndex.store(w + 1, std::memory_order_release);
    }

    /// Consumer side: pop up to maxFrames stereo frames into interleaved buffer.
    /// @return Number of frames actually read
    size_t pop(float* interleaved, size_t maxFrames)
    {
        size_t r = _readIndex.load(std::memory_order_relaxed);
        size_t w = _writeIndex.load(std::memory_order_acquire);

        size_t available = w - r;
        size_t toRead = available < maxFrames ? available : maxFrames;

        for (size_t i = 0; i < toRead; i++)
        {
            size_t idx = ((r + i) & (_capacity - 1)) * 2;
            interleaved[i * 2] = _buffer[idx];
            interleaved[i * 2 + 1] = _buffer[idx + 1];
        }

        _readIndex.store(r + toRead, std::memory_order_release);
        return toRead;
    }

    /// Frames currently buffered (consumer-side estimate)
    size_t available() const
    {
        return _writeIndex.load(std::memory_order_acquire) - _readIndex.load(std::memory_order_relaxed);
    }

    uint64_t overruns() const
    {
        return _overruns.load(std::memory_order_relaxed);
    }

private:
    std::vector<float> _buffer;  // Interleaved stereo frames, capacity * 2 floats
    size_t _capacity = 0;        // Power of two, in frames

    std::atomic<size_t> _writeIndex{0};  // Monotonic, masked on access
    std::atomic<size_t> _readIndex{0};
    std::atomic<bool> _active{false};
    std::atomic<uint64_t> _overruns{0};
};
