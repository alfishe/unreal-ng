// The General Sound command log (gs-debugger design §6.3, F1, F4): the host's traffic decoded by the firmware
// profile, the uploads as one block, the protocol anomalies

#include "emulator/sound/chips/gs/gscommandlog.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/state/statenodejson.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/gs/soundchip_gslw.h"

namespace
{

using Kind = GSCommandLogKind;

constexpr uint16_t kData = GeneralSoundCard::PORT_DATA;
constexpr uint16_t kCommand = GeneralSoundCard::PORT_COMMAND;
constexpr uint16_t kControl = GeneralSoundCard::PORT_CONTROL;

template <typename Chip>
struct LoggedCard
{
    EmulatorContext ctx{LoggerLevel::LogError};
    std::unique_ptr<Chip> chip;
    GSCommandLog log{&ctx, nullptr, "gs.commands"};

    LoggedCard()
    {
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<Chip>(&ctx, 128, 44100);
        chip->attachCommandLog(&log);
    }
    ~LoggedCard() { chip->attachCommandLog(nullptr); }

    void start()
    {
        std::string error;
        ASSERT_TRUE(log.History().Configure({.mode = history::Mode::Ring}, error)) << error;
    }
    void frame()
    {
        ctx.emulatorState.frame_counter++;
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
    }
    std::vector<GSCommandLogEntry> entries() const { return log.History().Snapshot(); }
    /// The entries of a kind, oldest first
    std::vector<GSCommandLogEntry> of(Kind kind) const
    {
        std::vector<GSCommandLogEntry> out;
        for (const GSCommandLogEntry& e : entries())
            if (e.kind == kind)
                out.push_back(e);
        return out;
    }
    std::string dump() const
    {
        std::string text;
        for (const GSCommandLogEntry& e : entries())
            text += e.Text() + "\n";
        return text;
    }
};

using LwCard = LoggedCard<SoundChip_GSLightweight>;
using LleCard = LoggedCard<SoundChip_GeneralSound>;

} // namespace

/// Off, nothing is kept; on, the commands are named, the pre byte and the parameters after them decoded, the replies
/// carry what they mean, and the card's taking of a command is seen at the next host access
TEST(GSCommandLog_Test, DecodesCommandsParamsAndReplies)
{
    LwCard card;
    card.chip->portDeviceOutMethod(kCommand, 0x23);
    EXPECT_TRUE(card.entries().empty()) << "off: nothing";

    card.start();
    card.chip->portDeviceOutMethod(kData, 0x05);     // Set current module: pre the handle
    card.chip->portDeviceOutMethod(kCommand, 0x2C);
    (void)card.chip->portDeviceInMethod(kData);      // reply: the old current module
    card.chip->portDeviceOutMethod(kData, 0x34);     // Set current track: pre low, post high
    card.chip->portDeviceOutMethod(kCommand, 0x2F);
    card.chip->portDeviceOutMethod(kData, 0x12);
    card.chip->portDeviceOutMethod(kCommand, 0x23);  // Get number of RAM pages: reply 1
    (void)card.chip->portDeviceInMethod(kData);

    EXPECT_EQ(card.log.Profile(), GSFirmwareProfiles()[1]) << "the lightweight card answers as gs105a";
    const auto commands = card.of(Kind::Command);
    ASSERT_EQ(commands.size(), 3u) << card.dump();
    EXPECT_EQ(commands[0].Text(), "#2C Set current module, module #05");
    EXPECT_EQ(commands[1].Text(), "#2F Set current track, track #34");
    EXPECT_EQ(commands[2].Text(), "#23 Get number of RAM pages");

    const auto params = card.of(Kind::Param);
    ASSERT_EQ(params.size(), 1u) << card.dump();
    EXPECT_EQ(params[0].command, 0x2F);
    EXPECT_EQ(params[0].index, 1);
    EXPECT_EQ(params[0].value, 0x12);

    const auto replies = card.of(Kind::Reply);
    ASSERT_EQ(replies.size(), 2u) << card.dump();
    EXPECT_EQ(replies[0].command, 0x2C);
    EXPECT_FALSE(replies[0].flags & GSCommandLogFlags::kUnasked);
    EXPECT_NE(replies[1].Text().find("pages"), std::string::npos) << replies[1].Text();
    EXPECT_GE(card.of(Kind::CardTook).size(), 2u) << card.dump();
    EXPECT_TRUE(card.of(Kind::Anomaly).empty()) << card.dump();

    StateNode node;
    commands[0].ToState(node);
    EXPECT_NE(StateNodeToJsonText(node).find("\"profile\":\"gs105a\""), std::string::npos)
        << StateNodeToJsonText(node);
}

