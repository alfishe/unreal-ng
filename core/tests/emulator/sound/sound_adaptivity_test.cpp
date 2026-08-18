#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/beeper.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/soundmanager.h"

/// Sound generation adaptivity tests.
///
/// Every sound generator and DSP stage must derive its per-frame sample count
/// from the machine's actual frame length (config.frame t-states @ 3.5 MHz),
/// never from the 50 Hz SAMPLES_PER_FRAME constant (882). ZX and clone frame
/// lengths differ: ZX48/128/Scorpion 69888T (50.08 Hz, 881 samples),
/// Spectrum +2A/+3 70908T (49.36 Hz, 893 samples), Pentagon 71680T
/// (48.83 Hz, 903 samples) - and hypothetical clones vary further.
/// A hardcoded count leaves an unprocessed/stale tail every frame, audible as
/// frame-rate harmonics and hiss on pure tones.

namespace
{
// Machine frame lengths in t-states @ 3.5 MHz base clock
struct MachineTiming
{
    const char* name;
    uint32_t frameTStates;
};

const std::vector<MachineTiming> MACHINE_TIMINGS = {
    {"ZX48/ZX128/Scorpion", 69888},   // 50.08 Hz -> 881 samples
    {"Spectrum +2A/+3", 70908},       // 49.36 Hz -> 893 samples
    {"Pentagon", 71680},              // 48.83 Hz -> 903 samples
    {"Hypothetical 60Hz clone", 58333},  // ~60 Hz -> 735 samples
    {"Hypothetical slow clone", 80000},  // 43.75 Hz -> 1008 samples
};

// The authoritative per-frame sample count for a given frame length
size_t expectedSamples(uint32_t frameTStates)
{
    return static_cast<size_t>(
        std::lround(frameTStates * (double)AUDIO_SAMPLING_RATE / (double)CPU_CLOCK_RATE));
}

// Count leading stereo pairs that were written over a sentinel fill
size_t countWrittenPairs(const int16_t* buffer, size_t maxPairs, int16_t sentinel)
{
    size_t pairs = 0;
    while (pairs < maxPairs && !(buffer[pairs * 2] == sentinel && buffer[pairs * 2 + 1] == sentinel))
    {
        pairs++;
    }
    return pairs;
}

constexpr int16_t SENTINEL = 0x7C7C;
}  // namespace

class SoundAdaptivity_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();
        ASSERT_EQ(_context->emulatorState.current_z80_frequency_multiplier, 1);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

/// region <End-to-end: SoundManager output size follows the frame length>

namespace
{
struct CallbackCapture
{
    size_t lastNumSamples = 0;
    size_t callCount = 0;

    static void callback(void* obj, int16_t* samples, size_t numSamples)
    {
        (void)samples;
        auto* self = static_cast<CallbackCapture*>(obj);
        self->lastNumSamples = numSamples;
        self->callCount++;
    }
};
}  // namespace

TEST_F(SoundAdaptivity_Test, SoundManager_CallbackSizeFollowsFrameLength)
{
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);

    for (const auto& machine : MACHINE_TIMINGS)
    {
        _context->config.frame = machine.frameTStates;

        size_t before = capture.callCount;
        sound->handleFrameStart();
        sound->handleFrameEnd();

        ASSERT_GT(capture.callCount, before) << machine.name << ": audio callback was not invoked";
        // Per-frame counts alternate by +-1 around the exact rational value
        // (integer accumulator carries the fraction); cumulative exactness is
        // verified by SoundManager_ExactSampleCountOverAccumulatorPeriod
        EXPECT_NEAR(static_cast<double>(capture.lastNumSamples),
                    static_cast<double>(expectedSamples(machine.frameTStates) * AUDIO_CHANNELS),
                    static_cast<double>(AUDIO_CHANNELS))
            << machine.name << " (" << machine.frameTStates
            << "T): mixed output size must derive from the frame length";
    }
}

/// endregion </End-to-end>

/// region <Beeper (blip_buf) adaptivity>

TEST_F(SoundAdaptivity_Test, Beeper_SampleCountFollowsFrameLength)
{
    constexpr size_t bufferPairs = MAX_SAMPLES_PER_FRAME + 64;
    std::vector<int16_t> buffer(bufferPairs * AUDIO_CHANNELS);

    Beeper beeper(_context, CPU_CLOCK_RATE, AUDIO_SAMPLING_RATE, buffer.data());

    for (const auto& machine : MACHINE_TIMINGS)
    {
        std::fill(buffer.begin(), buffer.end(), SENTINEL);

        beeper.handleFrameStart();
        beeper.handleFrameEnd(machine.frameTStates);

        size_t written = countWrittenPairs(buffer.data(), bufferPairs, SENTINEL);
        size_t expected = expectedSamples(machine.frameTStates);

        // blip_buf's internal resampling may land one sample either side of
        // the ideal count per frame; anything larger is a hardcoded-rate bug
        EXPECT_NEAR(static_cast<double>(written), static_cast<double>(expected), 1.0)
            << machine.name << " (" << machine.frameTStates << "T)";
    }
}

