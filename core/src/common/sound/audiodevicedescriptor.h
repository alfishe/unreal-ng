#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

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

    /// Ring over/underrun counters. Under converged rate control both stay
    /// at zero permanently; growth means the controller is not keeping up.
    std::atomic<uint64_t> enqueueErrors{0};
    std::atomic<uint64_t> dequeueErrors{0};

    /// endregion </Live buffer state>

    /// Ring occupancy in milliseconds at the current device rate
    /// (0.0 when no device is attached)
    double occupancyMs() const
    {
        const uint32_t rate = sampleRate.load(std::memory_order_relaxed);
        if (rate == 0)
            return 0.0;
        return occupancyFrames.load(std::memory_order_relaxed) * 1000.0 / rate;
    }

    /// Set the device name (init/re-init only, device stopped)
    void setDeviceName(const char* name)
    {
        std::strncpy(deviceName, name ? name : "", sizeof(deviceName) - 1);
        deviceName[sizeof(deviceName) - 1] = '\0';
    }
};
