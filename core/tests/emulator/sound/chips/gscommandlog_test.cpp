// The General Sound command log (gs-debugger design §6.3, F1, F4): the host's traffic decoded by the firmware
// profile, the uploads as one block, the protocol anomalies

#include "emulator/sound/chips/gs/gscommandlog.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "_helpers/ttdcardbench.h"
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
    (void)card.chip->portDeviceInMethod(kCommand);   // the host polls: the reply is there
    (void)card.chip->portDeviceInMethod(kData);      // reply: the old current module
    card.chip->portDeviceOutMethod(kData, 0x34);     // Set current track: pre low, post high
    card.chip->portDeviceOutMethod(kCommand, 0x2F);
    card.chip->portDeviceOutMethod(kData, 0x12);
    card.chip->portDeviceOutMethod(kCommand, 0x23);  // Get number of RAM pages: reply 1
    (void)card.chip->portDeviceInMethod(kCommand);
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
    (void)card.chip->portDeviceInMethod(kCommand);
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
    ASSERT_EQ(anomalies.size(), 4u) << card.dump();
    EXPECT_EQ(anomalies[0].value, uint8_t(GSCommandLogAnomaly::DataOverwritten));
    EXPECT_EQ(anomalies[0].count, 0x01u);
    EXPECT_EQ(anomalies[1].value, uint8_t(GSCommandLogAnomaly::DataBeforeCommand)) << "#02 is still latched; #23 takes none";
    EXPECT_EQ(anomalies[1].count, 0x02u);
    EXPECT_EQ(anomalies[2].value, uint8_t(GSCommandLogAnomaly::CommandOverwritten));
    EXPECT_EQ(anomalies[2].count, 0x23u);
    EXPECT_EQ(anomalies[3].value, uint8_t(GSCommandLogAnomaly::ReplyNotReady));
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

namespace
{
/// The anomalies of the log, by kind name, in order
std::vector<std::string> AnomalyNames(const std::vector<GSCommandLogEntry>& anomalies)
{
    std::vector<std::string> names;
    for (const GSCommandLogEntry& e : anomalies)
        names.push_back(GSCommandLogAnomalyName(static_cast<GSCommandLogAnomaly>(e.value)));
    return names;
}
} // namespace

/// F4 revision 4: a due reply read with no status poll that saw it is flagged even when the byte was right (the
/// NedoOS kernel's GS_INIT reads #23's reply after two HALTs); the host's poll keeps it out
TEST(GSCommandLog_Test, AReplyReadWithoutAPollIsFlagged)
{
    LwCard card;
    card.start();
    card.chip->portDeviceOutMethod(kCommand, 0x23);
    (void)card.chip->portDeviceInMethod(kData);     // the lightweight card answered at once: valid, unpolled
    card.chip->portDeviceOutMethod(kCommand, 0x23);
    (void)card.chip->portDeviceInMethod(kCommand);  // polled
    (void)card.chip->portDeviceInMethod(kData);
    (void)card.chip->readData();                     // a tool's read polls for itself

    const auto anomalies = card.of(Kind::Anomaly);
    ASSERT_EQ(anomalies.size(), 1u) << card.dump();
    EXPECT_EQ(anomalies[0].value, uint8_t(GSCommandLogAnomaly::ReplyReadWithoutPoll));
    EXPECT_EQ(anomalies[0].command, 0x23);
    EXPECT_NE(anomalies[0].Text().find("reply_read_without_poll"), std::string::npos) << anomalies[0].Text();
}

/// A command inside an open stream that the firmware does not run there (#F3 restarts the upload, others are only
/// acknowledged); a data byte before a command that takes none is a note; reads that drain an unasked byte once a run
TEST(GSCommandLog_Test, BusyCommandsNotesAndDrainsAreFlagged)
{
    LwCard card;
    card.start();
    card.chip->portDeviceOutMethod(kCommand, 0x30);  // a module upload (LOAD)
    (void)card.chip->portDeviceInMethod(kCommand);
    (void)card.chip->portDeviceInMethod(kData);
    for (int i = 0; i < 4; i++)
        card.chip->portDeviceOutMethod(kData, static_cast<uint8_t>(i));
    card.chip->portDeviceOutMethod(kCommand, 0xD1);  // a stream command: fine
    card.chip->portDeviceOutMethod(kCommand, 0x2A);  // not run while loading
    card.chip->portDeviceOutMethod(kCommand, 0xF3);  // restarts the upload
    card.frame();
    card.chip->portDeviceOutMethod(kData, 0x07);
    card.chip->portDeviceOutMethod(kCommand, 0x23);  // takes no pre byte
    card.chip->portDeviceInMethod(kCommand);
    (void)card.chip->portDeviceInMethod(kData);      // the reply
    (void)card.chip->portDeviceInMethod(kData);      // nothing asked, the flag clear: no drain

    const std::vector<std::string> names = AnomalyNames(card.of(Kind::Anomaly));
    const std::vector<std::string> expected = {"command_while_busy", "command_while_busy", "data_before_command"};
    EXPECT_EQ(names, expected) << card.dump();
}

