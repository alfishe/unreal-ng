#include <gtest/gtest.h>
#include <cmath>
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/audio.h"

class DeviceMixerTest : public ::testing::Test
{
protected:
    EmulatorContext* ctx = nullptr;
    SoundManager* sm = nullptr;

    void SetUp() override
    {
        ctx = new EmulatorContext(LoggerLevel::LogError);
        sm = new SoundManager(ctx);
        sm->reset();

        // Disable DSP chains for predictable test values
        sm->getAYChain().setPunchEnabled(false);
        sm->getAYChain().setRoomMode(AudioCharacterChain::RoomMode::Off);
        sm->syncAYChainSettings();  // Apply to both AY chains
        sm->getBeeperChain().setPunchEnabled(false);
        sm->getBeeperChain().setRoomMode(AudioCharacterChain::RoomMode::Off);
        sm->setBeeperFilterEnabled(false);
    }

    void TearDown() override
    {
        delete sm;
        delete ctx;
    }

    // Fill a device buffer with a known pattern
    void fillBuffer(AudioSourceType type, int16_t value)
    {
        int16_t* buf = nullptr;
        switch (type)
        {
            case AudioSourceType::Beeper:
                buf = const_cast<int16_t*>(sm->deviceBuffer(AudioSourceType::Beeper));
                break;
            case AudioSourceType::AY1_All:
                buf = sm->getTurboSound()->getChipBuffer(0);
                break;
            case AudioSourceType::AY2_All:
                buf = sm->getTurboSound()->getChipBuffer(1);
                break;
            default:
                return;
        }
        if (buf)
        {
            for (size_t i = 0; i < SAMPLES_PER_FRAME * AUDIO_CHANNELS; i++)
                buf[i] = value;
        }
    }

    // Get the first sample from master output
    int16_t getMasterSample()
    {
        sm->handleFrameEnd();
        return sm->deviceBuffer(AudioSourceType::MasterMix)[0];
    }
};

TEST_F(DeviceMixerTest, AllDevicesAudibleByDefault)
{
    // Fill each device with distinct values
    fillBuffer(AudioSourceType::Beeper, 1000);
    fillBuffer(AudioSourceType::AY1_All, 2000);
    fillBuffer(AudioSourceType::AY2_All, 3000);

    int16_t out = getMasterSample();
    // Should be sum of all three (~6000), allow ±5 for floating point rounding
    EXPECT_NEAR(out, 6000, 5);
}

TEST_F(DeviceMixerTest, MutedDeviceNotInMix)
{
    fillBuffer(AudioSourceType::Beeper, 1000);
    fillBuffer(AudioSourceType::AY1_All, 2000);
    fillBuffer(AudioSourceType::AY2_All, 3000);

    sm->setDeviceMute(AudioSourceType::Beeper, true);

    int16_t out = getMasterSample();
    // Should be AY1 + AY2 = 5000
    EXPECT_NEAR(out, 5000, 2);
}

TEST_F(DeviceMixerTest, SoloOnlyPlaysSoloedDevices)
{
    fillBuffer(AudioSourceType::Beeper, 1000);
    fillBuffer(AudioSourceType::AY1_All, 2000);
    fillBuffer(AudioSourceType::AY2_All, 3000);

    // Solo only AY1
    sm->setDeviceSolo(AudioSourceType::AY1_All, true);

    int16_t out = getMasterSample();
    // Only AY1 should be audible
    EXPECT_NEAR(out, 2000, 2);
}

TEST_F(DeviceMixerTest, MultiSoloPlaysAllSoloedDevices)
{
    fillBuffer(AudioSourceType::Beeper, 1000);
    fillBuffer(AudioSourceType::AY1_All, 2000);
    fillBuffer(AudioSourceType::AY2_All, 3000);

    // Solo beeper and AY2
    sm->setDeviceSolo(AudioSourceType::Beeper, true);
    sm->setDeviceSolo(AudioSourceType::AY2_All, true);

    int16_t out = getMasterSample();
    // Beeper + AY2 = 4000
    EXPECT_NEAR(out, 4000, 2);
}

TEST_F(DeviceMixerTest, SoloOverridesMute)
{
    fillBuffer(AudioSourceType::Beeper, 1000);
    fillBuffer(AudioSourceType::AY1_All, 2000);
    fillBuffer(AudioSourceType::AY2_All, 3000);

    // Mute beeper, but also solo it
    sm->setDeviceMute(AudioSourceType::Beeper, true);
    sm->setDeviceSolo(AudioSourceType::Beeper, true);

    int16_t out = getMasterSample();
    // Beeper is soloed, so it plays despite being muted
    // (solo semantics: if any solo active, only soloed devices play)
    EXPECT_NEAR(out, 1000, 2);
}

TEST_F(DeviceMixerTest, VolumeScalesOutput)
{
    fillBuffer(AudioSourceType::Beeper, 0);
    fillBuffer(AudioSourceType::AY1_All, 10000);
    fillBuffer(AudioSourceType::AY2_All, 0);

    sm->setDeviceVolume(AudioSourceType::AY1_All, 0.5f);

    int16_t out = getMasterSample();
    EXPECT_NEAR(out, 5000, 2);
}

TEST_F(DeviceMixerTest, ZeroVolumeSilencesDevice)
{
    fillBuffer(AudioSourceType::Beeper, 1000);
    fillBuffer(AudioSourceType::AY1_All, 2000);
    fillBuffer(AudioSourceType::AY2_All, 3000);

    sm->setDeviceVolume(AudioSourceType::AY1_All, 0.0f);

    int16_t out = getMasterSample();
    // Beeper + AY2 only
    EXPECT_NEAR(out, 4000, 2);
}

TEST_F(DeviceMixerTest, PeakCalculatedEvenWhenMuted)
{
    fillBuffer(AudioSourceType::Beeper, 16384);  // ~0.5 peak
    fillBuffer(AudioSourceType::AY1_All, 0);
    fillBuffer(AudioSourceType::AY2_All, 0);

    sm->setDeviceMute(AudioSourceType::Beeper, true);
    sm->handleFrameEnd();

    // Peak should still be computed for UI meters
    float peak = sm->device(AudioSourceType::Beeper)->peak;
    EXPECT_GT(peak, 0.4f);
    EXPECT_LT(peak, 0.6f);
}

TEST_F(DeviceMixerTest, ActivityDetectedAboveThreshold)
{
    fillBuffer(AudioSourceType::Beeper, 100);  // Small but above threshold
    fillBuffer(AudioSourceType::AY1_All, 0);
    fillBuffer(AudioSourceType::AY2_All, 0);

    sm->handleFrameEnd();

    EXPECT_TRUE(sm->device(AudioSourceType::Beeper)->activeRecently);
    EXPECT_FALSE(sm->device(AudioSourceType::AY1_All)->activeRecently);
}

TEST_F(DeviceMixerTest, SaturatingMixDoesNotOverflow)
{
    fillBuffer(AudioSourceType::Beeper, 20000);
    fillBuffer(AudioSourceType::AY1_All, 20000);
    fillBuffer(AudioSourceType::AY2_All, 0);

    int16_t out = getMasterSample();
    // Should saturate to INT16_MAX, not wrap
    EXPECT_EQ(out, 32767);
}

TEST_F(DeviceMixerTest, NegativeSaturation)
{
    fillBuffer(AudioSourceType::Beeper, -20000);
    fillBuffer(AudioSourceType::AY1_All, -20000);
    fillBuffer(AudioSourceType::AY2_All, 0);

    int16_t out = getMasterSample();
    // Should saturate to INT16_MIN
    EXPECT_EQ(out, -32768);
}
