#include "stdafx.h"
#include "pch.h"

#include <cmath>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/sound/audio.h"

/// Tests for frame pacing / A/V synchronization timing.
///
/// MainLoop paces free-running emulation (no audio callback requests) by waiting
/// config.frame_duration_us per frame. That value must match the real duration
/// of the audio generated per frame, otherwise the audio playback ring buffer
/// either fills up (video ahead, audio delayed by up to the ring capacity of
/// ~370 ms) or drains (audio underruns). Historically a hardcoded 20 ms timeout
/// made Pentagon (71680 t-states = 20.48 ms/frame) overproduce audio by ~2.4%.
class FramePacing_Test : public ::testing::Test
{
};

/// region <CalculateFrameDurationUs unit tests>

TEST_F(FramePacing_Test, FrameDuration_Pentagon)
{
    // Pentagon: 71680 t-states @ 3.5 MHz = 20480 us exactly
    EXPECT_EQ(CalculateFrameDurationUs(71680), 20480u);

    // Regression guard for the original bug: the timeout must be strictly
    // longer than the old hardcoded 20 ms, or Pentagon overproduces audio
    EXPECT_GT(CalculateFrameDurationUs(71680), 20000u);
}

TEST_F(FramePacing_Test, FrameDuration_ZX48_128_Scorpion)
{
    // ZX48 / ZX128 / ScorpionZS256: 69888 t-states @ 3.5 MHz = 19968 us exactly
    EXPECT_EQ(CalculateFrameDurationUs(69888), 19968u);
}

TEST_F(FramePacing_Test, FrameDuration_ZeroFallsBackTo50Hz)
{
    // Unconfigured frame length must not produce a zero timeout (busy loop)
    EXPECT_EQ(CalculateFrameDurationUs(0), 20000u);
}

TEST_F(FramePacing_Test, FrameDuration_NeverShorterThanAudioProduced)
{
    // For any frame length, the pacing timeout must be >= the exact real-time
    // frame duration (ceil rounding), but overshoot by less than 1 us.
    // A timeout even 1 us short of the frame duration means systematic audio
    // overproduction and unbounded ring buffer growth.
    for (uint32_t frame = 60000; frame <= 80000; frame += 7)
    {
        uint64_t durationUs = CalculateFrameDurationUs(frame);

        // duration_us >= frame / 3.5MHz  <=>  duration_us * 3.5e6 >= frame * 1e6
        EXPECT_GE(durationUs * CPU_CLOCK_RATE, static_cast<uint64_t>(frame) * 1'000'000ULL)
            << "Timeout shorter than frame duration for frame=" << frame;

        // Ceil tightness: (duration_us - 1) must be below the exact duration
        EXPECT_LT((durationUs - 1) * CPU_CLOCK_RATE, static_cast<uint64_t>(frame) * 1'000'000ULL)
            << "Timeout overshoots by a full microsecond or more for frame=" << frame;
    }
}

/// endregion </CalculateFrameDurationUs unit tests>

/// region <Pentagon A/V synchronization precision>

TEST_F(FramePacing_Test, Pentagon_VideoRefreshRate_Is48_83FPS)
{
    // Pentagon's real refresh rate is 3.5 MHz / 71680 = 48.828125 Hz, not 50 Hz
    constexpr double expectedFps = 3'500'000.0 / 71680.0;
    EXPECT_NEAR(expectedFps, 48.828125, 1e-9);

    double pacedFps = 1'000'000.0 / CalculateFrameDurationUs(71680);
    EXPECT_NEAR(pacedFps, expectedFps, 0.01);
}

TEST_F(FramePacing_Test, Pentagon_AudioAndVideoFramesStayInSync)
{
    // Per emulated frame, SoundManager produces
    //   round(frame * AUDIO_SAMPLING_RATE / CPU_CLOCK_RATE) samples
    // (soundmanager.cpp, handleFrameEnd) while MainLoop paces video at
    // frame_duration_us. The two must agree to within one audio sample period,
    // and pacing must never be FASTER than the audio produced (that direction
    // accumulates into the ring buffer and becomes audible A/V desync).
    constexpr uint32_t pentagonFrame = 71680;

    auto samplesPerFrame = static_cast<uint64_t>(
        std::round(pentagonFrame * (double)AUDIO_SAMPLING_RATE / (double)CPU_CLOCK_RATE));
    EXPECT_EQ(samplesPerFrame, 903u);  // 20.48 ms @ 44100 Hz

    double audioDurationUs = samplesPerFrame * 1'000'000.0 / AUDIO_SAMPLING_RATE;  // 20476.19 us
    double videoDurationUs = CalculateFrameDurationUs(pentagonFrame);              // 20480 us
    constexpr double audioSamplePeriodUs = 1'000'000.0 / AUDIO_SAMPLING_RATE;      // 22.68 us

    // Video pacing must not outrun audio production...
    EXPECT_GE(videoDurationUs, audioDurationUs)
        << "Video paced faster than audio is produced: ring buffer will fill up";

    // ...and must not lag it by a full sample either (sub-sample precision).
    // The residual (~3.8 us/frame) drains the ring slightly; the audio
    // callback's low-watermark requests compensate by waking MainLoop early.
    EXPECT_LT(videoDurationUs - audioDurationUs, audioSamplePeriodUs)
        << "Audio/video frame durations diverge by more than one sample period";
}

/// endregion </Pentagon A/V synchronization precision>

/// region <Config load integration>

TEST_F(FramePacing_Test, ConfigLoad_ComputesFrameDuration_Pentagon)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr) << "Failed to create emulator";

    const CONFIG& config = emulator->GetContext()->config;
    EXPECT_EQ(config.frame, 71680u);
    EXPECT_EQ(config.frame_duration_us, 20480u);
    EXPECT_EQ(config.frame_duration_us, CalculateFrameDurationUs(config.frame));

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// endregion </Config load integration>