/// A module upload is one block entry, whatever its length; LOAD's stream commands are named; an upload that stops
/// is reported once
TEST(GSCommandLog_Test, AnUploadIsOneBlock)
{
    LwCard card;
    card.start();
    card.chip->portDeviceOutMethod(kCommand, 0x30);
    (void)card.chip->portDeviceInMethod(kData);  // the module handle
    for (int i = 0; i < 300; i++)
        card.chip->portDeviceOutMethod(kData, static_cast<uint8_t>(i));
    for (uint32_t i = 0; i < GSCommandLog::kStallFrames + 5; i++)
        card.frame();
    card.chip->portDeviceOutMethod(kCommand, 0xD2);

    const auto blocks = card.of(Kind::Block);
    ASSERT_EQ(blocks.size(), 1u) << card.dump();
    EXPECT_EQ(blocks[0].command, 0x30);
    EXPECT_EQ(blocks[0].count, 300u);
    EXPECT_NE(blocks[0].Text().find("module upload: 300 bytes"), std::string::npos) << blocks[0].Text();
    EXPECT_TRUE(card.of(Kind::Data).empty() && card.of(Kind::Param).empty()) << "the bytes are only counted";

    const auto commands = card.of(Kind::Command);
    ASSERT_EQ(commands.size(), 2u) << card.dump();
    EXPECT_TRUE(commands[1].flags & GSCommandLogFlags::kStream);
    EXPECT_EQ(commands[1].Text(), "#D2 Close stream");

    const auto anomalies = card.of(Kind::Anomaly);
    ASSERT_EQ(anomalies.size(), 1u) << card.dump();
    EXPECT_EQ(anomalies[0].value, uint8_t(GSCommandLogAnomaly::TransferStalled));
    EXPECT_EQ(anomalies[0].count, 300u);
}

/// A block upload (#14) ends at its length, at its address in the card's window
TEST(GSCommandLog_Test, ABlockUploadEndsAtItsLength)
{
    LwCard card;
    card.start();
    card.chip->portDeviceOutMethod(kData, 0x04);  // length low
    card.chip->portDeviceOutMethod(kCommand, 0x14);
    for (uint8_t b : {0x00, 0x00, 0x80})         // length high, address low, high
        card.chip->portDeviceOutMethod(kData, b);
    for (int i = 0; i < 4; i++)
        card.chip->portDeviceOutMethod(kData, 0xAA);
    card.chip->portDeviceOutMethod(kData, 0x07);  // after the block: a plain data byte

    const auto blocks = card.of(Kind::Block);
    ASSERT_EQ(blocks.size(), 1u) << card.dump();
    EXPECT_EQ(blocks[0].count, 4u);
    EXPECT_EQ(blocks[0].dest, GSCommandLogDest::Ram);
    EXPECT_EQ(blocks[0].destAddr, 0x8000);
    EXPECT_FALSE(blocks[0].flags & GSCommandLogFlags::kTruncated);
    EXPECT_EQ(card.of(Kind::Param).size(), 3u) << card.dump();
    const auto data = card.of(Kind::Data);
    ASSERT_EQ(data.size(), 2u) << "the length's pre byte and the byte after the block: " << card.dump();
    EXPECT_EQ(data[1].value, 0x07);
}

