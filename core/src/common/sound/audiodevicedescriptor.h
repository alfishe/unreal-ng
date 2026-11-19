#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include "common/sound/audioclock.h"

/// Realtime-observable state of the frontend audio device and its ring
/// buffer. Owned by the frontend's AppSoundManager (outlives any emulator
/// instance) and registered with the emulator via Emulator::SetAudioCallback;
/// core-side consumers (DRC controller, WebAPI monitoring, diagnostics) read
/// it lock-free at any time.
///
/// Field groups by writer:
///   - Device parameters: written on the GUI thread at device init / reroute
///     re-init only (the device is stopped at that moment)
///   - Live buffer state: written by the audio threads on every callback
struct AudioDeviceDescriptor
{
    /// region <Device parameters (init / reroute re-init)>

    /// Device native sample rate in Hz (0 = no device attached)
    std::atomic<uint32_t> sampleRate{0};

    /// Output channel count
    std::atomic<uint32_t> channels{0};

    /// Ring buffer capacity in stereo frames
    std::atomic<uint32_t> capacityFrames{0};

    /// Device hardware buffer depth in frames (period size x period count):
    /// audio already handed to the OS but not yet audible. Part of the audio
    /// presentation delay alongside ring occupancy.
    std::atomic<uint32_t> deviceBufferFrames{0};

    /// Additional output latency reported by the OS/driver BELOW our buffers
    /// (CoreAudio device latency + safety offset), in frames. Virtual
    /// loopback devices (e.g. Background Music, BlackHole) can report
    /// substantial values here - this is how a desync living OUTSIDE our
    /// pipeline is identified. 0 when unknown/unsupported.
    std::atomic<uint32_t> deviceOutputLatencyFrames{0};

    /// Number of device re-establishments (hotplug / OS default-output
    /// reroutes). 0 after the initial init.
    std::atomic<uint32_t> reinitCount{0};

    /// Human-readable device name. Written only during init/re-init while the
    /// device is stopped; readers may observe a torn value during the brief
    /// re-init window - acceptable for monitoring, never used for logic.
    char deviceName[128] = {};

    /// endregion </Device parameters>

    /// region <Live buffer state (audio threads)>

    /// Current ring occupancy in stereo frames. This is the DRC controller's
    /// process variable: occupancy = the audio-behind-video presentation
    /// offset (audio is delayed by exactly the ring content).
    std::atomic<uint32_t> occupancyFrames{0};

    /// Lifetime totals (stereo frames through the ring)
    std::atomic<uint64_t> framesEnqueued{0};
    std::atomic<uint64_t> framesDequeued{0};

    /// The ring's write position in its stream (AudioRingBuffer::getWrittenStereoFrames), published after every
    /// enqueue: where the sound the core just delivered ends. Unlike framesEnqueued it counts only what got in
    std::atomic<uint64_t> streamWritten{0};

    /// When a frame of the ring's stream is heard (fed by the device callback): the master clock video frames are
    /// timed by (frame presenter design, section 3)
    AudioClock clock;

    /// Ring over/underrun counters. Under converged rate control both stay
    /// at zero permanently; growth means the controller is not keeping up.
    std::atomic<uint64_t> enqueueErrors{0};
    std::atomic<uint64_t> dequeueErrors{0};

    /// The ring's trough: the least occupancy a device callback left behind, over the last whole window of
    /// kTroughWindowNs (the margin the ring kept against an underrun), and how many windows have closed (a reader
    /// polling twice a window sees each). kNoTrough until the first window closes
    static constexpr int64_t kTroughWindowNs = 1'000'000'000;
    static constexpr uint32_t kNoTrough = UINT32_MAX;
    std::atomic<uint32_t> troughFrames{kNoTrough};
    std::atomic<uint64_t> troughWindows{0};

    /// Gaps: a device callback that found the ring short of what it asked for after one that got all of it (a run
    /// of short callbacks, a paused machine's included, counts once)
    std::atomic<uint64_t> underruns{0};

    /// endregion </Live buffer state>

    /// The device callback, after it took its frames: the occupancy it left (device thread only)
    void OnCallbackConsumed(uint32_t occupancy, int64_t nowNs, bool shortRead)
    {
        if (shortRead && !_lastShort)
            underruns.fetch_add(1, std::memory_order_relaxed);
        _lastShort = shortRead;
        if (occupancy < _windowTrough)
            _windowTrough = occupancy;
        if (_windowStartNs == 0)
            _windowStartNs = nowNs;
        else if (nowNs - _windowStartNs >= kTroughWindowNs)
        {
            troughFrames.store(_windowTrough, std::memory_order_relaxed);
            troughWindows.fetch_add(1, std::memory_order_release);
            _windowTrough = kNoTrough;
            _windowStartNs = nowNs;
        }
    }

    /// A fresh device (init, re-init): the trough window starts over (device stopped)
    void ResetTrough()
    {
        _windowTrough = kNoTrough;
        _windowStartNs = 0;
        _lastShort = true;
        troughFrames.store(kNoTrough, std::memory_order_relaxed);
    }

    /// Ring occupancy in milliseconds at the current device rate
    /// (0.0 when no device is attached)
    double occupancyMs() const
    {
        const uint32_t rate = sampleRate.load(std::memory_order_relaxed);
        if (rate == 0)
            return 0.0;
        return occupancyFrames.load(std::memory_order_relaxed) * 1000.0 / rate;
    }

    /// Total audio presentation delay in milliseconds: ring occupancy plus
    /// the device hardware buffer. This is how far the audible sound trails
    /// the emulated frame that produced it.
    double audioLatencyMs() const
    {
        const uint32_t rate = sampleRate.load(std::memory_order_relaxed);
        if (rate == 0)
            return 0.0;
        return (occupancyFrames.load(std::memory_order_relaxed) +
                deviceBufferFrames.load(std::memory_order_relaxed) +
                deviceOutputLatencyFrames.load(std::memory_order_relaxed)) * 1000.0 / rate;
    }

    /// Set the device name (init/re-init only, device stopped)
    void setDeviceName(const char* name)
    {
        std::strncpy(deviceName, name ? name : "", sizeof(deviceName) - 1);
        deviceName[sizeof(deviceName) - 1] = '\0';
    }

private:
    // The open trough window (device thread only)
    uint32_t _windowTrough = kNoTrough;
    int64_t _windowStartNs = 0;
    bool _lastShort = true;  // a fresh device starts on an empty ring: not a gap
};