/// The profile's handshake (M6) and modes, through a profile the host's code selects (M5): a command the firmware
/// acknowledges by the data flag is not "overwritten", a barrier starts over, a mode accepts only its commands, a
/// handler waiting for its reply to be read stalls, unsolicited bytes are no drain; a card reset returns to the ROM's
TEST(GSCommandLog_Test, TheProfileHandshakeKeepsLegalCasesOut)
{
    const std::string json = R"({
      "id": "handshake-test", "unsolicitedReplies": true,
      "commands": [ { "number": "20", "name": "note", "handshake": { "ack": "data" } },
                    { "number": "E1", "name": "play" }, { "number": "E4", "name": "volume" },
                    { "number": "E2", "name": "stop" }, { "number": "E5", "name": "seek" },
                    { "number": "33", "name": "query", "reply": 1, "handshake": { "replyWait": true } } ],
      "modes": [ { "name": "playing", "enter": ["E1"], "leave": ["E2"], "accepts": ["E4"] } ]
    })";
    std::string error;
    const std::unique_ptr<GSFirmwareProfile> profile = ParseGSFirmwareProfile(json, "", error);
    ASSERT_NE(profile, nullptr) << error;

    LleCard card;  // a Z80 that does not run: the command flag stays set as on NeoTracker for #20-#DF
    card.chip->loadROM("rom/gs105a.rom");
    card.start();
    card.log.PushProfile(profile.get());
    card.chip->portDeviceOutMethod(kCommand, 0x20);
    card.chip->portDeviceOutMethod(kCommand, 0xE1);  // after an ack-by-data command: legal
    card.chip->portDeviceOutMethod(kCommand, 0xE4);
    card.chip->portDeviceOutMethod(kCommand, 0xE5);  // not accepted while playing
    card.chip->portDeviceOutMethod(kCommand, 0xE2);
    card.chip->portDeviceOutMethod(kCommand, 0xE5);  // stopped: accepted

    const std::vector<std::string> names = AnomalyNames(card.of(Kind::Anomaly));
    const std::vector<std::string> expected = {"command_overwritten", "command_overwritten", "command_rejected_in_state",
                                         "command_overwritten", "command_overwritten"};
    EXPECT_EQ(names, expected) << "#E1 after #20 is no overwrite; the rest are (the card never runs)\n"
                               << card.dump();

    card.chip->portDeviceOutMethod(kControl, 0x80);  // card reset: the ROM's profile again
    EXPECT_EQ(card.log.Profile(), GSFirmwareProfiles()[1]);
}

/// A handler that spins until its reply is read (replyWait) and a host that never reads it
TEST(GSCommandLog_Test, ACardWaitingForItsReplyToBeReadStalls)
{
    const std::string json = R"({ "id": "reply-wait-test",
      "commands": [ { "number": "23", "name": "pages", "reply": 1, "handshake": { "replyWait": true } } ] })";
    std::string error;
    const std::unique_ptr<GSFirmwareProfile> profile = ParseGSFirmwareProfile(json, "", error);
    ASSERT_NE(profile, nullptr) << error;
    LwCard card;
    card.start();
    card.log.PushProfile(profile.get());
    card.chip->portDeviceOutMethod(kCommand, 0x23);  // the lightweight card writes the reply at once
    for (uint32_t i = 0; i < GSCommandLog::kStallFrames + 3; i++)
        card.frame();
    const auto anomalies = card.of(Kind::Anomaly);
    ASSERT_EQ(anomalies.size(), 1u) << card.dump();
    EXPECT_EQ(anomalies[0].value, uint8_t(GSCommandLogAnomaly::CardReplyStall));
    EXPECT_GE(anomalies[0].count, GSCommandLog::kStallFrames);
}

/// Code that takes data outside its commands as a stream (hostDataStream, GP's MP3 streamer): the bytes count into one
/// block across the code's polls, no Data entries and no pre bytes; the barrier ends it, and the protocol state shows it
TEST(GSCommandLog_Test, AHostDataStreamIsOneBlockAcrossThePolls)
{
    const std::string json = R"({ "id": "host-stream-test", "hostDataStream": true,
      "commands": [ { "number": "00", "name": "reset", "handshake": { "barrier": true } },
                    { "number": "01", "name": "free space", "reply": 1 } ] })";
    std::string error;
    const std::unique_ptr<GSFirmwareProfile> profile = ParseGSFirmwareProfile(json, "", error);
    ASSERT_NE(profile, nullptr) << error;
    EXPECT_TRUE(profile->hostDataStream);
    LwCard card;
    card.start();
    card.log.PushProfile(profile.get());
    for (uint8_t b = 0; b < 5; b++)
        card.chip->portDeviceOutMethod(kData, b);
    card.chip->portDeviceOutMethod(kCommand, 0x01);  // a poll inside the stream (the code that would answer it does
                                                     // not run on the lightweight card: no reply read)
    for (uint8_t b = 0; b < 3; b++)
        card.chip->portDeviceOutMethod(kData, b);
    const GSProtocolState state = card.log.State();
    EXPECT_TRUE(state.inTransfer && state.hostData) << state.Text();
    EXPECT_EQ(state.transferDone, 8u) << state.Text();
    EXPECT_NE(state.Text().find("(host data)"), std::string::npos) << state.Text();
    card.chip->portDeviceOutMethod(kCommand, 0x00);  // the barrier ends it

    EXPECT_TRUE(card.of(Kind::Data).empty()) << card.dump();
    EXPECT_TRUE(card.of(Kind::Anomaly).empty()) << "no data_before_command for the stream's bytes\n" << card.dump();
    const auto blocks = card.of(Kind::Block);
    ASSERT_EQ(blocks.size(), 1u) << card.dump();
    EXPECT_EQ(blocks[0].count, 8u);
    EXPECT_EQ(blocks[0].hostData, 1);
    EXPECT_EQ(blocks[0].Text(), "stream: 8 bytes of host data");
    EXPECT_FALSE(card.log.State().inTransfer);
}

