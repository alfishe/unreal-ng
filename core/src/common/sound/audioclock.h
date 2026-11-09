#pragma once

#include <atomic>
#include <cstdint>

/// The audio device as a clock: when a sample of the ring's stream is heard, in steady-clock nanoseconds
/// (frame presenter design, docs/inprogress/2026-10-26-frame-presenter, section 3).
///
/// The device consumes samples at its crystal's rate. Each device callback gives one pair: the device position
/// (stereo frames requested so far) and the callback's host time. The clock fits a line through these pairs
/// (exponentially weighted least squares over ~256 callbacks, 1.5-2.5 s): the callbacks' times jitter by
/// milliseconds, the line does not. The ring's stream maps onto the device's by one offset, taken at every
/// callback that found data: an underrun (silence the device played) or a discard (stream the device never
/// played) moves it, and the stream's later samples with it.
///
/// Threading: one writer, the device thread (OnDeviceCallback). Reset from any thread: the writer applies it at
/// its next callback, readers see the clock invalid at once. Readers on any thread (TimeOfStreamFrame): the fit is
/// published through a sequence lock, never blocking the writer.
class AudioClock
{
public:
    /// Callbacks before the fit counts as valid (~190 ms at 256-frame periods, 320 ms at WASAPI's 10 ms)
    static constexpr uint32_t kMinCallbacks = 32;
    /// Forgetting factor per callback: an effective window of 256 callbacks
    static constexpr double kForget = 1.0 - 1.0 / 256.0;
    /// The device's sample period may differ from the nominal by at most this (crystals are ~100 ppm off; an
    /// estimate beyond that is callback noise early in the fit)
    static constexpr double kMaxRateError = 0.005;
    /// No callback for this long: the device stopped, the clock is not valid. A callback after such a gap (sleep and
    /// wake, an overloaded host) starts the fit over: the line through both sides of the gap fits neither
    static constexpr int64_t kStaleNs = 250'000'000;

    /// Device thread, at the start of every callback, before the ring is read.
    /// @param hostNs        steady-clock now
    /// @param streamFrame   the ring's read position (AudioRingBuffer::getReadStereoFrames) before this callback
    /// @param frameCount    frames the device asks for
    /// @param streamHasData the ring holds data for this callback (the stream's next frame is played now)
    /// @param sampleRate    the device rate
    /// @param latencyFrames device buffer + output latency: from a callback until its first frame is heard
    void OnDeviceCallback(int64_t hostNs, uint64_t streamFrame, uint32_t frameCount, bool streamHasData,
                          uint32_t sampleRate, uint32_t latencyFrames);

    /// Any thread: forget the fit (device stopped, re-initialized, rate changed)
    void Reset();

    /// Any thread: when stream frame `streamFrame` is heard (its start). False while the fit is not valid: too few
    /// callbacks since a reset, or none for kStaleNs before `nowNs`
    bool TimeOfStreamFrame(uint64_t streamFrame, int64_t nowNs, int64_t* hostNs) const;

private:
    struct Fit
    {
        uint64_t originDevice = 0;  // device position of the fit's origin
        int64_t originNs = 0;       // host time of the origin
        double meanX = 0.0;         // weighted mean device position after the origin
        double meanY = 0.0;         // weighted mean host time after the origin, ns
        double slope = 0.0;         // ns per device frame
        int64_t streamToDevice = 0; // device position - stream position
        int64_t latencyNs = 0;
        int64_t lastCallbackNs = 0;
        uint32_t generation = 0;
    };
    bool ReadFit(Fit* fit) const;

    // Writer state (device thread)
    uint32_t _generation = 0;
    uint64_t _device = 0;  // frames requested so far
    uint32_t _callbacks = 0;
    int64_t _lastHostNs = 0;  // the last callback's time
    bool _haveOffset = false;
    uint64_t _originDevice = 0;
    int64_t _originNs = 0;
    double _weight = 0.0;
    double _meanX = 0.0;
    double _meanY = 0.0;
    double _sxx = 0.0;
    double _sxy = 0.0;
    int64_t _streamToDevice = 0;

    std::atomic<uint32_t> _resetGeneration{0};

    // Published fit (sequence lock: odd while the writer updates it)
    std::atomic<uint32_t> _seq{0};
    std::atomic<uint64_t> _pubOriginDevice{0};
    std::atomic<int64_t> _pubOriginNs{0};
    std::atomic<double> _pubMeanX{0.0};
    std::atomic<double> _pubMeanY{0.0};
    std::atomic<double> _pubSlope{0.0};
    std::atomic<int64_t> _pubStreamToDevice{0};
    std::atomic<int64_t> _pubLatencyNs{0};
    std::atomic<int64_t> _pubLastCallbackNs{0};
    std::atomic<uint32_t> _pubGeneration{0};
    std::atomic<bool> _pubValid{false};
};
