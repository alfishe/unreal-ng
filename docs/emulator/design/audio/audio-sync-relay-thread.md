# Audio Sync Relay Thread Design

## Problem Statement

The current audio callback implementation violates real-time audio thread constraints by calling `MessageCenter::Post()` directly from the Core Audio HAL thread:

```cpp
// Current implementation in audioDataCallback (PROBLEMATIC)
if (!obj->_ringBuffer.isHalfFull())
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_AUDIO_BUFFER_HALF_FULL);  // Mutex + heap + condvar
}
```

This causes `HALC_ProxyIOContext::IOWorkLoop: skipping cycle due to overload` warnings on macOS, especially in debug builds, because `Post()` performs:
1. **Mutex lock** (`std::unique_lock`) — can block indefinitely (priority inversion)
2. **Heap allocation** (`new Message(...)`) — unbounded latency, 10-100x slower in debug
3. **Condition variable notify** — kernel context switch

## Requirements

| Requirement | Target |
|-------------|--------|
| Audio callback to relay latency | < 1ms (sub-millisecond) |
| Audio callback blocking time | 0 (no blocking operations) |
| Preserve existing broadcast semantics | All MainLoops receive notification |
| Support emulator rebinding | Safe during emulator switch |
| Cross-platform | macOS, Windows, Linux |

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Audio Thread (Real-time)                        │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  audioDataCallback()                                            │    │
│  │    1. ringBuffer.dequeue() — memcpy only                        │    │
│  │    2. if (!isHalfFull()) {                                      │    │
│  │         atomic_flag.store(true);     // ~10ns                   │    │
│  │         semaphore.post();            // ~1μs kernel call        │    │
│  │       }                                                         │    │
│  └──────────────────────────┬──────────────────────────────────────┘    │
└─────────────────────────────┼───────────────────────────────────────────┘
                              │ ~1-10μs
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         Relay Thread (Normal priority)                  │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  while (running) {                                              │    │
│  │      semaphore.wait();              // Block until signaled     │    │
│  │      if (flag.exchange(false)) {                                │    │
│  │          MessageCenter::Post(NC_AUDIO_BUFFER_HALF_FULL);        │    │
│  │      }                                                          │    │
│  │  }                                                              │    │
│  └──────────────────────────┬──────────────────────────────────────┘    │
└─────────────────────────────┼───────────────────────────────────────────┘
                              │ ~50-200μs
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         MessageCenter Thread                            │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  Dispatch(NC_AUDIO_BUFFER_HALF_FULL)                            │    │
│  │    → MainLoop 1: handleAudioBufferHalfFull() → _cv.notify_one() │    │
│  │    → MainLoop 2: handleAudioBufferHalfFull() → _cv.notify_one() │    │
│  │    → MainLoop N: handleAudioBufferHalfFull() → _cv.notify_one() │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

### Latency Budget

| Stage | Latency | Notes |
|-------|---------|-------|
| Atomic store | ~10ns | Single CPU instruction |
| Semaphore post | ~1μs | Kernel call, non-blocking |
| Semaphore wake | ~10-50μs | Context switch to relay thread |
| MessageCenter Post | ~50-200μs | Mutex + heap (now in relay thread) |
| MessageCenter Dispatch | ~10-50μs | Observer iteration |
| MainLoop CV notify | ~1μs | Per MainLoop |
| **Total** | **~100-500μs** | Well under 1ms requirement |

## Detailed Design

### 1. Signaling Primitive Selection

#### Option A: Binary Semaphore (Recommended)

```cpp
// Cross-platform semaphore wrapper
#ifdef __APPLE__
    #include <dispatch/dispatch.h>
    dispatch_semaphore_t _semaphore;
#elif defined(_WIN32)
    #include <windows.h>
    HANDLE _semaphore;
#else
    #include <semaphore.h>
    sem_t _semaphore;
#endif
```

**Pros:**
- Minimal latency (~1μs signal, ~10-50μs wake)
- No mutex required for signaling
- Platform-native implementation

**Cons:**
- Platform-specific code required

#### Option B: std::atomic + std::condition_variable

