#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

#include <gtest/gtest.h>

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
    void SetUp() override
    {
        _context = std::make_unique<EmulatorContext>();
        _context->config.frame = PENTAGON_FRAME;
        _context->pCore = &_core;
        _core.setContext(_context.get());
    }

    void SetT(uint32_t t) { _core.getZ80()->t = t; }

    std::unique_ptr<EmulatorContext> _context;
    Core _core;
};

/// Test that legacy TurboSound device outputs silence when idle
TEST_F(IdleSilence_Test, LegacyTurboSound_IdleSilence)
{
    auto device = std::make_unique<SoundChip_TurboSound>(_context.get());
    device->setCoreRate(44100);
    device->reset();

    for (size_t frame = 0; frame < FRAMES_TO_TEST; frame++)
    {
        SetT(0);
        device->handleFrameStart();
        SetT(PENTAGON_FRAME);
        device->handleStep();

        // Check combined buffer
        const int16_t* combined = reinterpret_cast<const int16_t*>(device->getBuffer());
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
    auto device = std::make_unique<SoundChip_TurboSoundFM>(_context.get());
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
        const int16_t* combined = reinterpret_cast<const int16_t*>(device->getBuffer());
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
    _context->config.sound.turboSoundKind = TurboSoundKind::TurboSound;

    SoundManager soundManager(_context.get());
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
        const AudioFrameDescriptor& desc = soundManager.getBufferDescriptor();
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

    SoundManager soundManager(_context.get());
    soundManager.reset();

    for (size_t frame = 0; frame < FRAMES_TO_TEST; frame++)
    {
        SetT(0);
        soundManager.handleFrameStart();

        // No handleStep calls - simulating idle

        SetT(PENTAGON_FRAME);
        soundManager.handleFrameEnd();

        const AudioFrameDescriptor& desc = soundManager.getBufferDescriptor();
        const int16_t* outBuffer = reinterpret_cast<const int16_t*>(desc.memoryBuffer);
        const size_t samples = MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS;

        int16_t peak = findPeak(outBuffer, samples);
        EXPECT_EQ(peak, 0)
            << "SoundManager (TSFM) output not silent at frame " << frame
            << ", peak: " << peak;
    }
}

}  // namespace
