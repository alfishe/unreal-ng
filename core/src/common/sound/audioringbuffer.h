#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>

template <typename T, size_t Size>
class AudioRingBuffer
{
protected:
    T _buffer[Size];
    size_t _readPtr;
    size_t _writePtr;

    /// region <Debug>
protected:
    size_t _enqueueErrorCount = 0;
    size_t _dequeueErrorCount = 0;
    /// endregion <Debug>

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
        return _readPtr == _writePtr;
    }

    inline bool isHalfFull() const
    {
        int occupiedFrames = getOccupiedFrames();
        bool result = occupiedFrames >= Size >> 1;

        return result;
    }

    inline bool isFull() const
    {
        return getBufferIndex(_writePtr, 1) == _readPtr;
    }

    size_t enqueue(const T* samples, size_t numSamples)
    {
        size_t result = 0;

        if (isFull())
        {
            _enqueueErrorCount++;
            //std::cout << StringHelper::Format("[%06d] Buffer is full. Unable to enqueue samples.", _enqueueErrorCount) << std::endl;
            return result;
        }

        size_t availableSpace = getAvailableSpace();
        size_t samplesToCopy = std::min(availableSpace, numSamples);

        size_t firstChunk = std::min(samplesToCopy, Size - _writePtr);
        memcpy(_buffer + _writePtr, samples, firstChunk * sizeof(T));

        size_t secondChunk = samplesToCopy - firstChunk;
        if (secondChunk > 0)
        {
            memcpy(_buffer, samples + firstChunk, secondChunk * sizeof(T));
        }

        _writePtr = getBufferIndex(_writePtr, samplesToCopy);

        result = samplesToCopy;

        return result;
    }

    size_t dequeue(T* samples, size_t numSamples)
    {
        size_t result = 0;

        if (isEmpty())
        {
            _dequeueErrorCount++;
            //std::cout << StringHelper::Format("[%06d] Buffer is empty. Unable to dequeue samples. Requested %d", _dequeueErrorCount, numSamples) << std::endl;

            return result;
        }

        size_t availableData = getAvailableData();
        size_t samplesToCopy = std::min(availableData, numSamples);

        size_t firstChunk = std::min(samplesToCopy, Size - _readPtr);
        memcpy(samples, _buffer + _readPtr, firstChunk * sizeof(T));

        size_t secondChunk = samplesToCopy - firstChunk;
        memcpy(samples + firstChunk, _buffer, secondChunk * sizeof(T));

        _readPtr = getBufferIndex(_readPtr, samplesToCopy);

        result = samplesToCopy;

        /// region <Debug>
        //std::cout << StringHelper::Format("Requested %d, returned %d, stored %d", numSamples, result, getAvailableData()) << std::endl;

        /// endregion </Debug>

        return result;
    }

    inline size_t getAvailableSpace() const
    {
        size_t result = (_readPtr - _writePtr + Size - 1) % Size;

        return result;
    }

    inline size_t getAvailableData() const
    {
        size_t result = (_writePtr - _readPtr + Size) % Size;

        return result;
    }

    inline size_t size() const
    {
        return Size;
    }

private:
    inline size_t getBufferIndex(size_t index, size_t increment = 1) const
    {
        size_t result = (index + increment) % Size;

        return result;
    }

    // Helper function to calculate the number of occupied frames in the buffer
    inline int getOccupiedFrames() const
    {
        int result;

        if (_writePtr >= _readPtr)
        {
            result = _writePtr - _readPtr;
        }
        else
        {
            result = Size - (_readPtr - _writePtr);
        }

        return result;
    }
};