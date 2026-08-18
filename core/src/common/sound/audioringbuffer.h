#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

/// Lock-free SPSC ring buffer for interleaved stereo int16 audio.
///
/// Threading contract: exactly ONE producer (emulation thread, enqueue) and
/// ONE consumer (audio device thread, dequeue). Indices are atomics with
/// release/acquire ordering so each side observes a consistent view of the
/// other's progress - previously they were plain size_t, a genuine data race.
///
/// Units: the buffer stores int16 values; one stereo frame = 2 values.
/// Occupancy for rate-control purposes should be read via
/// getOccupancyStereoFrames() (audio-sync design: ring occupancy IS the A/V
/// offset, and is the DRC controller's process variable).
template <typename T, size_t Size>
class AudioRingBuffer
{
protected:
    T _buffer[Size];
    std::atomic<size_t> _readPtr;
    std::atomic<size_t> _writePtr;

    /// region <Diagnostics>
protected:
    std::atomic<size_t> _enqueueErrorCount{0};  // Full-buffer drops (producer)
    std::atomic<size_t> _dequeueErrorCount{0};  // Empty-buffer underruns (consumer)
    /// endregion <Diagnostics>

public:
    AudioRingBuffer() : _readPtr{0}, _writePtr{0}
    {
        // Clear buffer to eliminate noise on audio playback start
        clear();
    }

    void clear()
    {
        std::fill(_buffer, _buffer + Size, 0);
    }

    inline bool isEmpty() const
    {
        return _readPtr.load(std::memory_order_acquire) == _writePtr.load(std::memory_order_acquire);
    }

    inline bool isHalfFull() const
    {
        return getAvailableData() >= (Size >> 1);
    }

    inline bool isFull() const
    {
        return getBufferIndex(_writePtr.load(std::memory_order_acquire), 1) ==
               _readPtr.load(std::memory_order_acquire);
    }

    /// Producer side only
    size_t enqueue(const T* samples, size_t numSamples)
    {
        size_t writePtr = _writePtr.load(std::memory_order_relaxed);   // Own index
        size_t readPtr = _readPtr.load(std::memory_order_acquire);     // Consumer's progress

        size_t availableSpace = (readPtr - writePtr + Size - 1) % Size;
        if (availableSpace == 0)
        {
            _enqueueErrorCount.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

        size_t samplesToCopy = std::min(availableSpace, numSamples);
        if (samplesToCopy < numSamples)
        {
            // Partial write = dropped tail; count it so DRC convergence
            // problems are observable rather than silent
            _enqueueErrorCount.fetch_add(1, std::memory_order_relaxed);
        }

        size_t firstChunk = std::min(samplesToCopy, Size - writePtr);
        memcpy(_buffer + writePtr, samples, firstChunk * sizeof(T));

        size_t secondChunk = samplesToCopy - firstChunk;
        if (secondChunk > 0)
        {
            memcpy(_buffer, samples + firstChunk, secondChunk * sizeof(T));
        }

        // Publish: data writes must be visible before the index moves
        _writePtr.store(getBufferIndex(writePtr, samplesToCopy), std::memory_order_release);

        return samplesToCopy;
    }

    /// Consumer side only
    size_t dequeue(T* samples, size_t numSamples)
    {
        size_t readPtr = _readPtr.load(std::memory_order_relaxed);     // Own index
        size_t writePtr = _writePtr.load(std::memory_order_acquire);   // Producer's progress

        size_t availableData = (writePtr - readPtr + Size) % Size;
        if (availableData == 0)
        {
            _dequeueErrorCount.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

        size_t samplesToCopy = std::min(availableData, numSamples);

        size_t firstChunk = std::min(samplesToCopy, Size - readPtr);
        memcpy(samples, _buffer + readPtr, firstChunk * sizeof(T));

        size_t secondChunk = samplesToCopy - firstChunk;
        if (secondChunk > 0)
        {
            memcpy(samples + firstChunk, _buffer, secondChunk * sizeof(T));
        }

        _readPtr.store(getBufferIndex(readPtr, samplesToCopy), std::memory_order_release);

        return samplesToCopy;
    }

    inline size_t getAvailableSpace() const
    {
        size_t readPtr = _readPtr.load(std::memory_order_acquire);
        size_t writePtr = _writePtr.load(std::memory_order_acquire);
        return (readPtr - writePtr + Size - 1) % Size;
    }

    inline size_t getAvailableData() const
    {
        size_t readPtr = _readPtr.load(std::memory_order_acquire);
        size_t writePtr = _writePtr.load(std::memory_order_acquire);
        return (writePtr - readPtr + Size) % Size;
    }

    /// Ring occupancy in STEREO FRAMES (the natural unit for latency and for
    /// the DRC controller; ends the int16-vs-frames unit confusion)
    inline size_t getOccupancyStereoFrames() const
    {
        return getAvailableData() / 2;
    }

    inline size_t getEnqueueErrorCount() const
    {
        return _enqueueErrorCount.load(std::memory_order_relaxed);
    }

    inline size_t getDequeueErrorCount() const
    {
        return _dequeueErrorCount.load(std::memory_order_relaxed);
    }

    inline size_t size() const
    {
        return Size;
    }

private:
    inline size_t getBufferIndex(size_t index, size_t increment = 1) const
    {
        return (index + increment) % Size;
    }
};