/// A fixed block (block256: NeoTracker's #E8 header) ends at its length; a page block (block_pages: #EA) at its pre
/// byte's pages of 256 bytes: the next byte is the host's own again
TEST(GSCommandLog_Test, FixedAndPageBlocksEndAtTheirLength)
{
    const std::string json = R"({ "id": "blocks-test",
      "commands": [ { "number": "E8", "name": "start load", "pre": 1, "params": [ { "name": "slot", "kind": "byte" } ],
                      "transfer": "block256" },
                    { "number": "EA", "name": "load block", "pre": 1, "params": [ { "name": "pages", "kind": "byte" } ],
                      "transfer": "block_pages" } ] })";
    std::string error;
    const std::unique_ptr<GSFirmwareProfile> profile = ParseGSFirmwareProfile(json, "", error);
    ASSERT_NE(profile, nullptr) << error;
    EXPECT_EQ(profile->Command(0xEA).transfer, GSTransferKind::PagedBlock);
    LwCard card;
    card.start();
    card.log.PushProfile(profile.get());
    card.chip->portDeviceOutMethod(kData, 0x00);
    card.chip->portDeviceOutMethod(kCommand, 0xE8);
    for (int i = 0; i < 256; i++)
        card.chip->portDeviceOutMethod(kData, 0x55);
    card.chip->portDeviceOutMethod(kData, 0x02);  // the next command's pre byte, not the header's
    card.chip->portDeviceOutMethod(kCommand, 0xEA);
    for (int i = 0; i < 512; i++)
        card.chip->portDeviceOutMethod(kData, 0xAA);

    const auto blocks = card.of(Kind::Block);
    ASSERT_EQ(blocks.size(), 2u) << card.dump();
    EXPECT_EQ(blocks[0].count, 256u);
    EXPECT_EQ(blocks[1].count, 512u);
    EXPECT_FALSE(blocks[1].flags & GSCommandLogFlags::kTruncated);
    const auto commands = card.of(Kind::Command);
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[1].value, 0x02) << "#EA's pre byte";
    EXPECT_EQ(card.of(Kind::Data).size(), 2u) << "the two pre bytes only\n" << card.dump();
}

/// A sector transfer (M12, the NeoGS SD driver's #1E): the sub-command selects read or write, the last parameter
/// counts the 512-byte sectors, a #BB strobe opens each (not a command), and the status byte after them is the reply
TEST(GSCommandLog_Test, SectorTransfersCarryTheirStrobesAndStatus)
{
    const std::string json = R"({ "id": "sectors-test",
      "commands": [ { "number": "1E", "name": "SD driver", "pre": 1, "reply": 1,
                      "params": [ { "name": "sub-command" }, { "name": "LBA 31-24" }, { "name": "LBA 23-16" },
                                  { "name": "LBA 15-8" }, { "name": "LBA 7-0" }, { "name": "sectors" } ],
                      "transfer": "sectors", "handshake": { "strobe": true },
                      "sectors": { "length": 512, "count": 5, "select": 0, "read": 2, "write": 3 } } ] })";
    std::string error;
    const std::unique_ptr<GSFirmwareProfile> profile = ParseGSFirmwareProfile(json, "", error);
    ASSERT_NE(profile, nullptr) << error;
    EXPECT_EQ(profile->Command(0x1E).transfer, GSTransferKind::Sectors);
    EXPECT_EQ(profile->Command(0x1E).post, 5);
    LwCard card;
    card.start();
    card.log.PushProfile(profile.get());
    const auto send = [&](uint8_t sub, uint8_t count) {
        card.chip->portDeviceOutMethod(kData, sub);
        card.chip->portDeviceOutMethod(kCommand, 0x1E);
        for (const uint8_t b : {uint8_t(0x00), uint8_t(0x00), uint8_t(0x01), uint8_t(0x47), count})
            card.chip->portDeviceOutMethod(kData, b);
    };
    send(2, 2);  // read 2 sectors
    for (int sector = 0; sector < 2; sector++)
    {
        card.chip->portDeviceOutMethod(kCommand, 0xFC);  // the strobe
        for (int i = 0; i < 512; i++)
            (void)card.chip->portDeviceInMethod(kData);
    }
    (void)card.chip->portDeviceInMethod(kCommand);
    (void)card.chip->portDeviceInMethod(kData);  // the status
    send(3, 1);  // write 1 sector
    card.chip->portDeviceOutMethod(kCommand, 0xFC);
    for (int i = 0; i < 512; i++)
        card.chip->portDeviceOutMethod(kData, 0xE5);
    (void)card.chip->portDeviceInMethod(kCommand);
    (void)card.chip->portDeviceInMethod(kData);
    send(0, 0);  // init: no sectors, the status only
    EXPECT_FALSE(card.log.State().inTransfer) << card.dump();

    EXPECT_EQ(card.of(Kind::Command).size(), 3u) << "the strobes are no commands\n" << card.dump();
    const auto blocks = card.of(Kind::Block);
    ASSERT_EQ(blocks.size(), 2u) << card.dump();
    EXPECT_EQ(blocks[0].count, 1024u);
    EXPECT_EQ(blocks[0].value, 1) << "a read";
    EXPECT_EQ(blocks[1].count, 512u);
    EXPECT_EQ(blocks[1].value, 2) << "a write";
    const auto params = card.of(Kind::Param);
    ASSERT_EQ(params.size(), 15u) << card.dump();
    EXPECT_EQ(params[4].index, 5) << "the count";
    EXPECT_EQ(params[4].value, 0x02);
    const auto replies = card.of(Kind::Reply);
    ASSERT_EQ(replies.size(), 2u) << "the two status bytes\n" << card.dump();
    for (const GSCommandLogEntry& reply : replies)
        EXPECT_FALSE(reply.flags & GSCommandLogFlags::kUnasked) << card.dump();
    EXPECT_EQ(card.of(Kind::Data).size(), 3u) << "the pre bytes only\n" << card.dump();
}

