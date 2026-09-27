#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "common/sound/filters/audio_character_chain.h"

/// AudioCharacterChain room-mode IDs: the stable text form used by settings,
/// automation and frontend persistence. Every mode round-trips; the parser
/// also takes the dB forms people type ("-15dB", "15").

using RoomMode = AudioCharacterChain::RoomMode;

TEST(AudioCharacterChain_Test, RoomModeIdsRoundTrip)
{
    for (int i = 0; i < static_cast<int>(RoomMode::COUNT); i++)
    {
        const auto mode = static_cast<RoomMode>(i);
        RoomMode parsed = RoomMode::COUNT;
        ASSERT_TRUE(AudioCharacterChain::parseRoomMode(AudioCharacterChain::roomModeId(mode), parsed))
            << AudioCharacterChain::roomModeId(mode);
        EXPECT_EQ(parsed, mode);
    }
}

TEST(AudioCharacterChain_Test, RoomModeParseAcceptsDbForms)
{
    RoomMode mode = RoomMode::Off;
    EXPECT_TRUE(AudioCharacterChain::parseRoomMode("-15dB", mode));
    EXPECT_EQ(mode, RoomMode::Room_15dB);
    EXPECT_TRUE(AudioCharacterChain::parseRoomMode("9", mode));
    EXPECT_EQ(mode, RoomMode::Room_9dB);
    EXPECT_TRUE(AudioCharacterChain::parseRoomMode("OFF", mode));
    EXPECT_EQ(mode, RoomMode::Off);

    mode = RoomMode::Room_6dB;
    EXPECT_FALSE(AudioCharacterChain::parseRoomMode("7db", mode));
    EXPECT_FALSE(AudioCharacterChain::parseRoomMode("", mode));
    EXPECT_FALSE(AudioCharacterChain::parseRoomMode("loud", mode));
    EXPECT_EQ(mode, RoomMode::Room_6dB) << "a failed parse must not touch the output";
}
