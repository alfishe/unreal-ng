#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"

/// Idle silence tests: verify that freshly initialized sound devices produce
/// zero output when no audio activity occurs. Regression guard for DC offset
/// or low-level noise ("silent hissing") in the audio path.

namespace
{

constexpr uint32_t PENTAGON_FRAME = 71680;
constexpr size_t FRAMES_TO_TEST = 10;

/// Check if a buffer is all zeros
bool isBufferSilent(const int16_t* buffer, size_t sampleCount)
{
    for (size_t i = 0; i < sampleCount; i++)
    {
        if (buffer[i] != 0)
            return false;
    }
    return true;
}

/// Find peak absolute value in buffer
int16_t findPeak(const int16_t* buffer, size_t sampleCount)
{
    int16_t peak = 0;
    for (size_t i = 0; i < sampleCount; i++)
    {
        int16_t v = buffer[i] < 0 ? int16_t(-buffer[i]) : buffer[i];
        if (v > peak)
            peak = v;
    }
    return peak;
}

class IdleSilence_Test : public ::testing::Test
{
protected:
    // A standard Pentagon emulator supplies the context the sound devices
    // need (core, memory, feature manager); the frame counter is driven by
    // hand exactly like the other sound unit tests
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();
        _context->config.frame = PENTAGON_FRAME;
        _context->pCore->GetZ80()->tt = 0;
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

    void SetT(uint32_t t) { _context->pCore->GetZ80()->tt = t << 8; }
};

/// Test that legacy TurboSound device outputs silence when idle
TEST_F(IdleSilence_Test, LegacyTurboSound_IdleSilence)
{
    auto device = std::make_unique<SoundChip_TurboSound>(_context);
    device->setCoreRate(44100);
    device->reset();

    for (size_t frame = 0; frame < FRAMES_TO_TEST; frame++)
    {
        SetT(0);
        device->handleFrameStart();
        SetT(PENTAGON_FRAME);
        device->handleStep();

        // Check combined buffer
        const int16_t* combined = reinterpret_cast<const int16_t*>(device->getAudioBuffer());
        const size_t samples = device->getRenderedSamplesThisFrame() * AUDIO_CHANNELS;

        EXPECT_TRUE(isBufferSilent(combined, samples))
            << "Legacy TurboSound combined buffer not silent at frame " << frame
            << ", peak: " << findPeak(combined, samples);

        // Check per-chip buffers
        const int16_t* chip0 = device->getChipBuffer(0);
        const int16_t* chip1 = device->getChipBuffer(1);

        EXPECT_TRUE(isBufferSilent(chip0, samples))
            << "Legacy TurboSound chip0 buffer not silent at frame " << frame
            << ", peak: " << findPeak(chip0, samples);

        EXPECT_TRUE(isBufferSilent(chip1, samples))
            << "Legacy TurboSound chip1 buffer not silent at frame " << frame
            << ", peak: " << findPeak(chip1, samples);
    }
}

/// Test that TSFM device outputs silence when idle
TEST_F(IdleSilence_Test, TurboSoundFM_IdleSilence)
{
    auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
    device->setCoreRate(44100);
    device->reset();

    for (size_t frame = 0; frame < FRAMES_TO_TEST; frame++)
    {
        SetT(0);
        device->handleFrameStart();
        SetT(PENTAGON_FRAME);
        device->handleStep();
        device->handleFrameEnd();

        const size_t samples = device->getRenderedSamplesThisFrame() * AUDIO_CHANNELS;

        // Check combined buffer
        const int16_t* combined = reinterpret_cast<const int16_t*>(device->getAudioBuffer());
        EXPECT_TRUE(isBufferSilent(combined, samples))
            << "TSFM combined buffer not silent at frame " << frame
            << ", peak: " << findPeak(combined, samples);

        // Check per-chip SSG buffers
        const int16_t* chip0 = device->getChipBuffer(0);
        const int16_t* chip1 = device->getChipBuffer(1);

        EXPECT_TRUE(isBufferSilent(chip0, samples))
            << "TSFM chip0 SSG buffer not silent at frame " << frame
            << ", peak: " << findPeak(chip0, samples);

        EXPECT_TRUE(isBufferSilent(chip1, samples))
            << "TSFM chip1 SSG buffer not silent at frame " << frame
            << ", peak: " << findPeak(chip1, samples);

        // Check FM buffers
        const int16_t* fm0 = device->getFmBuffer(0);
        const int16_t* fm1 = device->getFmBuffer(1);

        EXPECT_TRUE(isBufferSilent(fm0, samples))
            << "TSFM FM0 buffer not silent at frame " << frame
            << ", peak: " << findPeak(fm0, samples);

        EXPECT_TRUE(isBufferSilent(fm1, samples))
            << "TSFM FM1 buffer not silent at frame " << frame
            << ", peak: " << findPeak(fm1, samples);
    }
}

/// Test that beeper outputs silence when idle (no edge transitions)
TEST_F(IdleSilence_Test, Beeper_IdleSilence)
{
    // The beeper test requires SoundManager for proper initialization
    // This is a placeholder - beeper idle silence should be tested
    // at the SoundManager level
    GTEST_SKIP() << "Beeper idle silence requires SoundManager integration test";
}

/// Test full SoundManager output is silent when no emulation runs
TEST_F(IdleSilence_Test, SoundManager_IdleOutput)
{
    _context->config.sound.turboSoundKind = TurboSoundKind::AY;

    auto soundManagerOwner = std::make_unique<SoundManager>(_context);  // heap: SoundManager is far too large for the stack
    SoundManager& soundManager = *soundManagerOwner;
    soundManager.reset();

    // Capture output
    std::vector<int16_t> capturedAudio;

    for (size_t frame = 0; frame < FRAMES_TO_TEST; frame++)
    {
        SetT(0);
        soundManager.handleFrameStart();

        // No handleStep calls - simulating idle

        SetT(PENTAGON_FRAME);
        soundManager.handleFrameEnd();

        // Get the output buffer
        const AudioFrameDescriptor& desc = soundManager.getAudioBufferDescriptor();
        const int16_t* outBuffer = reinterpret_cast<const int16_t*>(desc.memoryBuffer);
        const size_t samples = MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS;

        int16_t peak = findPeak(outBuffer, samples);
        EXPECT_EQ(peak, 0)
            << "SoundManager output not silent at frame " << frame
            << ", peak: " << peak;
    }
}

/// Test full SoundManager with TSFM outputs silence when idle
TEST_F(IdleSilence_Test, SoundManager_TSFM_IdleOutput)
{
    _context->config.sound.turboSoundKind = TurboSoundKind::FM;

    auto soundManagerOwner = std::make_unique<SoundManager>(_context);  // heap: SoundManager is far too large for the stack
    SoundManager& soundManager = *soundManagerOwner;
    soundManager.reset();

    for (size_t frame = 0; frame < FRAMES_TO_TEST; frame++)
    {
        SetT(0);
        soundManager.handleFrameStart();

        // No handleStep calls - simulating idle

        SetT(PENTAGON_FRAME);
        soundManager.handleFrameEnd();

        const AudioFrameDescriptor& desc = soundManager.getAudioBufferDescriptor();
        const int16_t* outBuffer = reinterpret_cast<const int16_t*>(desc.memoryBuffer);
        const size_t samples = MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS;

        int16_t peak = findPeak(outBuffer, samples);
        EXPECT_EQ(peak, 0)
            << "SoundManager (TSFM) output not silent at frame " << frame
            << ", peak: " << peak;
    }
}

}  // namespace