namespace
{
std::vector<uint8_t> CardCode(const char* name)
{
    std::ifstream in(TestPathHelper::FindProjectRoot() / "testdata/software/nedoos/card-code" / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The host's #14: length low as the pre byte, length high, address low, high, the bytes
template <typename Card>
void Upload(Card& card, uint16_t address, const std::vector<uint8_t>& bytes)
{
    const auto length = static_cast<uint16_t>(bytes.size());
    card.chip->portDeviceOutMethod(kData, static_cast<uint8_t>(length));
    card.chip->portDeviceOutMethod(kCommand, 0x14);
    for (uint8_t b : {uint8_t(length >> 8), uint8_t(address), uint8_t(address >> 8)})
        card.chip->portDeviceOutMethod(kData, b);
    for (uint8_t b : bytes)
        card.chip->portDeviceOutMethod(kData, b);
}

template <typename Card>
void Jump(Card& card, uint16_t address)
{
    card.chip->portDeviceOutMethod(kData, static_cast<uint8_t>(address));
    card.chip->portDeviceOutMethod(kCommand, 0x13);
    card.chip->portDeviceOutMethod(kData, static_cast<uint8_t>(address >> 8));
}
} // namespace

/// The upload profiles of data/symbols/gs/profiles (M5) load after the built-ins, each found by its upload's digest
TEST(GSCommandLog_Test, TheNedoOsUploadProfilesLoad)
{
    for (const char* id : {"nedoos-gp-gscode", "nedoos-ngsldr", "nedoos-neotracker", "nedoos-neopg2", "nedoos-ngssd"})
    {
        SCOPED_TRACE(id);
        const GSFirmwareProfile* profile = FindGSFirmwareProfileById(id);
        ASSERT_NE(profile, nullptr) << "a broken profile file is skipped with a warning";
        EXPECT_EQ(profile->source.rfind(".json"), profile->source.size() - 5) << profile->source;
        ASSERT_FALSE(profile->matches.empty());
        EXPECT_EQ(profile->matches[0].kind, GSMatchKind::Upload);
    }
    const GSFirmwareProfile* neo = FindGSFirmwareProfileById("nedoos-neotracker");
    EXPECT_TRUE(neo->unsolicitedReplies);
    ASSERT_EQ(neo->modes.size(), 1u);
    EXPECT_EQ(neo->dispatch.table, 0x2E80) << "COMTAB through the profile's symbol file";
    EXPECT_EQ(neo->dispatch.unknownStub, 0x2600);
    ASSERT_EQ(neo->instances.size(), 1u);
    EXPECT_EQ(neo->instances[0].address, 0x4F00);
    const GSFirmwareProfile* gp = FindGSFirmwareProfileById("nedoos-gp-gscode");
    EXPECT_TRUE(gp->Command(0x00).handshake.exit);
    ASSERT_EQ(gp->objectVariables.size(), 4u);
    EXPECT_EQ(gp->objectVariables[3].address, 0x43C1);
}

/// A #14 block is hashed: a known one names its profile, #13 into it decodes by that profile from then on, a card
/// reset returns to the ROM's. The kernel's SD driver extends the stock firmware (its #1E, the rest the ROM's), sent
/// padded to 768 bytes (the match covers its 686); ngsplay's loader replaces it
TEST(GSCommandLog_Test, AnUploadStartedByJumpSelectsItsProfile)
{
    LwCard card;
    card.start();
    std::vector<uint8_t> sd = CardCode("ngssd.bin");
    ASSERT_EQ(sd.size(), 686u);
    sd.resize(768, 0x55);
    Upload(card, 0x5B00, sd);
    Upload(card, 0x8000, {1, 2, 3});  // unknown code
    std::vector<GSUploadRecord> uploads = card.log.Uploads();
    ASSERT_EQ(uploads.size(), 2u) << card.dump();
    ASSERT_NE(uploads[0].profile, nullptr);
    EXPECT_STREQ(uploads[0].profile->id, "nedoos-ngssd");
    EXPECT_EQ(uploads[0].matchedLength, 686u);
    EXPECT_EQ(uploads[0].length, 768u);
    EXPECT_EQ(uploads[1].profile, nullptr) << "an unknown upload is recorded with its digest";
    EXPECT_EQ(uploads[1].sha256.size(), 64u);

    Jump(card, 0x5B00);
    EXPECT_STREQ(card.log.Profile()->id, "nedoos-ngssd");
    EXPECT_TRUE(card.log.Uploads()[0].started);
    card.chip->portDeviceOutMethod(kData, 0x02);     // sub-command: read
    card.chip->portDeviceOutMethod(kCommand, 0x1E);
    card.chip->portDeviceOutMethod(kCommand, 0x23);  // the ROM's command, below the driver
    const auto commands = card.of(Kind::Command);
    ASSERT_GE(commands.size(), 2u);
    EXPECT_EQ(commands[commands.size() - 2].Text().rfind("#1E SD driver (GSDDRV)", 0), 0u) << card.dump();
    EXPECT_EQ(commands.back().Text(), "#23 Get number of RAM pages") << card.dump();

    card.chip->portDeviceOutMethod(kControl, 0x80);
    EXPECT_EQ(card.log.Profile(), GSFirmwareProfiles()[1]) << "a reset returns to the ROM's";

    Upload(card, 0x5100, CardCode("ngsldr.bin"));
    Jump(card, 0x5100);
    EXPECT_STREQ(card.log.Profile()->id, "nedoos-ngsldr");
    card.chip->portDeviceOutMethod(kCommand, 0x23);
    EXPECT_EQ(card.of(Kind::Command).back().Text(), "#23") << "the loader replaces the firmware: #23 is unknown";
    EXPECT_EQ(card.log.UnknownCommands()[0x23], 1u) << "AU6: the unknown command counted";

    StateNode node;
    card.log.State().ToState(node);
    EXPECT_NE(StateNodeToJsonText(node).find("\"profile\":\"nedoos-ngsldr\""), std::string::npos)
        << StateNodeToJsonText(node);
}

/// The uploads and the profile stack follow time travel (D3): a move back shows the blocks up to the machine's frame
/// and the decoder's profile there; a replay of the frames does not record a block twice, also one that a frame start
/// cut in two; a live run from the past drops the blocks of the line it left. The follower called as the controller
/// calls it (Restored at the checkpoint, the replay with the hooks, Landed)
TEST(GSCommandLog_Test, UploadsAndTheProfileStackFollowTimeTravel)
{
    LwCard card;
    card.start();
    std::vector<uint8_t> sd = CardCode("ngssd.bin");
    ASSERT_EQ(sd.size(), 686u);
    sd.resize(768, 0x55);
    const auto send = [&](size_t from, size_t to) {
        for (size_t i = from; i < to; i++)
            card.chip->portDeviceOutMethod(kData, sd[i]);
    };
    const auto begin = [&] {
        card.chip->portDeviceOutMethod(kData, 0x00);  // 768: length low, high 3, at #5B00
        card.chip->portDeviceOutMethod(kCommand, 0x14);
        for (uint8_t b : {uint8_t(0x03), uint8_t(0x00), uint8_t(0x5B)})
            card.chip->portDeviceOutMethod(kData, b);
    };
    const auto moveTo = [&](uint64_t frame) {
        card.ctx.emulatorState.frame_counter = frame;
        card.log.Restored();
    };

    card.frame();  // 1: the block begins
    begin();
    send(0, 300);
    card.frame();  // 2: a frame start cuts it (a mark mid-block)
    send(300, sd.size());
    Jump(card, 0x5B00);
    card.frame();  // 3
    ASSERT_STREQ(card.log.Profile()->id, "nedoos-ngssd");
    ASSERT_EQ(card.log.Uploads().size(), 1u);
    const std::string digest = card.log.Uploads()[0].sha256;
    const size_t logged = card.entries().size();

    moveTo(0);
    card.log.Landed(nullptr);
    EXPECT_TRUE(card.log.Uploads().empty()) << "frame 0: no block yet";
    EXPECT_EQ(card.log.Profile(), nullptr);

    moveTo(3);
    card.log.Landed(nullptr);
    ASSERT_EQ(card.log.Uploads().size(), 1u);
    EXPECT_TRUE(card.log.Uploads()[0].started);
    EXPECT_STREQ(card.log.Profile()->id, "nedoos-ngssd") << "the decoder's profile at frame 3";

    // Back into the block: the checkpoint of frame 2, the rest replayed
    moveTo(2);
    card.ctx.ttdReplayActive = true;
    send(300, sd.size());
    Jump(card, 0x5B00);
    card.ctx.ttdReplayActive = false;
    card.log.Landed(nullptr);
    ASSERT_EQ(card.log.Uploads().size(), 1u) << "the replayed block is the one the list holds";
    EXPECT_EQ(card.log.Uploads()[0].sha256, digest) << "the bytes before the cut came back";
    EXPECT_STREQ(card.log.Profile()->id, "nedoos-ngssd");
    EXPECT_EQ(card.entries().size(), logged) << "a replay logs nothing";

    // A live run from frame 0 is a new line: the block of the left line goes
    moveTo(0);
    card.log.Landed(nullptr);
    card.chip->portDeviceOutMethod(kCommand, 0x23);
    moveTo(3);
    card.log.Landed(nullptr);
    EXPECT_TRUE(card.log.Uploads().empty()) << card.dump();
}

/// GP's streamer: its #00 jumps back to the ROM (handshake exit), its replies wait to be read
TEST(GSCommandLog_Test, GpStreamerExitsToTheRom)
{
    LwCard card;
    card.start();
    Upload(card, 0x4000, CardCode("gscode.bin"));
    Jump(card, 0x4000);
    ASSERT_STREQ(card.log.Profile()->id, "nedoos-gp-gscode");
    card.chip->portDeviceOutMethod(kCommand, 0x00);
    EXPECT_EQ(card.log.Profile(), GSFirmwareProfiles()[1]);
    EXPECT_EQ(card.of(Kind::Command).back().Text().rfind("#00 Reset (back to the ROM)", 0), 0u) << card.dump();
}

/// The dispatch table gives each command its handler as the card holds it now (M4 card_pc), read when the host
/// writes the command; the profile's handler label names it (card_label: the kernel's #1E is GSDDRV)
TEST(GSCommandLog_Test, ACommandCarriesTheHandlerTheTableHolds)
{
    LleCard card;
    card.chip->loadROM("rom/gs105a.rom");
    card.start();
    card.chip->portDeviceOutMethod(kCommand, 0x00);
    card.chip->portDeviceOutMethod(kCommand, 0x23);
    card.chip->portDeviceOutMethod(kCommand, 0xF3);
    const auto commands = card.of(Kind::Command);
    ASSERT_EQ(commands.size(), 3u) << card.dump();
    const uint16_t expected[] = {0x0365, 0xC2FD, 0x0679};  // COM00 (COMTAB), COM23 (COMTABH), COMF3 (COMTAB)
    for (size_t i = 0; i < 3; i++)
    {
        ASSERT_TRUE(commands[i].flags & GSCommandLogFlags::kHandler) << i;
        EXPECT_EQ(commands[i].destAddr, expected[i]) << i;
    }
    StateNode node = StateNode::Object();
    commands[1].ToState(node);
    EXPECT_EQ(node.find("card_pc")->i, 0xC2FD);

    LwCard lw;  // no card memory: no handler
    lw.start();
    lw.chip->portDeviceOutMethod(kCommand, 0x23);
    EXPECT_FALSE(lw.of(Kind::Command).back().flags & GSCommandLogFlags::kHandler);

    const GSFirmwareProfile* sd = FindGSFirmwareProfileById("nedoos-ngssd");
    ASSERT_NE(sd, nullptr);
    ASSERT_NE(sd->Command(0x1E).handler, nullptr);
    EXPECT_STREQ(sd->Command(0x1E).handler, "GSDDRV");
}

/// The stock test mode (TEST_H.a80 TCOM): after #FA the commands below #20 pick a test (no parameters, no
/// transfer: #0E is not the LPT Covox stream there), a command from #20 on leaves the mode and is decoded as usual
TEST(GSCommandLog_Test, TheTestModeTakesTestNumbers)
{
    LwCard card;
    card.start();
    for (const uint8_t command : {0xFA, 0x0B, 0x0E, 0x23, 0x0E})
        card.chip->portDeviceOutMethod(kCommand, command);
    const auto commands = card.of(Kind::Command);
    ASSERT_EQ(commands.size(), 5u) << card.dump();
    EXPECT_EQ(commands[0].Text(), "#FA Test mode");
    EXPECT_EQ(commands[1].Text(), "#0B (test selection)");
    EXPECT_EQ(commands[2].Text(), "#0E (test selection)");
    EXPECT_EQ(commands[3].Text(), "#23 Get number of RAM pages") << "#20 on leaves the mode";
    EXPECT_EQ(commands[4].Text().rfind("#0E LPT Covox mode", 0), 0u) << "out of the mode: the command again";
    for (const GSCommandLogEntry& e : card.of(Kind::CardTook))
        if (e.command == 0x0B)
            EXPECT_EQ(e.Text(), "card took #0B (test selection)");
}

/// region <Time travel (D3)>

namespace
{

/// The host talks to the card all the time (interrupts off): a pre byte, #2F, its post byte, #23 and its reply, a
/// delay after each command
void WriteTalker(ttdtest::Bench& b)
{
    static const uint8_t program[] = {
        0xF3,              // 7FFF DI
        0x06, 0x00,        // 8000 LD B,0
        0x04,              // 8002 INC B
        0x78,              // 8003 LD A,B
        0xD3, 0xB3,        // 8004 OUT (#B3),A    the pre byte
        0x3E, 0x2F,        // 8006 LD A,#2F
        0xD3, 0xBB,        // 8008 OUT (#BB),A    Set current track
        0x0E, 0x40,        // 800A LD C,#40
        0x0D,              // 800C DEC C
        0x20, 0xFD,        // 800D JR NZ,#800C
        0x78,              // 800F LD A,B
        0xD3, 0xB3,        // 8010 OUT (#B3),A    its post byte
        0x3E, 0x23,        // 8012 LD A,#23
        0xD3, 0xBB,        // 8014 OUT (#BB),A    Get number of RAM pages
        0x0E, 0x40,        // 8016 LD C,#40
        0x0D,              // 8018 DEC C
        0x20, 0xFD,        // 8019 JR NZ,#8018
        0xDB, 0xB3,        // 801B IN A,(#B3)     its reply
        0x18, 0xE3,        // 801D JR #8002
    };
    Z80* z80 = b.z80();
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x7FFF + i), program[i]);
    z80->pc = 0x7FFF;
}

/// The machine with the classic card talking, its command log on (@p limit: the ring's size, 0 the default; -1 off)
bool BuildTalking(ttdtest::Bench& b, int limit)
{
    if (!ttdtest::Build(b, ttdtest::Card::GS))
        return false;
    WriteTalker(b);
    b.emulator->RunNFrames(1);
    GSCommandLog* log = b.context->pSoundManager->gsCommandLog();
    if (!log)
    {
        ADD_FAILURE() << "no command log";
        return false;
    }
    if (limit >= 0)
    {
        std::string error;
        if (!log->History().Configure({.mode = history::Mode::Ring, .limit = static_cast<size_t>(limit)}, error))
        {
            ADD_FAILURE() << error;
            return false;
        }
    }
    return true;
}

struct Moment
{
    uint64_t frame = 0;
    uint32_t t = 0;
};

Moment MachineNow(ttdtest::Bench& b)
{
    return {b.context->emulatorState.frame_counter, b.z80()->t};
}

/// The entries not after @p m
std::vector<GSCommandLogEntry> UpTo(const std::vector<GSCommandLogEntry>& all, const Moment& m)
{
    std::vector<GSCommandLogEntry> out;
    for (const GSCommandLogEntry& e : all)
        if (e.frame < m.frame || (e.frame == m.frame && e.mainT <= m.t))
            out.push_back(e);
    return out;
}

void ExpectSameEntries(const std::vector<GSCommandLogEntry>& actual, const std::vector<GSCommandLogEntry>& expected,
                       const std::string& what)
{
    EXPECT_EQ(actual.size(), expected.size()) << what;
    const size_t n = std::min(actual.size(), expected.size());
    for (size_t i = 0; i < n; i++)
        if (std::memcmp(&actual[i], &expected[i], sizeof(GSCommandLogEntry)) != 0)
        {
            ADD_FAILURE() << what << ": entry " << i << " differs: " << actual[i].Text() << " (frame "
                          << actual[i].frame << " t " << actual[i].mainT << ") vs " << expected[i].Text() << " (frame "
                          << expected[i].frame << " t " << expected[i].mainT << ")";
            return;
        }
}

/// The first #2F of frame @p frame: a seek to it lands after the command, its post byte still due
const GSCommandLogEntry* SetTrackIn(const std::vector<GSCommandLogEntry>& all, uint64_t frame)
{
    for (const GSCommandLogEntry& e : all)
        if (e.frame == frame && e.kind == GSCommandLogKind::Command && e.command == 0x2F &&
            !(e.flags & GSCommandLogFlags::kAutomation))
            return &e;
    return nullptr;
}

ttd::TTDTimePoint TargetOf(ttdtest::Bench& b, const GSCommandLogEntry& e)
{
    return {e.frame, b.context->emulatorState.TtdTInFrame(e.mainT)};
}

/// From where the machine stands to @p end on the same path: the T-states between
void RunTo(ttdtest::Bench& b, const Moment& end)
{
    const Moment now = MachineNow(b);
    const uint64_t frame = b.context->emulatorState.BaseToCpuT(b.context->config.frame);
    const uint64_t distance = (end.frame - now.frame) * frame + end.t - now.t;
    b.emulator->RunTStates(distance);
}

}  // namespace

