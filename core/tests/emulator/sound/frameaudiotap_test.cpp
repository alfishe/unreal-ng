#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/beeper.h"
#include "emulator/sound/soundmanager.h"

/// The frame audio tap (the GUI window recording's sound track): every frame's mixed samples with the frame's number,
/// taken before mute like the recording, synthesised in turbo while a tap is present, nothing after removal

namespace
{
constexpr uint32_t PENTAGON_FRAME = 71680;

struct HostOutput
{
    static void Callback(void*, int16_t*, size_t) {}
};

struct Tapped
{
    uint64_t frame = 0;
    std::vector<int16_t> samples;
};
}  // namespace

class FrameAudioTap_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    std::unique_ptr<SoundManager> _sound;
    int _host = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _context->config.frame = PENTAGON_FRAME;
        _sound = std::make_unique<SoundManager>(_context);
        _sound->reset();
        // A host output: a muted manager then sends it zeros, the tap must still get the sound
        _context->pAudioManagerObj.store(&_host, std::memory_order_release);
        _context->pAudioCallback.store(&HostOutput::Callback, std::memory_order_release);
    }

    void TearDown() override
    {
        _sound.reset();
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    void SetT(uint32_t t) { _context->pCore->GetZ80()->tt = t << 8; }

    /// One frame with a square wave on the beeper, numbered like the main loop numbers it
    void RunBeepingFrame(uint64_t frame)
    {
        _context->emulatorState.frame_counter = frame;
        SetT(0);
        _sound->handleFrameStart();
        for (int i = 0; i < 100; i++)
            _sound->getBeeper().handlePortOut((i & 1) ? 0x10 : 0x00, 500 + i * 600);
        SetT(PENTAGON_FRAME);
        _sound->handleStep();
        _sound->handleFrameEnd();
    }

    static int16_t Peak(const std::vector<int16_t>& samples)
    {
        int16_t peak = 0;
        for (int16_t v : samples)
            peak = std::max<int16_t>(peak, v < 0 ? static_cast<int16_t>(-v) : v);
        return peak;
    }
};

TEST_F(FrameAudioTap_Test, EveryFrameArrivesWithItsNumberAndSamplesBeforeMute)
{
    std::vector<Tapped> tapped;
    const uint64_t id = _sound->addFrameAudioTap([&tapped](const int16_t* samples, size_t sampleFrames, uint64_t frame) {
        tapped.push_back({frame, std::vector<int16_t>(samples, samples + sampleFrames * AUDIO_CHANNELS)});
    });
    EXPECT_TRUE(_sound->hasFrameAudioTaps());
    _sound->mute();

    size_t expectedPairs = 0;
    for (uint64_t frame = 10; frame < 13; frame++)
    {
        RunBeepingFrame(frame);
        expectedPairs += _sound->lastFrameSamples();
    }
    ASSERT_EQ(tapped.size(), 3u);
    size_t pairs = 0;
    for (size_t i = 0; i < tapped.size(); i++)
    {
        EXPECT_EQ(tapped[i].frame, 10 + i);
        EXPECT_GT(Peak(tapped[i].samples), 0) << "frame " << tapped[i].frame << ": the sound before mute";
        pairs += tapped[i].samples.size() / AUDIO_CHANNELS;
    }
    // 44100 / (3.5 MHz / 71680) = 903.168 pairs a frame: the frames' own counts, summed
    EXPECT_EQ(pairs, expectedPairs);

    _sound->removeFrameAudioTap(id);
    EXPECT_FALSE(_sound->hasFrameAudioTaps());
    RunBeepingFrame(13);
    EXPECT_EQ(tapped.size(), 3u) << "nothing after removal";
}

TEST_F(FrameAudioTap_Test, TurboWithoutSoundStillSynthesisesForATap)
{
    _context->config.turbo_mode = true;
    _context->config.turbo_mode_audio = false;
    std::vector<int16_t> last;
    const uint64_t id = _sound->addFrameAudioTap([&last](const int16_t* samples, size_t sampleFrames, uint64_t) {
        last.assign(samples, samples + sampleFrames * AUDIO_CHANNELS);
    });
    RunBeepingFrame(1);
    RunBeepingFrame(2);
    EXPECT_GT(Peak(last), 0) << "a tap keeps the synthesis on in turbo, as a recording does";
    _sound->removeFrameAudioTap(id);
    _context->config.turbo_mode = false;
}
