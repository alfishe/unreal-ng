#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundcharactersettings.h"
#include "emulator/sound/soundmanager.h"

/// SoundCharacterSettings: the one parser / formatter every automation
/// surface (CLI, WebAPI, Lua, Python) uses for ay_voicing, ay_punch, ay_room
/// and beeper_punch. Pinned: names, get/set round trips through the
/// SoundManager requests, accepted aliases, rejection of bad and hidden values.

class SoundCharacterSettings_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    SoundManager* _sound = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _sound = new SoundManager(_context);
    }

    void TearDown() override
    {
        delete _sound;
        delete _context;
    }
};

TEST_F(SoundCharacterSettings_Test, KnowsExactlyTheFourSettings)
{
    ASSERT_EQ(SoundCharacterSettings::Descriptors().size(), 4u);
    for (const char* name : {"ay_voicing", "ay_punch", "ay_room", "beeper_punch"})
        EXPECT_NE(SoundCharacterSettings::Find(name), nullptr) << name;
    EXPECT_NE(SoundCharacterSettings::Find("AY_VOICING"), nullptr) << "names are case-insensitive";
    EXPECT_EQ(SoundCharacterSettings::Find("audio_rate"), nullptr);
    EXPECT_TRUE(SoundCharacterSettings::Find("ay_punch")->isBool);
    EXPECT_FALSE(SoundCharacterSettings::Find("ay_room")->isBool);
}

TEST_F(SoundCharacterSettings_Test, DefaultsReadBack)
{
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "ay_voicing"), "classic");
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "ay_punch"), "on");
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "ay_room"), "9db");
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "beeper_punch"), "off");
}

TEST_F(SoundCharacterSettings_Test, SetRoundTrips)
{
    std::string error;
    EXPECT_TRUE(SoundCharacterSettings::Set(*_sound, "ay_voicing", "flat", error)) << error;
    EXPECT_EQ(_sound->getAYVoicing(), FilterVoicing::Preset::Flat);
    EXPECT_TRUE(SoundCharacterSettings::Set(*_sound, "ay_voicing", "legacy", error)) << error;
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "ay_voicing"), "classic");

    EXPECT_TRUE(SoundCharacterSettings::Set(*_sound, "ay_punch", "false", error)) << error;
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "ay_punch"), "off");
    EXPECT_TRUE(SoundCharacterSettings::Set(*_sound, "beeper_punch", "1", error)) << error;
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "beeper_punch"), "on");

    EXPECT_TRUE(SoundCharacterSettings::Set(*_sound, "ay_room", "-9dB", error)) << error;
    EXPECT_EQ(SoundCharacterSettings::Get(*_sound, "ay_room"), "9db");
    EXPECT_EQ(_sound->getAYRoomMode(), AudioCharacterChain::RoomMode::Room_9dB);
}

TEST_F(SoundCharacterSettings_Test, RejectsBadValues)
{
    std::string error;
    EXPECT_FALSE(SoundCharacterSettings::Set(*_sound, "ay_voicing", "loud", error)) << "unknown profile";
    const std::string allowed = error.substr(error.find("Use "));
    EXPECT_NE(allowed.find("classic"), std::string::npos) << "the error lists the accepted values: " << error;
    EXPECT_NE(allowed.find("tv"), std::string::npos) << "the error lists the accepted values: " << error;

    EXPECT_FALSE(SoundCharacterSettings::Set(*_sound, "ay_punch", "maybe", error));
    EXPECT_FALSE(SoundCharacterSettings::Set(*_sound, "ay_room", "7db", error));
    EXPECT_FALSE(SoundCharacterSettings::Set(*_sound, "unknown", "on", error));

    EXPECT_EQ(_sound->getAYVoicing(), FilterVoicing::Preset::Classic) << "rejected input changes nothing";
    EXPECT_TRUE(_sound->getAYPunch());
}

TEST_F(SoundCharacterSettings_Test, AllowedValuesOnlyVisibleProfiles)
{
    const std::vector<std::string> voicing = SoundCharacterSettings::AllowedValues("ay_voicing");
    EXPECT_EQ(voicing, (std::vector<std::string>{"flat", "classic", "headphones", "warm", "tv", "small_speaker"}));
    EXPECT_EQ(SoundCharacterSettings::AllowedValues("ay_room").size(),
              static_cast<size_t>(AudioCharacterChain::RoomMode::COUNT));
    EXPECT_EQ(SoundCharacterSettings::AllowedValues("ay_punch"), (std::vector<std::string>{"on", "off"}));
}