/// endregion </Beeper>

/// region <Covox (blip_buf) adaptivity>

TEST_F(SoundAdaptivity_Test, Covox_SampleCountFollowsFrameLength)
{
    Covox covox(_context);
    covox.reset();

    for (const auto& machine : MACHINE_TIMINGS)
    {
        _context->config.frame = machine.frameTStates;

        covox.handleFrameStart();

        // Sentinel-fill AFTER frame start (which may clear the buffer)
        int16_t* buffer = covox.getBuffer();
        constexpr size_t bufferPairs = MAX_SAMPLES_PER_FRAME;
        std::fill(buffer, buffer + bufferPairs * AUDIO_CHANNELS, SENTINEL);

        covox.handleFrameEnd();

        size_t written = countWrittenPairs(buffer, bufferPairs, SENTINEL);
        size_t expected = expectedSamples(machine.frameTStates);

        EXPECT_NEAR(static_cast<double>(written), static_cast<double>(expected), 1.0)
            << machine.name << " (" << machine.frameTStates << "T)";
    }
}

/// endregion </Covox>

/// region <TurboSound / AY adaptivity>

TEST_F(SoundAdaptivity_Test, TurboSound_SampleCountFollowsFrameLength)
{
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    SoundChip_TurboSound* turboSound = sound->getTurboSound();
    if (!turboSound)
    {
        GTEST_SKIP() << "TurboSound not available on this model";
    }

    Z80* z80 = _context->pCore->GetZ80();

    for (const auto& machine : MACHINE_TIMINGS)
    {
        _context->config.frame = machine.frameTStates;

        turboSound->handleFrameStart();
        z80->t = machine.frameTStates;  // Simulate reaching frame end
        turboSound->handleStep();

        size_t rendered = turboSound->getRenderedSamplesThisFrame();
        size_t expected = expectedSamples(machine.frameTStates);

        // PLL accumulation may land one sample short of the rounded ideal
        EXPECT_NEAR(static_cast<double>(rendered), static_cast<double>(expected), 1.0)
            << machine.name << " (" << machine.frameTStates << "T)";
    }

    z80->t = 0;
}

/// endregion </TurboSound>

/// region <Exact sample accumulator (audio-sync Fix 1)>

TEST_F(SoundAdaptivity_Test, SoundManager_ExactSampleCountOverAccumulatorPeriod)
{
    // The integer accumulator makes total samples over N frames EXACTLY
    // floor(N * frame * SR / CPU_CLOCK) - drift-free by construction.
    // The old round() emitted a systematic bias (-0.019% Pentagon,
    // +0.047% ZX48) responsible for realtime ring drift and
    // audio-behind-video drift in recordings.
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);

    struct Case
    {
        const char* name;
        uint32_t frame;
        uint32_t frames;  // Full accumulator period
    };
    const Case cases[] = {
        {"Pentagon", 71680, 125},   // 125 * 903.168 = 112896 exactly
        {"ZX48/128", 69888, 625},   // 625 * 880.5888 = 550368 exactly
    };

    for (const auto& c : cases)
    {
        _context->config.frame = c.frame;
        sound->reset();  // Restart the accumulator for a clean period

        uint64_t totalStereoSamples = 0;
        for (uint32_t f = 0; f < c.frames; f++)
        {
            sound->handleFrameStart();
            sound->handleFrameEnd();
            totalStereoSamples += capture.lastNumSamples / AUDIO_CHANNELS;
        }

        uint64_t expected =
            (static_cast<uint64_t>(c.frames) * c.frame * AUDIO_SAMPLING_RATE) / CPU_CLOCK_RATE;
        EXPECT_EQ(totalStereoSamples, expected)
            << c.name << ": " << c.frames << " frames must deliver exactly " << expected
            << " samples (zero drift by construction)";
    }
}

TEST_F(SoundAdaptivity_Test, TurboSound_PLLContinuityAcrossFrames)
{
    // The AY phase accumulator must be free-running across frames: zeroing it
    // per frame locked the AY at 903 samples/frame (never 904), a systematic
    // -0.019% rate bias vs the exact accumulator.
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);
    SoundChip_TurboSound* turboSound = sound->getTurboSound();
    if (!turboSound)
        GTEST_SKIP() << "TurboSound not available";

    Z80* z80 = _context->pCore->GetZ80();
    _context->config.frame = 71680;
    turboSound->reset();

    uint64_t total = 0;
    for (int f = 0; f < 125; f++)
    {
        turboSound->handleFrameStart();
        z80->t = 71680;
        turboSound->handleStep();
        total += turboSound->getRenderedSamplesThisFrame();
    }
    z80->t = 0;

    // 125 * 903.168 = 112896; allow FP phase accumulation slack of 2
    EXPECT_NEAR(static_cast<double>(total), 112896.0, 2.0)
        << "AY PLL must carry fractional phase across frames (903/904 pattern)";
}

