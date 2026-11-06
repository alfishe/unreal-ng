#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <cstdint>

#include "_helpers/testtier.h"
#include "common/sound/audioclock.h"

/// AudioClock: the audio device as the master clock of the frame presenter (docs/inprogress/2026-10-26-frame-presenter,
/// section 3). A synthetic device: callbacks of `period` frames at a crystal's true rate, each called late by a
/// pseudo-random 0..jitter, and a stream that the device plays from the ring

namespace
{
constexpr uint32_t kRate = 44100;
constexpr uint32_t kPeriod = 256;
constexpr uint32_t kLatency = 512;

struct SyntheticDevice
{
    AudioClock clock;
    double trueRate = kRate;      // the crystal: frames per second as the host clock counts them
    int64_t jitterNs = 3'000'000;
    int64_t startNs = 1'000'000'000'000;
    uint64_t device = 0;         // frames the device asked for so far
    uint64_t stream = 0;         // the ring's read position
    uint64_t streamToDevice = 0; // silence played so far (underruns): device - stream
    uint32_t lcg = 12345;

    /// The ideal host time device frame `d` is asked for (no jitter)
    int64_t IdealNs(uint64_t d) const { return startNs + static_cast<int64_t>(d * 1e9 / trueRate); }
    /// When stream frame `s` is truly heard
    int64_t HeardNs(uint64_t s) const
    {
        return IdealNs(s + streamToDevice) + static_cast<int64_t>(kLatency * 1e9 / kRate);
    }
    int64_t NowNs() const { return IdealNs(device); }

    /// One callback; `hasData` false = an underrun (the device plays silence, the stream stands)
    void Callback(bool hasData = true)
    {
        lcg = lcg * 1103515245u + 12345u;
        const int64_t late = jitterNs ? static_cast<int64_t>((lcg >> 8) % static_cast<uint32_t>(jitterNs)) : 0;
        clock.OnDeviceCallback(IdealNs(device) + late, stream, kPeriod, hasData, kRate, kLatency);
        device += kPeriod;
        if (hasData)
            stream += kPeriod;
        else
            streamToDevice += kPeriod;
    }
    void Run(int callbacks)
    {
        for (int i = 0; i < callbacks; i++)
            Callback();
    }

    /// The rate the clock's line gives: stream frames per second of host time over the next 10 s of the stream
    double FittedRate() const
    {
        const uint64_t span = static_cast<uint64_t>(kRate) * 10;
        int64_t from = 0;
        int64_t to = 0;
        if (!clock.TimeOfStreamFrame(stream, NowNs(), &from) || !clock.TimeOfStreamFrame(stream + span, NowNs(), &to) ||
            to <= from)
            return 0.0;
        return static_cast<double>(span) * 1e9 / static_cast<double>(to - from);
    }
};
}  // namespace

TEST(AudioClock_Test, NotValidUntilEnoughCallbacks)
{
    SyntheticDevice dev;
    int64_t t = 0;
    EXPECT_FALSE(dev.clock.TimeOfStreamFrame(0, dev.NowNs(), &t)) << "no callback yet";
    dev.Run(AudioClock::kMinCallbacks - 1);
    EXPECT_FALSE(dev.clock.TimeOfStreamFrame(dev.stream, dev.NowNs(), &t));
    dev.Run(1);
    EXPECT_TRUE(dev.clock.TimeOfStreamFrame(dev.stream, dev.NowNs(), &t));
}

