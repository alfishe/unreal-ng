#pragma once

#include <vector>
#include <shared_mutex>
#include <mutex>
#include <cstdint>
#include <algorithm>

/// Thread-safe ring buffer with FIFO eviction.
/// @tparam T Event type (must have `timestamp` member for filtering)
template<typename T>
class RingBuffer
{
public:
    explicit RingBuffer(size_t capacity)
        : _buffer(capacity), _capacity(capacity), _head(0), _count(0),
          _totalProduced(0), _totalEvicted(0)
    {
    }

    /// Push event (thread-safe, evicts oldest if full)
    void push(T&& event)
    {
        std::unique_lock lock(_mutex);
        
        if (_count == _capacity)
        {
            // Buffer full: evict oldest
            _totalEvicted++;
        }
        else
        {
            _count++;
        }

        size_t index = (_head + _count - 1) % _capacity;
        _buffer[index] = std::move(event);
        
        if (_count == _capacity)
        {
            _head = (_head + 1) % _capacity;
        }
        
        _totalProduced++;
    }

    /// Push event (copy version)
    void push(const T& event)
    {
        T copy = event;
        push(std::move(copy));
    }

    /// Get all events (thread-safe)
    std::vector<T> getAll() const
    {
        std::shared_lock lock(_mutex);
        
        std::vector<T> result;
        result.reserve(_count);
        
        for (size_t i = 0; i < _count; i++)
        {
            size_t index = (_head + i) % _capacity;
            result.push_back(_buffer[index]);
        }
        
        return result;
    }

    /// Get events since timestamp (thread-safe)
    std::vector<T> getSince(uint64_t timestamp) const
    {
        std::shared_lock lock(_mutex);
        
        std::vector<T> result;
        
        for (size_t i = 0; i < _count; i++)
        {
            size_t index = (_head + i) % _capacity;
            if (_buffer[index].timestamp >= timestamp)
            {
                result.push_back(_buffer[index]);
            }
        }
        
        return result;
    }

    /// Get range of events (thread-safe)
    std::vector<T> getRange(size_t start, size_t count) const
    {
        std::shared_lock lock(_mutex);
        
        std::vector<T> result;
        size_t actualStart = std::min(start, _count);
        size_t actualCount = std::min(count, _count - actualStart);
        result.reserve(actualCount);
        
        for (size_t i = 0; i < actualCount; i++)
        {
            size_t index = (_head + actualStart + i) % _capacity;
            result.push_back(_buffer[index]);
        }
        
        return result;
    }

    /// Clear all events (thread-safe)
    void clear()
    {
        std::unique_lock lock(_mutex);
        _head = 0;
        _count = 0;
        // Note: _totalProduced and _totalEvicted are NOT reset
    }

    // Accessors (thread-safe)
    
    size_t size() const
    {
        std::shared_lock lock(_mutex);
        return _count;
    }

    size_t capacity() const { return _capacity; }

    bool isEmpty() const
    {
        std::shared_lock lock(_mutex);
        return _count == 0;
    }

    bool isFull() const
    {
        std::shared_lock lock(_mutex);
        return _count == _capacity;
    }

    uint64_t totalEventsProduced() const
    {
        std::shared_lock lock(_mutex);
        return _totalProduced;
    }

    uint64_t totalEventsEvicted() const
    {
        std::shared_lock lock(_mutex);
        return _totalEvicted;
    }

private:
    std::vector<T> _buffer;
    size_t _capacity;
    size_t _head;       // Index of oldest event
    size_t _count;      // Current number of events

    uint64_t _totalProduced;
    uint64_t _totalEvicted;

    mutable std::shared_mutex _mutex;
};
