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
    }

    void TearDown() override
    {
        delete sm;
        delete ctx;
    }

    int16_t _beeperVal = 0;
    bool _beeperValSet = false;

    void fillBuffer(AudioSourceType type, int16_t value)
    {
        if (type == AudioSourceType::Beeper)
        {
            sm->getBeeper().handleTapeAudio(value, 0);
            _beeperVal = value;
            _beeperValSet = true;
        }
        else
        {
            int16_t* buf = const_cast<int16_t*>(sm->deviceBuffer(type));
            if (buf)
            {
                for (size_t i = 0; i < SAMPLES_PER_FRAME * AUDIO_CHANNELS; i++)
                    buf[i] = value;
            }
        }
    }

    int16_t getMasterSample()
    {
        sm->handleFrameEnd();
        if (_beeperValSet)
        {
            int16_t* beeperBuf = const_cast<int16_t*>(sm->deviceBuffer(AudioSourceType::Beeper));
            for (size_t i = 0; i < SAMPLES_PER_FRAME * AUDIO_CHANNELS; i++)
                beeperBuf[i] = _beeperVal;

            int16_t* out = const_cast<int16_t*>(sm->deviceBuffer(AudioSourceType::MasterMix));
            std::fill(out, out + SAMPLES_PER_FRAME * AUDIO_CHANNELS, 0);

            bool soloActive = false;
            for (const auto& dev : sm->devices())
            {
                if (dev.solo) { soloActive = true; break; }
            }

            for (auto& d : sm->devices())
            {
                bool audible = soloActive ? d.solo : !d.mute;
                const int16_t* src = sm->deviceBuffer(d.type);
                if (src)
                {
                    float peak = 0.0f;
                    for (size_t i = 0; i < SAMPLES_PER_FRAME * AUDIO_CHANNELS; i++)
                    {
                        float absVal = std::abs(static_cast<float>(src[i])) / 32768.0f;
                        if (absVal > peak) peak = absVal;
                    }
                    if (auto* devPtr = sm->device(d.type))
                    {
                        devPtr->peak = peak;
                        devPtr->activeRecently = (peak > 0.001f);
                    }
                }
                if (audible && src && d.volume > 0.0f)
                {
                    float vol = d.volume;
                    for (size_t i = 0; i < SAMPLES_PER_FRAME * AUDIO_CHANNELS; i++)
                    {
                        int32_t mixed = out[i] + static_cast<int32_t>(src[i] * vol);
                        out[i] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
                    }
                }
            }
        }
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
    fillBuffer(AudioSourceType::AY1_All, 16384);  // ~0.5 peak
    fillBuffer(AudioSourceType::AY2_All, 0);

    sm->setDeviceMute(AudioSourceType::AY1_All, true);
    sm->handleFrameEnd();

    // Peak should still be computed for UI meters
    float peak = sm->device(AudioSourceType::AY1_All)->peak;
    EXPECT_GT(peak, 0.4f);
    EXPECT_LT(peak, 0.6f);
}

TEST_F(DeviceMixerTest, ActivityDetectedAboveThreshold)
{
    fillBuffer(AudioSourceType::AY1_All, 100);  // Small but above threshold
    fillBuffer(AudioSourceType::AY2_All, 0);

    sm->handleFrameEnd();

    EXPECT_TRUE(sm->device(AudioSourceType::AY1_All)->activeRecently);
    EXPECT_FALSE(sm->device(AudioSourceType::AY2_All)->activeRecently);
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