/// The point of the fit: the callbacks jitter by up to 3 ms, the times of consecutive video frames' sound do not
TEST(AudioClock_Test, SmoothDespiteCallbackJitter)
{
    SyntheticDevice dev;
    dev.trueRate = kRate * (1.0 + 100e-6);  // a crystal 100 ppm fast
    dev.Run(1000);  // ~5.8 s

    // Frame ends 882 stream frames apart (50 Hz), all ahead of the stream as the core writes them
    const uint64_t frame = 882;
    const uint64_t firstEnd = dev.stream + 2000 + frame * 3 + kPeriod * 50;
    int64_t previous = 0;
    double worstStepError = 0.0;
    double worstAbsError = 0.0;
    for (int k = 0; k < 50; k++)
    {
        dev.Callback();
        const uint64_t end = firstEnd + k * frame;
        int64_t heard = 0;
        ASSERT_TRUE(dev.clock.TimeOfStreamFrame(end, dev.NowNs(), &heard));
        const int64_t truth = dev.HeardNs(end);
        worstAbsError = std::max(worstAbsError, std::abs(static_cast<double>(heard - truth)));
        if (k > 0)
        {
            const double step = static_cast<double>(heard - previous);
            // Each frame one frame of the true rate later; between the reads, one callback moved the fit
            const double trueStep = frame * 1e9 / dev.trueRate;
            worstStepError = std::max(worstStepError, std::abs(step - trueStep));
        }
        previous = heard;
    }
    // The fit moves by a fraction of the jitter per callback; consecutive frames' times stay within 0.1 ms of the
    // true period (the requirement of design section 9)
    EXPECT_LT(worstStepError, 100'000.0) << "ns";
    // A constant bias of about the mean lateness (1.5 ms) is expected: the callbacks are late, never early
    EXPECT_LT(worstAbsError, 2'500'000.0) << "ns";
    // The rate's noise with 3 ms of jitter over the window is ~17 ppm (0.75 Hz); 2 Hz is ~3 sigma
    EXPECT_NEAR(dev.FittedRate(), dev.trueRate, 2.0);
}

/// An underrun: the device played silence, so every later sample of the stream is heard that much later
TEST(AudioClock_Test, UnderrunMovesTheStream)
{
    SyntheticDevice dev;
    dev.jitterNs = 0;
    dev.Run(100);
    const uint64_t probe = dev.stream + 5000;
    int64_t before = 0;
    ASSERT_TRUE(dev.clock.TimeOfStreamFrame(probe, dev.NowNs(), &before));

    dev.Callback(false);
    dev.Callback(false);
    dev.Run(1);
    int64_t after = 0;
    ASSERT_TRUE(dev.clock.TimeOfStreamFrame(probe, dev.NowNs(), &after));
    EXPECT_NEAR(static_cast<double>(after - before), 2.0 * kPeriod * 1e9 / kRate, 10'000.0);
    EXPECT_NEAR(static_cast<double>(after), static_cast<double>(dev.HeardNs(probe)), 10'000.0);
}

/// A discard (hard resync) or a clear: stream the device never plays; later samples come sooner
TEST(AudioClock_Test, DiscardMovesTheStream)
{
    SyntheticDevice dev;
    dev.jitterNs = 0;
    dev.Run(100);
    const uint64_t probe = dev.stream + 20000;
    int64_t before = 0;
    ASSERT_TRUE(dev.clock.TimeOfStreamFrame(probe, dev.NowNs(), &before));

    dev.stream += 4410;  // 100 ms dropped
    dev.streamToDevice -= 4410;
    dev.Run(1);
    int64_t after = 0;
    ASSERT_TRUE(dev.clock.TimeOfStreamFrame(probe, dev.NowNs(), &after));
    EXPECT_NEAR(static_cast<double>(before - after), 100e6, 10'000.0);
}

TEST(AudioClock_Test, StaleAndReset)
{
    SyntheticDevice dev;
    dev.Run(100);
    int64_t t = 0;
    EXPECT_TRUE(dev.clock.TimeOfStreamFrame(dev.stream, dev.NowNs(), &t));
    EXPECT_FALSE(dev.clock.TimeOfStreamFrame(dev.stream, dev.NowNs() + AudioClock::kStaleNs + 10'000'000, &t))
        << "a stopped device is no clock";

    dev.clock.Reset();
    EXPECT_FALSE(dev.clock.TimeOfStreamFrame(dev.stream, dev.NowNs(), &t)) << "invalid at once";
    dev.Run(AudioClock::kMinCallbacks - 1);
    EXPECT_FALSE(dev.clock.TimeOfStreamFrame(dev.stream, dev.NowNs(), &t)) << "the fit starts over";
    dev.Run(1);
    EXPECT_TRUE(dev.clock.TimeOfStreamFrame(dev.stream, dev.NowNs(), &t));
}

/// A gap in the callbacks (sleep and wake, an overloaded host): the fit starts over, it never draws its line through
/// both sides of the gap
TEST(AudioClock_Test, GapStartsTheFitOver)
{
    SyntheticDevice dev;
    dev.jitterNs = 0;
    dev.Run(300);
    dev.startNs += 2'000'000'000;  // the host slept 2 s; the device goes on from where it stopped
    dev.Run(1);
    int64_t heard = 0;
    EXPECT_FALSE(dev.clock.TimeOfStreamFrame(dev.stream, dev.NowNs(), &heard)) << "one callback after the gap";
    dev.Run(AudioClock::kMinCallbacks - 1);
    const uint64_t probe = dev.stream + 3000;
    ASSERT_TRUE(dev.clock.TimeOfStreamFrame(probe, dev.NowNs(), &heard));
    EXPECT_NEAR(static_cast<double>(heard), static_cast<double>(dev.HeardNs(probe)), 100'000.0)
        << "the times after the gap only";
}

/// Hours of audio: the origin moves along, the numbers stay small, the error does not grow
TEST(AudioClock_Test, LongRunKeepsPrecision)
{
    SyntheticDevice dev;
    dev.trueRate = kRate * (1.0 - 50e-6);
    dev.jitterNs = 300'000;  // the subject is the numbers after hours, not the jitter
    const int callbacks = TestTier::Long() ? 172 * 3600 * 4 : 172 * 1800;  // 4 h / 30 min
    dev.Run(callbacks);
    int64_t heard = 0;
    const uint64_t probe = dev.stream + 3000;
    ASSERT_TRUE(dev.clock.TimeOfStreamFrame(probe, dev.NowNs(), &heard));
    EXPECT_LT(std::abs(static_cast<double>(heard - dev.HeardNs(probe))), 2'500'000.0);
    EXPECT_NEAR(dev.FittedRate(), dev.trueRate, 0.5);
}

/// Early in the fit, three callbacks over a few ms cannot tell the rate: the slope stays near the nominal one
TEST(AudioClock_Test, SlopeClampedToPlausibleRates)
{
    SyntheticDevice dev;
    dev.jitterNs = 5'000'000;
    dev.Run(AudioClock::kMinCallbacks);
    const double rate = dev.FittedRate();
    // The clamp is on the sample period: the rate's bounds are its reciprocals
    EXPECT_GE(rate, kRate / (1.0 + AudioClock::kMaxRateError) - 0.01);
    EXPECT_LE(rate, kRate / (1.0 - AudioClock::kMaxRateError) + 0.01);
}