/// The firmware's card (a real Z80 that has not run): what the host does without waiting for the card is an anomaly
TEST(GSCommandLog_Test, TheHostNotWaitingIsAnAnomaly)
{
    LleCard card;
    card.chip->loadROM("rom/gs105a.rom");
    card.start();
    card.chip->portDeviceOutMethod(kData, 0x01);
    card.chip->portDeviceOutMethod(kData, 0x02);   // the first byte not taken
    (void)card.chip->portDeviceInMethod(kData);     // clears the data flag: a read nothing asked for
    card.chip->portDeviceOutMethod(kCommand, 0x23);
    card.chip->portDeviceOutMethod(kCommand, 0x20); // #23 not taken
    (void)card.chip->portDeviceInMethod(kData);     // #20's reply not written yet

    EXPECT_EQ(card.log.Profile(), GSFirmwareProfiles()[1]);
    const auto anomalies = card.of(Kind::Anomaly);
    ASSERT_EQ(anomalies.size(), 3u) << card.dump();
    EXPECT_EQ(anomalies[0].value, uint8_t(GSCommandLogAnomaly::DataOverwritten));
    EXPECT_EQ(anomalies[0].count, 0x01u);
    EXPECT_EQ(anomalies[1].value, uint8_t(GSCommandLogAnomaly::CommandOverwritten));
    EXPECT_EQ(anomalies[1].count, 0x23u);
    EXPECT_EQ(anomalies[2].value, uint8_t(GSCommandLogAnomaly::ReplyNotReady));
    const auto replies = card.of(Kind::Reply);
    ASSERT_EQ(replies.size(), 2u) << card.dump();
    EXPECT_TRUE(replies[0].flags & GSCommandLogFlags::kUnasked);
    EXPECT_TRUE(replies[1].flags & GSCommandLogFlags::kNotReady);
    EXPECT_EQ(replies[1].command, 0x20);
}

/// A card reset ends what was going on; a log switched off and on starts over
TEST(GSCommandLog_Test, AResetAndARestartStartOver)
{
    LwCard card;
    card.start();
    card.chip->portDeviceOutMethod(kCommand, 0x30);
    card.chip->portDeviceOutMethod(kData, 0x11);
    card.chip->portDeviceOutMethod(kControl, 0x80);

    const auto blocks = card.of(Kind::Block);
    ASSERT_EQ(blocks.size(), 1u) << card.dump();
    EXPECT_TRUE(blocks[0].flags & GSCommandLogFlags::kTruncated);
    EXPECT_EQ(card.of(Kind::Marker).size(), 1u);

    std::string error;
    ASSERT_TRUE(card.log.History().Configure({.mode = history::Mode::Off}, error));
    card.chip->portDeviceOutMethod(kCommand, 0x30);  // the upload the log does not see
    card.start();
    card.chip->portDeviceOutMethod(kData, 0x09);
    card.chip->portDeviceOutMethod(kCommand, 0x31);
    const auto commands = card.of(Kind::Command);
    ASSERT_EQ(commands.size(), 1u) << card.dump();
    EXPECT_EQ(commands[0].Text(), "#31 Play module, module #09") << "decoded afresh, not as upload bytes";

    // Off and on again with no host access between: still afresh (#2F's parameter is not awaited any more)
    card.chip->portDeviceOutMethod(kData, 0x01);
    card.chip->portDeviceOutMethod(kCommand, 0x2F);
    ASSERT_TRUE(card.log.History().Configure({.mode = history::Mode::Off}, error));
    card.start();
    card.chip->portDeviceOutMethod(kData, 0x02);
    EXPECT_TRUE(card.of(Kind::Param).empty()) << card.dump();
    EXPECT_EQ(card.of(Kind::Data).size(), 1u) << card.dump();
}
