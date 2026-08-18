#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/beeper.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/chips/soundchip_turbosound.h"

/// Multi-rate core tests (multirate plan phase 7).
///
/// The core audio rate is a construction parameter ([SOUND] CoreRate); every
/// chip and filter designs itself for it. These tests verify the rate matrix
/// end-to-end: resolution rules, per-frame sample counts, exactness of the
/// integer accumulator at every rate, and pitch invariance (the same emulated
/// tone must measure the same Hz at every core rate - the acceptance
/// criterion from the evaluation doc).

namespace
{
const std::vector<size_t> CORE_RATES = {44100, 48000, 88200, 96000, 176400, 192000};

constexpr uint32_t PENTAGON_FRAME = 71680;

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

class Multirate_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            _context->pAudioDeviceSampleRate.store(0, std::memory_order_release);
            _context->config.sound.coreRate = 0;
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

/// region <Core rate resolution rules>

TEST_F(Multirate_Test, CoreRateResolution)
{
    // Explicit config value wins
    _context->config.sound.coreRate = 96000;
    {
        SoundManager sound(_context);
        EXPECT_EQ(sound.getCoreRate(), 96000u);
    }

    // auto + supported device rate published -> match the device
    _context->config.sound.coreRate = 0;
    _context->pAudioDeviceSampleRate.store(48000, std::memory_order_release);
    {
        SoundManager sound(_context);
        EXPECT_EQ(sound.getCoreRate(), 48000u);
    }

    // auto + unsupported device rate -> conservative 44100
    _context->pAudioDeviceSampleRate.store(22050, std::memory_order_release);
    {
        SoundManager sound(_context);
        EXPECT_EQ(sound.getCoreRate(), 44100u);
    }

    // auto + no device rate known -> 44100
    _context->pAudioDeviceSampleRate.store(0, std::memory_order_release);
    {
        SoundManager sound(_context);
        EXPECT_EQ(sound.getCoreRate(), 44100u);
    }
}

/// endregion </Core rate resolution rules>

/// region <Exact sample accumulator at every rate>

TEST_F(Multirate_Test, ExactSampleCountOverPeriodAtEveryRate)
{
    // The integer accumulator must deliver EXACTLY N*frame*rate/CPU_CLOCK
    // samples over N frames at every core rate - drift-free by construction.
    // (At 48k-family rates Pentagon's accumulator period is 25 frames vs 125.)
    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);
    _context->config.frame = PENTAGON_FRAME;

    for (size_t rate : CORE_RATES)
    {
        _context->config.sound.coreRate = static_cast<unsigned>(rate);
        SoundManager sound(_context);
        ASSERT_EQ(sound.getCoreRate(), rate);

        constexpr uint32_t FRAMES = 125;
        uint64_t total = 0;
        for (uint32_t f = 0; f < FRAMES; f++)
        {
            sound.handleFrameStart();
            sound.handleFrameEnd();
            total += capture.lastNumSamples / AUDIO_CHANNELS;
        }

        const uint64_t expected =
            (static_cast<uint64_t>(FRAMES) * PENTAGON_FRAME * rate) / CPU_CLOCK_RATE;
        EXPECT_EQ(total, expected)
            << rate << " Hz: " << FRAMES << " Pentagon frames must deliver exactly "
            << expected << " samples";
    }
}

/// endregion </Exact sample accumulator>

/// region <AY renders the correct sample count at every rate>

TEST_F(Multirate_Test, TurboSound_SampleCountFollowsCoreRate)
{
    Z80* z80 = _context->pCore->GetZ80();
    _context->config.frame = PENTAGON_FRAME;

    for (size_t rate : CORE_RATES)
    {
        _context->config.sound.coreRate = static_cast<unsigned>(rate);
        SoundManager sound(_context);
        SoundChip_TurboSound* turboSound = sound.getTurboSound();
        ASSERT_NE(turboSound, nullptr);
        ASSERT_EQ(turboSound->getCoreRate(), rate);

        uint64_t total = 0;
        constexpr int FRAMES = 25;
        for (int f = 0; f < FRAMES; f++)
        {
            turboSound->handleFrameStart();
            z80->t = PENTAGON_FRAME;
            turboSound->handleStep();
            total += turboSound->getRenderedSamplesThisFrame();
        }
        z80->t = 0;

        const double expected = FRAMES * PENTAGON_FRAME * static_cast<double>(rate) / CPU_CLOCK_RATE;
        EXPECT_NEAR(static_cast<double>(total), expected, 2.0)
            << rate << " Hz: AY PLL must render the frame's worth of samples";
    }
}

/// endregion </AY sample count>

/// region <Pitch invariance (acceptance criterion)>