```cpp
std::atomic<bool> _needsRefill{false};
std::mutex _relayMutex;
std::condition_variable _relayCV;

// Audio callback (no lock needed for notify_one):
_needsRefill.store(true, std::memory_order_release);
_relayCV.notify_one();  // Safe without holding mutex

// Relay thread:
std::unique_lock<std::mutex> lock(_relayMutex);
_relayCV.wait(lock, [this] { return _needsRefill.load() || _stopRequested; });
```

**Pros:**
- Pure C++11/14/17, no platform code
- `notify_one()` does NOT require holding the mutex

**Cons:**
- Slightly higher latency than semaphore (~5-20μs extra)

#### Option C: C++20 std::counting_semaphore

```cpp
#include <semaphore>
std::binary_semaphore _semaphore{0};

// Audio callback:
_semaphore.release();

// Relay thread:
_semaphore.acquire();
```

**Pros:**
- Clean, standard C++20 API
- Platform-optimized implementation

**Cons:**
- Requires C++20

### Recommendation

Use **Option B** (atomic + condition_variable) for C++17 compatibility with the existing codebase. The ~10-20μs extra latency is negligible compared to the 20ms frame budget.

### 2. AudioSignalRelay Class

Location: `unreal-qt/src/emulator/audiosignalrelay.h`

```cpp
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

/**
 * @brief Lock-free relay between real-time audio thread and MessageCenter.
 *
 * The audio callback signals via atomic + CV (sub-millisecond latency).
 * A dedicated relay thread receives the signal and posts to MessageCenter.
 * This keeps blocking operations out of the real-time audio thread.
 */
class AudioSignalRelay
{
public:
    AudioSignalRelay();
    ~AudioSignalRelay();

    // Non-copyable
    AudioSignalRelay(const AudioSignalRelay&) = delete;
    AudioSignalRelay& operator=(const AudioSignalRelay&) = delete;

    /**
     * @brief Start the relay thread.
     * @note Must be called before signalBufferNeedsRefill().
     */
    void start();

    /**
     * @brief Stop the relay thread and wait for it to exit.
     */
    void stop();

    /**
     * @brief Signal that audio buffer needs refill.
     * @note REAL-TIME SAFE: Only atomic store + CV notify (no lock).
     *       Call this from the audio callback.
     */
    void signalBufferNeedsRefill();

private:
    void relayThreadFunc();

    std::thread _relayThread;
    std::atomic<bool> _needsRefill{false};
    std::atomic<bool> _stopRequested{false};
    std::atomic<bool> _isRunning{false};

    std::mutex _relayMutex;
    std::condition_variable _relayCV;
};
```

### 3. Implementation

Location: `unreal-qt/src/emulator/audiosignalrelay.cpp`

```cpp
#include "audiosignalrelay.h"
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/platform.h"

#ifdef __APPLE__
#include <pthread.h>
#endif

AudioSignalRelay::AudioSignalRelay() = default;

AudioSignalRelay::~AudioSignalRelay()
{
    stop();
}

void AudioSignalRelay::start()
{
    if (_isRunning.load())
        return;

    _stopRequested.store(false);
    _isRunning.store(true);
    _relayThread = std::thread(&AudioSignalRelay::relayThreadFunc, this);
}

void AudioSignalRelay::stop()
{
    if (!_isRunning.load())
        return;

    _stopRequested.store(true);
    _relayCV.notify_one();  // Wake up if waiting

    if (_relayThread.joinable())
        _relayThread.join();

    _isRunning.store(false);
}

void AudioSignalRelay::signalBufferNeedsRefill()
{
    // REAL-TIME SAFE: No mutex lock, just atomic + notify
    // notify_one() is safe to call without holding the mutex
    _needsRefill.store(true, std::memory_order_release);
    _relayCV.notify_one();
}

void AudioSignalRelay::relayThreadFunc()
{
    // Set thread name for debugging
#ifdef __APPLE__
    pthread_setname_np("audio_signal_relay");
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), "audio_signal_relay");
#endif

    while (!_stopRequested.load(std::memory_order_acquire))
    {
        // Wait for signal from audio callback
        {
            std::unique_lock<std::mutex> lock(_relayMutex);
            _relayCV.wait(lock, [this] {
                return _needsRefill.load(std::memory_order_acquire) ||
                       _stopRequested.load(std::memory_order_acquire);
            });
        }

        // Check if we should exit
        if (_stopRequested.load(std::memory_order_acquire))
            break;

        // Consume the signal and post to MessageCenter
        // exchange() atomically reads and clears the flag
        if (_needsRefill.exchange(false, std::memory_order_acq_rel))
        {
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(NC_AUDIO_BUFFER_HALF_FULL);
        }
    }
}
```