/// A seek back holds the entries after the target back; seeks forward within the history bring them back, nothing
/// twice or missing; the decoder stands where the machine stands (#2F's post byte due)
TEST(GSCommandLog_Test, TtdSeekBackCutsAndSeekForwardRestores)
{
    ttdtest::Bench b;
    ASSERT_TRUE(BuildTalking(b, 0));
    GSCommandLog& log = *b.context->pSoundManager->gsCommandLog();
    ttdtest::Record(b, 5);
    const Moment end = MachineNow(b);
    const ttd::TTDTimePoint endPoint = b.ttd->CurrentPosition();
    const std::vector<GSCommandLogEntry> straight = log.History().Snapshot();
    ASSERT_GT(straight.size(), 100u);

    const GSCommandLogEntry* back = SetTrackIn(straight, straight.front().frame + 2);
    ASSERT_NE(back, nullptr);
    ASSERT_TRUE(b.ttd->SeekTo(TargetOf(b, *back)));
    const Moment there = MachineNow(b);
    const std::vector<GSCommandLogEntry> cut = log.History().Snapshot();
    ExpectSameEntries(cut, UpTo(straight, there), "after the seek back");
    EXPECT_LT(cut.size(), straight.size());
    EXPECT_EQ(log.History().Held(), straight.size() - cut.size());
    if (log.Profile())
    {
        const GSProtocolState state = log.State();
        EXPECT_EQ(state.command, 0x2F);
        EXPECT_LT(state.paramsReceived, state.paramsExpected) << state.Text();
    }

    const GSCommandLogEntry* forward = SetTrackIn(straight, back->frame + 1);
    ASSERT_NE(forward, nullptr);
    ASSERT_TRUE(b.ttd->SeekTo(TargetOf(b, *forward)));
    ExpectSameEntries(log.History().Snapshot(), UpTo(straight, MachineNow(b)), "after a seek forward");

    ASSERT_TRUE(b.ttd->StepBackFrame());
    ExpectSameEntries(log.History().Snapshot(), UpTo(straight, MachineNow(b)), "after a frame step back");

    ASSERT_TRUE(b.ttd->SeekTo(endPoint));
    ASSERT_EQ(MachineNow(b).frame, end.frame);
    ExpectSameEntries(log.History().Snapshot(), straight, "back at the end");
    EXPECT_EQ(log.History().Held(), 0u);
    EXPECT_EQ(log.History().GetStats().written, straight.size());
}