TEST_F(Multirate_Test, Beeper_PitchInvariantAcrossRates)
{
    // A square wave toggled every 512 T-states (~3418 Hz) must measure the
    // same frequency at every core rate. Zero-crossing count over a known
    // duration is the estimator: crossings / (2 * seconds) = Hz.
    // 512 divides the 71680T frame exactly, so the per-frame T-state restart
    // does not perturb the tone phase.
    constexpr uint32_t TOGGLE_PERIOD_T = 512;   // Half-period: 3417.97 Hz tone
    constexpr int FRAMES = 50;                  // ~1.024 s of Pentagon time
    const double emulatedSeconds =
        static_cast<double>(FRAMES) * PENTAGON_FRAME / static_cast<double>(CPU_CLOCK_RATE);

    _context->config.frame = PENTAGON_FRAME;

    for (size_t rate : CORE_RATES)
    {
        const size_t bufferPairs = MAX_SAMPLES_PER_FRAME + 64;
        std::vector<int16_t> buffer(bufferPairs * AUDIO_CHANNELS);
        Beeper beeper(_context, CPU_CLOCK_RATE, rate, buffer.data());

        uint64_t crossings = 0;
        int16_t prev = 0;
        int32_t level = 8000;

        for (int f = 0; f < FRAMES; f++)
        {
            beeper.handleFrameStart();
            for (uint32_t t = 0; t < PENTAGON_FRAME; t += TOGGLE_PERIOD_T)
            {
                beeper.handleTapeAudio(level, t);
                level = -level;
            }
            beeper.handleFrameEnd(PENTAGON_FRAME);

            const int samples = beeper.getLastSamplesRead();
            for (int i = 0; i < samples; i++)
            {
                const int16_t s = buffer[i * 2];  // Left channel
                if ((prev < 0 && s >= 0) || (prev >= 0 && s < 0))
                    crossings++;
                prev = s;
            }
        }

        const double measuredHz = static_cast<double>(crossings) / (2.0 * emulatedSeconds);
        const double expectedHz = static_cast<double>(CPU_CLOCK_RATE) / (2.0 * TOGGLE_PERIOD_T);

        EXPECT_NEAR(measuredHz, expectedHz, expectedHz * 0.005)
            << rate << " Hz core: beeper tone pitch must be invariant to the core rate";
    }
}

TEST_F(Multirate_Test, AY_PitchInvariantAcrossRates)
{
    // Program AY channel A to a 1 kHz-ish tone and verify the rendered pitch
    // is identical at every core rate. AY tone period N -> f = 1.75MHz/(16*N);
    // N=109 -> 1003.44 Hz.
    Z80* z80 = _context->pCore->GetZ80();
    _context->config.frame = PENTAGON_FRAME;

    constexpr uint8_t TONE_PERIOD = 109;
    const double expectedHz = static_cast<double>(PSG_CLOCK_RATE) / (16.0 * TONE_PERIOD);

    for (size_t rate : CORE_RATES)
    {
        _context->config.sound.coreRate = static_cast<unsigned>(rate);
        SoundManager sound(_context);
        SoundChip_TurboSound* turboSound = sound.getTurboSound();
        ASSERT_NE(turboSound, nullptr);

        // Program chip 0: channel A tone, full fixed volume
        auto poke = [&](uint8_t reg, uint8_t value) {
            turboSound->portDeviceOutMethod(0xFFFD, reg);
            turboSound->portDeviceOutMethod(0xBFFD, value);
        };
        poke(0, TONE_PERIOD);  // Tone A fine
        poke(1, 0);            // Tone A coarse
        poke(7, 0b00111110);   // Mixer: tone A on, noise off
        poke(8, 15);           // Volume A: max, no envelope

        // Capture the rendered stream, then measure around its actual midline
        // with hysteresis (robust against DC filtering and volume scaling)
        std::vector<int16_t> stream;
        constexpr int FRAMES = 50;
        stream.reserve(FRAMES * (PENTAGON_FRAME / 16));

        for (int f = 0; f < FRAMES; f++)
        {
            turboSound->handleFrameStart();
            z80->t = PENTAGON_FRAME;
            turboSound->handleStep();

            const size_t samples = turboSound->getRenderedSamplesThisFrame();
            const int16_t* buf = turboSound->getChipBuffer(0);
            for (size_t i = 0; i < samples; i++)
                stream.push_back(buf[i * 2]);  // Left channel
        }
        z80->t = 0;

        const auto [minIt, maxIt] = std::minmax_element(stream.begin(), stream.end());
        const int32_t lo = *minIt, hi = *maxIt;
        ASSERT_GT(hi - lo, 1000) << rate << " Hz core: AY output is flat - tone not rendered";

        const int32_t mid = (lo + hi) / 2;
        const int32_t hyst = (hi - lo) / 8;
        uint64_t crossings = 0;
        int state = 0;  // -1 below, +1 above, 0 unknown
        for (int16_t s : stream)
        {
            if (s > mid + hyst && state <= 0)
            {
                if (state < 0)
                    crossings++;  // One midline crossing per polarity flip
                state = 1;
            }
            else if (s < mid - hyst && state >= 0)
            {
                if (state > 0)
                    crossings++;
                state = -1;
            }
        }

        const double seconds = static_cast<double>(stream.size()) / static_cast<double>(rate);
        const double measuredHz = static_cast<double>(crossings) / (2.0 * seconds);

        EXPECT_NEAR(measuredHz, expectedHz, expectedHz * 0.01)
            << rate << " Hz core: AY tone pitch must be invariant to the core rate";
    }
}

/// endregion </Pitch invariance>
