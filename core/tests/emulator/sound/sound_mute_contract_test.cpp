#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/beeper.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/soundmanager.h"

/// Sound-off contract, for every sound generator (beeper, Covox, TurboSound AY, TurboSound FM).
///
/// With the `sound` feature off:
///  - GENERATION and its filters are off: nothing reaches the mixed output and the per-device
///    buffers stay silent, however hard the program drives the device;
///  - the REGISTER / STATE part stays live: what the program can read back (AY registers) and what
///    the next edge after un-muting depends on (the beeper / Covox output levels) is still tracked,
///    so switching sound back on neither loses state nor produces a spurious step;
///  - switching sound back on resumes audible output.
/// (The FM core's CPU-visible state - timers, busy, status - is covered in tsfm_core_test.cpp.)

namespace
{

constexpr uint32_t PENTAGON_FRAME = 71680;

int16_t Peak(const int16_t* buffer, size_t count)
{
    int16_t peak = 0;
    for (size_t i = 0; i < count; i++)
    {
        const int16_t v = buffer[i] < 0 ? int16_t(-buffer[i]) : buffer[i];
        if (v > peak)
            peak = v;
    }
    return peak;
}

}  // namespace

class SoundMuteContract_Test : public ::testing::TestWithParam<TurboSoundKind>
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    std::unique_ptr<SoundManager> _sound;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _context->config.frame = PENTAGON_FRAME;
        _context->config.sound.turboSoundKind = GetParam();
        _context->config.sound.covoxFB = 1;

        _sound = std::make_unique<SoundManager>(_context);
        _sound->reset();

        // Character chains are irrelevant here; keep the signal path plain
        _sound->getAYChain().setPunchEnabled(false);
        _sound->getBeeperChain().setPunchEnabled(false);

        ASSERT_TRUE(_sound->hasCovox());
        ASSERT_NE(_sound->getTurboSound(), nullptr);
        ASSERT_EQ(_sound->getTurboSound()->hasFm(), GetParam() == TurboSoundKind::FM);
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

    void SetSoundFeature(bool enabled)
    {
        ASSERT_TRUE(_context->pFeatureManager->setFeature(Features::kSoundGeneration, enabled));
        _sound->UpdateFeatureCache();
    }

    /// AY register write through the TurboSound ports
    void WriteAy(uint8_t reg, uint8_t value)
    {
        _sound->getTurboSound()->portDeviceOutMethod(PORT_FFFD, reg);
        _sound->getTurboSound()->portDeviceOutMethod(PORT_BFFD, value);
    }

    uint8_t ReadAy(uint8_t reg)
    {
        _sound->getTurboSound()->portDeviceOutMethod(PORT_FFFD, reg);
        return _sound->getTurboSound()->portDeviceInMethod(PORT_FFFD);
    }

    /// Program a loud steady tone on AY channel A
    void StartAyTone()
    {
        WriteAy(0, 0x20);   // Tone A period low
        WriteAy(1, 0x00);   // Tone A period high
        WriteAy(7, 0x3E);   // Mixer: tone A on, everything else off
        WriteAy(8, 0x0F);   // Channel A volume: max, no envelope
    }

    /// One emulated frame: drive every generator, then run the frame like the main loop does.
    /// Beeper toggles 100x and the Covox DAC swings 50x inside the frame; the AY plays its tone
    void RunDrivenFrame(bool driveBeeperAndCovox = true)
    {
        SetT(0);
        _sound->handleFrameStart();

        if (driveBeeperAndCovox)
        {
            for (int i = 0; i < 100; i++)
                _sound->getBeeper().handlePortOut((i & 1) ? 0x10 : 0x00, 500 + i * 600);

            for (int i = 0; i < 50; i++)
            {
                SetT(500 + i * 1200);
                _sound->getCovox()->portDeviceOutMethod(0x00F1, (i & 1) ? 0xFF : 0x00);
            }
        }

        SetT(PENTAGON_FRAME / 2);
        _sound->handleStep();
        SetT(PENTAGON_FRAME);
        _sound->handleStep();
        _sound->handleFrameEnd();
    }

    /// Peak of the mixed output of the last frame
    int16_t OutputPeak()
    {
        const AudioFrameDescriptor& desc = _sound->getAudioBufferDescriptor();
        return Peak(reinterpret_cast<const int16_t*>(desc.memoryBuffer), MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS);
    }

    /// Peak of one registered source's own buffer after the last frame
    float DevicePeak(AudioSourceType type)
    {
        const AudioDeviceInfo* info = _sound->device(type);
        return info ? info->peak : -1.0f;
    }
};

INSTANTIATE_TEST_SUITE_P(TurboSoundKinds, SoundMuteContract_Test,
                         ::testing::Values(TurboSoundKind::AY, TurboSoundKind::FM),
                         [](const ::testing::TestParamInfo<TurboSoundKind>& info) {
                             return info.param == TurboSoundKind::AY ? "AY" : "FM";
                         });