/// endregion </Exact sample accumulator>

/// region <DRC rate controller (audio-sync Fix 2)>

TEST_F(SoundAdaptivity_Test, DRC_ConvergesToTargetOccupancy)
{
    // Closed loop: a simulated DAC consumes exactly real-time while the
    // emulator produces through the full pipeline (mix -> DRC resampler ->
    // callback). The PI controller must drive ring occupancy to the 70 ms
    // setpoint from both directions and hold it with a small trim.
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);

    std::atomic<uint32_t> occCell{0};
    _context->pAudioRingOccupancy.store(&occCell, std::memory_order_release);

    _context->config.frame = 71680;
    const double consumePerFrame = 71680.0 * 44100.0 / 3500000.0;  // Real-time DAC

    auto runLoop = [&](double startFrames, int frames) -> double {
        sound->reset();
        double ring = startFrames;
        occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);
        for (int f = 0; f < frames; f++)
        {
            sound->handleFrameStart();
            sound->handleFrameEnd();
            ring += static_cast<double>(capture.lastNumSamples) / AUDIO_CHANNELS;
            ring = std::max(0.0, ring - consumePerFrame);
            occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);
        }
        return ring;
    };

    // From near-empty (emergency refill seeds ~46ms in the real mainloop;
    // start there) and from badly overfull
    for (double startMs : {46.0, 300.0})
    {
        double finalFrames = runLoop(startMs * 44100.0 / 1000.0, 6000);
        double finalMs = finalFrames * 1000.0 / 44100.0;

        EXPECT_NEAR(finalMs, 70.0, 8.0)
            << "DRC must converge ring occupancy to the 70ms setpoint (start " << startMs << "ms)";
        EXPECT_LT(std::abs(sound->getDrcRatio() - 1.0), 0.001)
            << "Converged trim must be small (start " << startMs << "ms)";
    }

    _context->pAudioRingOccupancy.store(nullptr, std::memory_order_release);
}

TEST_F(SoundAdaptivity_Test, DRC_DisengagedWithoutOccupancyCell)
{
    // No audio device attached (headless/tests): DRC must stay at exact
    // unity bypass so output remains bit-identical and sample-exact
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);
    ASSERT_EQ(_context->pAudioRingOccupancy.load(), nullptr);

    sound->handleFrameStart();
    sound->handleFrameEnd();
    EXPECT_EQ(sound->getDrcRatio(), 1.0);
}

/// endregion </DRC rate controller>

TEST_F(SoundAdaptivity_Test, DRC_NativeDeviceRate48k_ConvergesAndConverts)
{
    // Device at native 48000 Hz (audio-sync Fix 3): the DRC resampler's base
    // ratio becomes 48000/44100 and the controller still converges occupancy
    // (measured in DEVICE frames) to the 70 ms setpoint.
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);

    std::atomic<uint32_t> occCell{0};
    _context->pAudioRingOccupancy.store(&occCell, std::memory_order_release);
    _context->pAudioDeviceSampleRate.store(48000, std::memory_order_release);

    _context->config.frame = 71680;
    const double consumePerFrame = 71680.0 * 48000.0 / 3500000.0;  // Real-time DAC @48k

    sound->reset();
    double ring = 70.0 * 48.0;  // Start at setpoint (in device frames): verify HOLD
    occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);

    uint64_t totalDeviceSamples = 0;
    for (int f = 0; f < 6000; f++)
    {
        sound->handleFrameStart();
        sound->handleFrameEnd();
        totalDeviceSamples += capture.lastNumSamples / AUDIO_CHANNELS;
        ring += static_cast<double>(capture.lastNumSamples) / AUDIO_CHANNELS;
        ring = std::max(0.0, ring - consumePerFrame);
        occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);
    }

    double finalMs = ring * 1000.0 / 48000.0;
    EXPECT_NEAR(finalMs, 70.0, 8.0) << "Occupancy must hold at setpoint with 48k device";

    // Output volume converted at ~48/44.1: 6000 frames x 903.168 core samples
    double expectedDevice = 6000.0 * 903.168 * 48000.0 / 44100.0;
    EXPECT_NEAR(static_cast<double>(totalDeviceSamples), expectedDevice, expectedDevice * 0.002)
        << "Device stream volume must reflect the 48000/44100 base ratio";

    _context->pAudioRingOccupancy.store(nullptr, std::memory_order_release);
    _context->pAudioDeviceSampleRate.store(0, std::memory_order_release);
}