/// After a seek back, the run forward from there (a resume: a new branch) logs what a straight run logged, also in a
/// ring that wrapped; a switch back to the trunk keeps the past the two lines share
TEST(GSCommandLog_Test, TtdRunAfterSeekBackLogsAsAStraightRun)
{
    for (const int limit : {0, 256})
    {
        SCOPED_TRACE(limit ? "a ring of 256" : "the default ring");
        ttdtest::Bench b;
        ASSERT_TRUE(BuildTalking(b, limit));
        GSCommandLog& log = *b.context->pSoundManager->gsCommandLog();
        ttdtest::Record(b, 5);
        const Moment end = MachineNow(b);
        const std::vector<GSCommandLogEntry> straight = log.History().Snapshot();
        const history::Stats straightStats = log.History().GetStats();
        if (limit)
            ASSERT_GT(straightStats.dropped, 0u) << "the ring wrapped";

        // The wrapped ring holds less than a frame: the seek goes back to a #2F in its older half
        const GSCommandLogEntry* back = nullptr;
        if (limit)
        {
            for (size_t i = straight.size() / 4; i < straight.size() && !back; i++)
                if (straight[i].kind == GSCommandLogKind::Command && straight[i].command == 0x2F)
                    back = &straight[i];
        }
        else
            back = SetTrackIn(straight, straight.back().frame - 2);
        ASSERT_NE(back, nullptr);
        ASSERT_TRUE(b.ttd->SeekTo(TargetOf(b, *back)));
        const Moment there = MachineNow(b);
        const ttd::TTDTimePoint fork = b.ttd->CurrentPosition();
        ASSERT_TRUE(b.ttd->ResumeRecordingFrom(fork));
        RunTo(b, end);
        ASSERT_EQ(MachineNow(b).frame, end.frame);
        ASSERT_EQ(MachineNow(b).t, end.t);
        b.ttd->StopRecording();

        ExpectSameEntries(log.History().Snapshot(), straight, "the run forward");
        const history::Stats stats = log.History().GetStats();
        EXPECT_EQ(stats.written, straightStats.written);
        EXPECT_EQ(stats.dropped, straightStats.dropped);
        EXPECT_EQ(log.History().Held(), 0u);

        if (limit)
            continue;
        // The trunk's end: its entries after the fork are not this log's any more, the shared past stays
        std::string error;
        ASSERT_TRUE(b.ttd->SwitchBranch(0, std::nullopt, error)) << error;
        ExpectSameEntries(log.History().Snapshot(), UpTo(straight, there), "the trunk after the switch");
        EXPECT_EQ(log.History().Held(), 0u);
    }
}

/// With the log off, time travel finds nothing to cut: no entries, no marks, nothing held
TEST(GSCommandLog_Test, TtdSeekWithTheLogOff)
{
    ttdtest::Bench b;
    ASSERT_TRUE(BuildTalking(b, -1));
    GSCommandLog& log = *b.context->pSoundManager->gsCommandLog();
    ttdtest::Record(b, 3);
    const Moment end = MachineNow(b);
    ASSERT_TRUE(b.ttd->SeekTo({end.frame - 1, 1000}));
    ASSERT_TRUE(b.ttd->ResumeRecordingFrom(b.ttd->CurrentPosition()));
    b.emulator->RunNFrames(1);
    b.ttd->StopRecording();
    EXPECT_TRUE(log.History().Snapshot().empty());
    EXPECT_EQ(log.History().Held(), 0u);
    EXPECT_EQ(log.Marks(), 0u);
}

/// endregion </Time travel (D3)>