/// Control: with sound ON every generator really is audible under this drive. Without it the
/// silence assertions below would prove nothing
TEST_P(SoundMuteContract_Test, SoundOn_EveryGeneratorIsAudible)
{
    StartAyTone();
    for (int i = 0; i < 3; i++)
        RunDrivenFrame();

    EXPECT_GT(DevicePeak(AudioSourceType::Beeper), 0.01f) << "beeper produced no output";
    EXPECT_GT(DevicePeak(AudioSourceType::COVOX), 0.01f) << "Covox produced no output";
    EXPECT_GT(DevicePeak(AudioSourceType::AY1_All), 0.01f) << "AY produced no output";
    EXPECT_GT(OutputPeak(), 0) << "nothing in the mixed output";
}

/// Generation off: no generator reaches the output or its own buffer
TEST_P(SoundMuteContract_Test, SoundOff_NoGeneratorProducesOutput)
{
    SetSoundFeature(false);
    StartAyTone();
    for (int i = 0; i < 3; i++)
    {
        RunDrivenFrame();

        EXPECT_EQ(OutputPeak(), 0) << "mixed output not silent with sound off, frame " << i;
        EXPECT_EQ(DevicePeak(AudioSourceType::Beeper), 0.0f) << "beeper generated with sound off, frame " << i;
        EXPECT_EQ(DevicePeak(AudioSourceType::COVOX), 0.0f) << "Covox generated with sound off, frame " << i;
        EXPECT_EQ(DevicePeak(AudioSourceType::AY1_All), 0.0f) << "AY generated with sound off, frame " << i;
        if (GetParam() == TurboSoundKind::FM)
        {
            EXPECT_EQ(DevicePeak(AudioSourceType::FM1), 0.0f) << "FM 1 generated with sound off, frame " << i;
            EXPECT_EQ(DevicePeak(AudioSourceType::FM2), 0.0f) << "FM 2 generated with sound off, frame " << i;
        }
    }
    EXPECT_EQ(_sound->getTurboSound()->getRenderedSamplesThisFrame(), 0u) << "TurboSound rendered with sound off";
}

/// Register part stays live with sound off: the program can still read back the AY registers
TEST_P(SoundMuteContract_Test, SoundOff_AyRegistersStillReadBack)
{
    SetSoundFeature(false);
    WriteAy(8, 0x0A);
    WriteAy(9, 0x05);
    WriteAy(0, 0x42);
    EXPECT_EQ(ReadAy(8), 0x0A);
    EXPECT_EQ(ReadAy(9), 0x05);
    EXPECT_EQ(ReadAy(0), 0x42);

    RunDrivenFrame(false);  // A frame passing must not disturb the register file
    EXPECT_EQ(ReadAy(8), 0x0A);
    EXPECT_EQ(ReadAy(9), 0x05);
    EXPECT_EQ(ReadAy(0), 0x42);
}

/// State part stays live with sound off: the beeper / Covox levels are tracked, so the first edge
/// after un-muting is a real step (from the tracked level), not a missed or spurious one
TEST_P(SoundMuteContract_Test, SoundOff_BeeperAndCovoxLevelsStillTracked)
{
    SetSoundFeature(false);

    // Beeper driven HIGH and Covox DAC driven to full scale while muted
    SetT(0);
    _sound->handleFrameStart();
    _sound->getBeeper().handlePortOut(0x10, 1000);
    SetT(1000);
    _sound->getCovox()->portDeviceOutMethod(0x00F1, 0xFF);
    SetT(PENTAGON_FRAME);
    _sound->handleStep();
    _sound->handleFrameEnd();
    EXPECT_EQ(OutputPeak(), 0) << "steady levels reached the output with sound off";

    SetSoundFeature(true);

    // Now drop both levels to idle: from the TRACKED high level that is a real falling edge
    // (audible step); had the level been lost while muted it would be "no change" - silence
    SetT(0);
    _sound->handleFrameStart();
    _sound->getBeeper().handlePortOut(0x00, 20000);
    SetT(20000);
    _sound->getCovox()->portDeviceOutMethod(0x00F1, 0x80);
    SetT(PENTAGON_FRAME);
    _sound->handleStep();
    _sound->handleFrameEnd();

    EXPECT_GT(DevicePeak(AudioSourceType::Beeper), 0.001f) << "beeper level was not tracked while muted";
    EXPECT_GT(DevicePeak(AudioSourceType::COVOX), 0.001f) << "Covox level was not tracked while muted";
}

/// Sound off then on again: generation resumes and the AY keeps playing what was programmed
TEST_P(SoundMuteContract_Test, SoundBackOn_GenerationResumes)
{
    StartAyTone();

    SetSoundFeature(false);
    for (int i = 0; i < 2; i++)
        RunDrivenFrame();
    ASSERT_EQ(OutputPeak(), 0);

    SetSoundFeature(true);
    for (int i = 0; i < 3; i++)
        RunDrivenFrame();

    EXPECT_GT(DevicePeak(AudioSourceType::AY1_All), 0.01f) << "AY did not resume after sound back on";
    EXPECT_GT(DevicePeak(AudioSourceType::Beeper), 0.01f) << "beeper did not resume after sound back on";
    EXPECT_GT(DevicePeak(AudioSourceType::COVOX), 0.01f) << "Covox did not resume after sound back on";
    EXPECT_GT(OutputPeak(), 0);
}
