# RingBuffer Thread-Safety Implementation

## Purpose

Thread-safe ring buffer for both RawEvent and SemanticEvent storage in the analyzer framework.

## Concurrent Access Patterns

**Writer thread**: Emulation thread (Z80 execution, breakpoints, port I/O)  
**Reader threads**: WebAPI, UI, export threads

## API Design

```cpp
template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity);
    
    // Writer operations
    void push(T&& event);                                    // Emulation thread
    void push(const T& event);
    void clear();
    
    // Reader operations
    std::vector<T> getAll() const;                          // WebAPI/UI threads
    std::vector<T> getSince(uint64_t timestamp) const;
    std::vector<T> getRange(size_t start, size_t count) const;
    
    size_t size() const;
    size_t capacity() const;
    bool isEmpty() const;
    bool isFull() const;
    
    //Statistics
    uint64_t totalEventsProduced() const;
    uint64_t totalEventsEvicted() const;
    
private:
    std::vector<T> _buffer;
    size_t _capacity, _head, _tail, _count;
    uint64_t _totalProduced, _totalEvicted;
    mutable std::shared_mutex _mutex;  // C++17
};
```

## Thread-Safety Strategy

**Lock**: `std::shared_mutex`
- Shared lock (read): Multiple concurrent readers
- Exclusive lock (write): Single writer

## Implementation

```cpp
void RingBuffer<T>::push(T&& event) {
    std::unique_lock lock(_mutex);  // Exclusive
    
    if (_count == _capacity) {
        _tail = (_tail + 1) % _capacity;
        _totalEvicted++;
    } else {
        _count++;
    }
    
    _buffer[_head] = std::move(event);
    _head = (_head + 1) % _capacity;
    _totalProduced++;
}

std::vector<T> RingBuffer<T>::getSince(uint64_t timestamp) const {
    std::shared_lock lock(_mutex);  // Shared (multiple readers OK)
    
    std::vector<T> result;
    size_t index = _tail;
    for (size_t i = 0; i < _count; i++) {
        if (_buffer[index].timestamp >= timestamp) {
            result.push_back(_buffer[index]);
        }
        index = (index + 1) % _capacity;
    }
    return result;
}
```

## Performance

- Write frequency: ~14K events/sec (260 events per 18.75ms sector)
- Read frequency: API 1-10 Hz, UI 50 Hz
- Contention: Low (writes dominate)

## Usage

```cpp
class TRDOSAnalyzer : public IAnalyzer {
private:
    RingBuffer<RawEvent> _rawEvents{512};        // 8 KB
    RingBuffer<TRDOSEvent> _semanticEvents{10000}; // 480 KB
};
```

## Reusability

Generic for all analyzers:
- `RingBuffer<AYEvent>` — Sound analyzer  
- `RingBuffer<ULAEvent>` — Video analyzer
- `RingBuffer<CPUEvent>` — CPU trace

**Location**: `core/src/debugger/common/ringbuffer.h`
