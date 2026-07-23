#include <gtest/gtest.h>
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"

class DeviceRegistryTest : public ::testing::Test
{
protected:
    EmulatorContext* ctx = nullptr;
    SoundManager* sm = nullptr;

    void SetUp() override
    {
        ctx = new EmulatorContext(LoggerLevel::LogError);
        sm = new SoundManager(ctx);
    }

    void TearDown() override
    {
        delete sm;
        delete ctx;
    }
};

TEST_F(DeviceRegistryTest, RegistryContainsBeeperAndAY)
{
    const auto& devices = sm->devices();
    ASSERT_GE(devices.size(), 2u);

    // Beeper always present
    auto* beeper = sm->device(AudioSourceType::Beeper);
    ASSERT_NE(beeper, nullptr);
    EXPECT_EQ(beeper->name, "Beeper");

    // AY 1 always present
    auto* ay1 = sm->device(AudioSourceType::AY1_All);
    ASSERT_NE(ay1, nullptr);
    EXPECT_EQ(ay1->name, "AY 1");
}

TEST_F(DeviceRegistryTest, TurboSoundHasTwoAYChips)
{
    // Default construction creates TurboSound with 2 chips
    EXPECT_EQ(sm->getAYChipCount(), 2);

    auto* ay2 = sm->device(AudioSourceType::AY2_All);
    ASSERT_NE(ay2, nullptr);
    EXPECT_EQ(ay2->name, "AY 2");
}

TEST_F(DeviceRegistryTest, DefaultStateNoMuteNoSoloFullVolume)
{
    for (const auto& d : sm->devices())
    {
        EXPECT_FALSE(d.mute) << "Device " << d.name << " should not be muted by default";
        EXPECT_FALSE(d.solo) << "Device " << d.name << " should not be soloed by default";
        EXPECT_FLOAT_EQ(d.volume, 1.0f) << "Device " << d.name << " should have full volume by default";
    }
}

TEST_F(DeviceRegistryTest, SetDeviceMute)
{
    sm->setDeviceMute(AudioSourceType::Beeper, true);
    EXPECT_TRUE(sm->device(AudioSourceType::Beeper)->mute);

    sm->setDeviceMute(AudioSourceType::Beeper, false);
    EXPECT_FALSE(sm->device(AudioSourceType::Beeper)->mute);
}

TEST_F(DeviceRegistryTest, SetDeviceSolo)
{
    sm->setDeviceSolo(AudioSourceType::AY1_All, true);
    EXPECT_TRUE(sm->device(AudioSourceType::AY1_All)->solo);

    sm->setDeviceSolo(AudioSourceType::AY1_All, false);
    EXPECT_FALSE(sm->device(AudioSourceType::AY1_All)->solo);
}

TEST_F(DeviceRegistryTest, SetDeviceVolume)
{
    sm->setDeviceVolume(AudioSourceType::Beeper, 0.5f);
    EXPECT_FLOAT_EQ(sm->device(AudioSourceType::Beeper)->volume, 0.5f);

    // Clamp to [0, 1]
    sm->setDeviceVolume(AudioSourceType::Beeper, 1.5f);
    EXPECT_FLOAT_EQ(sm->device(AudioSourceType::Beeper)->volume, 1.0f);

    sm->setDeviceVolume(AudioSourceType::Beeper, -0.5f);
    EXPECT_FLOAT_EQ(sm->device(AudioSourceType::Beeper)->volume, 0.0f);
}

TEST_F(DeviceRegistryTest, LegacyVolumeAPIDelegatesToRegistry)
{
    sm->setAYVolume(0.7);
    EXPECT_FLOAT_EQ(sm->device(AudioSourceType::AY1_All)->volume, 0.7f);
    EXPECT_FLOAT_EQ(sm->device(AudioSourceType::AY2_All)->volume, 0.7f);

    sm->setBeeperVolume(0.3);
    EXPECT_FLOAT_EQ(sm->device(AudioSourceType::Beeper)->volume, 0.3f);
}

TEST_F(DeviceRegistryTest, DeviceBufferReturnsCorrectPointers)
{
    // MasterMix returns output buffer
    const int16_t* master = sm->deviceBuffer(AudioSourceType::MasterMix);
    EXPECT_NE(master, nullptr);

    // Beeper returns beeper buffer
    const int16_t* beeper = sm->deviceBuffer(AudioSourceType::Beeper);
    EXPECT_NE(beeper, nullptr);
    EXPECT_NE(beeper, master);

    // AY chips return per-chip buffers
    const int16_t* ay1 = sm->deviceBuffer(AudioSourceType::AY1_All);
    const int16_t* ay2 = sm->deviceBuffer(AudioSourceType::AY2_All);
    EXPECT_NE(ay1, nullptr);
    EXPECT_NE(ay2, nullptr);
    EXPECT_NE(ay1, ay2);
}

TEST_F(DeviceRegistryTest, UnknownDeviceReturnsNullptr)
{
    auto* covox = sm->device(AudioSourceType::COVOX);
    EXPECT_EQ(covox, nullptr);

    const int16_t* covoxBuf = sm->deviceBuffer(AudioSourceType::COVOX);
    EXPECT_EQ(covoxBuf, nullptr);
}
