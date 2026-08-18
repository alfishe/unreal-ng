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
        EXPECT_EQ(capture.lastNumSamples, expectedSamples(machine.frameTStates) * AUDIO_CHANNELS)
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