### 4. Integration with AppSoundManager

#### Header Changes (`soundmanager.h`):

```cpp
#pragma once

#include <QObject>
#include <3rdparty/miniaudio/miniaudio.h>
#include <common/sound/audioringbuffer.h>
#include <emulator/sound/soundmanager.h>
#include "audiosignalrelay.h"  // NEW

class AppSoundManager : QObject
{
    Q_OBJECT

protected:
    ma_device _audioDevice;
    AudioRingBuffer<int16_t, AUDIO_BUFFER_SAMPLES_PER_FRAME * 8> _ringBuffer;
    AudioSignalRelay _signalRelay;  // NEW

public:
    AppSoundManager() = default;
    virtual ~AppSoundManager();

    bool init();
    void deinit();
    void start();
    void stop();

    static void audioDataCallback(ma_device* pDevice, void* pOutput,
                                   const void* pInput, ma_uint32 frameCount);
    static void audioCallback(void* obj, int16_t* samples, size_t numSamples);
};
```

#### Implementation Changes (`soundmanager.cpp`):

```cpp
bool AppSoundManager::init()
{
    // ... existing miniaudio init code ...

    // Start the signal relay thread
    _signalRelay.start();

    return result;
}

void AppSoundManager::deinit()
{
    stop();

    // Stop relay thread before audio device
    _signalRelay.stop();

    ma_device_uninit(&_audioDevice);
}

void AppSoundManager::audioDataCallback(ma_device* pDevice, void* pOutput,
                                         const void* pInput, ma_uint32 frameCount)
{
    AppSoundManager* obj = (AppSoundManager*)pDevice->pUserData;

    if (obj)
    {
        obj->_ringBuffer.dequeue((int16_t*)pOutput, frameCount * 2);

        if (!obj->_ringBuffer.isHalfFull())
        {
            // CHANGED: Use relay instead of direct MessageCenter call
            obj->_signalRelay.signalBufferNeedsRefill();
        }
    }

    (void)pInput;
}
```

## Thread Safety Analysis

### Emulator Rebinding Scenario

```
Time ──────────────────────────────────────────────────────────────────►

Emulator 1 running                    Emulator 2 becomes active
     │                                       │
     │  Audio callback fires                 │
     │  ──► signalRelay.signal()             │
     │                                       │
     │        Relay thread wakes             │
     │        ──► Post(NC_AUDIO_...)         │
     │                                       │
     │           MessageCenter dispatches    │
     │           ──► MainLoop 1 receives     │ ◄── Still subscribed
     │           ──► MainLoop 2 receives     │ ◄── Now active
     │                                       │
     │  unbindFromEmulator() called          │
     │  MainLoop 1 unsubscribes              │
     │                                       │
```

**Why this is safe:**

1. **Broadcast semantics preserved**: All subscribed MainLoops receive the notification
2. **No MainLoop pointer stored**: AudioSignalRelay doesn't know about MainLoops
3. **MessageCenter handles subscription/unsubscription atomically**: Uses mutexes internally
4. **Only one emulator generates audio**: Audio callback is bound to one emulator at a time via `SetAudioCallback()`
5. **Stale notifications are harmless**: If old MainLoop receives notification after unbind, it's already unsubscribed

### Race Condition Analysis

| Scenario | Audio Thread | Relay Thread | Safe? |
|----------|--------------|--------------|-------|
| Normal signal | `flag=true; notify()` | `wait(); if(flag.exchange(false)) Post()` | Yes |
| Multiple signals before relay wakes | `flag=true` (already true) | Coalesced to single Post | Yes |
| Stop during wait | — | `stopRequested=true; notify()` | Yes |
| Signal during rebind | Signal goes to MessageCenter | All MainLoops receive | Yes |

### Memory Ordering

```cpp
// Audio thread (producer)
_needsRefill.store(true, std::memory_order_release);  // Ensures visibility

// Relay thread (consumer)
_needsRefill.load(std::memory_order_acquire);  // Sees producer's write
_needsRefill.exchange(false, std::memory_order_acq_rel);  // Read-modify-write
```

## Multi-Instance Considerations (VideoWall)

The VideoWall application can run multiple emulator instances, but only **one** has audio bound at a time:

```cpp
// VideoWallWindow.cpp:765
emulator->SetAudioCallback(_soundManager, &AppSoundManager::audioCallback);
```

The design handles this correctly:

1. **Single AppSoundManager per app**: Owns the audio device and relay thread
2. **Single audio callback binding**: Only one emulator fills the ring buffer
3. **Broadcast notification**: All MainLoops receive `NC_AUDIO_BUFFER_HALF_FULL`
4. **Per-MainLoop filtering**: Each MainLoop only paces its own emulator

## Testing Strategy

### Unit Tests

1. **Relay latency test**: Measure time from `signalBufferNeedsRefill()` to MessageCenter `Post()`
   - Target: < 500μs average, < 1ms p99

2. **Signal coalescing test**: Rapid signals should coalesce to single Post
   - Send 100 signals in 1ms, verify ~1 Post

3. **Start/stop lifecycle**: Verify clean shutdown with no hangs

4. **Stress test**: 10,000 signals, verify no crashes or leaks

### Integration Tests

1. **Audio playback test**: Verify audio plays without glitches
2. **Emulator switch test**: Switch emulators rapidly, verify no crashes
3. **Debug build test**: Run in debug mode, verify no HAL warnings

### Manual Verification

1. Run `unreal-qt` in debug mode
2. Start emulation with audio
3. Verify no `HALC_ProxyIOContext::IOWorkLoop: skipping cycle` messages
4. Verify audio quality unchanged

## Performance Comparison

| Metric | Before (MessageCenter in callback) | After (Relay thread) |
|--------|-------------------------------------|----------------------|
| Audio callback time | 50-500μs (debug) | < 10μs |
| HAL overload warnings | Frequent in debug | None |
| Frame sync latency | ~50-200μs | ~100-500μs (+100-300μs) |
| Audio quality | Good | Good |

The slight increase in sync latency (100-300μs) is negligible compared to the 20ms frame period.

## Files to Modify

| File | Change |
|------|--------|
| `unreal-qt/src/emulator/audiosignalrelay.h` | NEW - Relay class header |
| `unreal-qt/src/emulator/audiosignalrelay.cpp` | NEW - Relay class implementation |
| `unreal-qt/src/emulator/soundmanager.h` | Add `AudioSignalRelay _signalRelay` member |
| `unreal-qt/src/emulator/soundmanager.cpp` | Use relay in `audioDataCallback()`, lifecycle |
| `unreal-qt/CMakeLists.txt` | Add new source files |
| `unreal-videowall/src/emulator/audiosignalrelay.h` | Copy or share with unreal-qt |
| `unreal-videowall/src/emulator/audiosignalrelay.cpp` | Copy or share with unreal-qt |
| `unreal-videowall/src/emulator/soundmanager.h` | Same changes as unreal-qt |
| `unreal-videowall/src/emulator/soundmanager.cpp` | Same changes as unreal-qt |
| `unreal-videowall/CMakeLists.txt` | Add new source files |

## Future Considerations

### Move AudioSignalRelay to Core

If more applications need this pattern, consider moving `AudioSignalRelay` to `core/src/common/` for reuse.

### Direct MainLoop Signaling (Alternative)

For maximum efficiency, the relay could signal MainLoops directly instead of via MessageCenter:

```cpp
// In AudioSignalRelay
std::vector<std::atomic<bool>*> _mainLoopFlags;

void signalBufferNeedsRefill()
{
    for (auto* flag : _mainLoopFlags)
        flag->store(true, std::memory_order_release);
    // Signal all MainLoop CVs...
}
```

This would remove MessageCenter overhead entirely but adds complexity for MainLoop registration.

### C++20 Semaphore Upgrade

When the project moves to C++20, replace the condition_variable with `std::binary_semaphore` for cleaner code and potentially lower latency.

## Conclusion

The AudioSignalRelay design solves the Core Audio HAL overload issue by:

1. **Removing all blocking operations from audio callback**
2. **Maintaining sub-millisecond latency** (~100-500μs total)
3. **Preserving existing broadcast semantics** (no MainLoop changes required)
4. **Supporting safe emulator rebinding** (no race conditions)
5. **Minimal code changes** (new class + minor integration)

The implementation is straightforward, testable, and follows established patterns in the codebase.
