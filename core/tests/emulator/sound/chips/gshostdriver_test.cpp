// The debugger's send (gs-debugger design §6.4, F6) and the protocol state (F3): a command sent as a careful host
// sends it, its replies read and decoded; the conversation's state read from the command log's decoder

#include "emulator/sound/chips/gs/gshostdriver.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "debugger/ttd/ttdinputapply.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/gscommandlog.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/gs/soundchip_gslw.h"

namespace
{

constexpr uint16_t kData = GeneralSoundCard::PORT_DATA;
constexpr uint16_t kCommand = GeneralSoundCard::PORT_COMMAND;

/// A card, its command log and a driver whose steps apply at once (the TTD hooks' live input, without a machine)
template <typename Chip>
struct DrivenCard
{
    EmulatorContext ctx{LoggerLevel::LogError};
    std::unique_ptr<Chip> chip;
    GSCommandLog log{&ctx, nullptr, "gs.commands"};
    bool refuse = false;
    std::unique_ptr<GSHostDriver> driver;

    DrivenCard()
    {
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<Chip>(&ctx, 128, 44100);
        chip->attachCommandLog(&log);
        driver = std::make_unique<GSHostDriver>(&ctx, [this](const ttd::TTDInputEvent& ev) {
            if (refuse)
                return false;
            ttd::TTDInputDevices devices;
            devices.generalSound = chip.get();
            return ttd::ApplyInputEvent(ev, devices);
        });
    }
    ~DrivenCard() { chip->attachCommandLog(nullptr); }

    void startLog()
    {
        std::string error;
        ASSERT_TRUE(log.History().Configure({.mode = history::Mode::Ring}, error)) << error;
    }
    void frame()
    {
        ctx.emulatorState.frame_counter++;
        chip->handleFrameStart();
        if (driver->Active())
            driver->Frame(*chip);
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
    }
    /// Frames until the conversation finishes, at most @p frames
    GSConversation run(uint32_t id, int frames = 40)
    {
        for (int i = 0; i < frames && driver->Find(id) && !driver->Find(id)->Finished(); i++)
            frame();
        return driver->Find(id).value_or(GSConversation{});
    }
    bool boot()
    {
        for (int i = 0; i < 1000; i++)
        {
            frame();
            if (chip->isReadyForCommands())
                return true;
        }
        return false;
    }
};

using LwCard = DrivenCard<SoundChip_GSLightweight>;
using LleCard = DrivenCard<SoundChip_GeneralSound>;

} // namespace

/// #23 on the firmware's card: sent once the card may take it, the reply read once written, named by the profile
TEST(GSHostDriver_Test, SendsACommandAndReadsItsReply)
{
    LleCard card;
    card.chip->loadROM("rom/gs105a.rom");
    ASSERT_TRUE(card.chip->isROMLoaded());
    ASSERT_TRUE(card.boot());
    card.startLog();

    std::string error;
    const uint32_t id = card.driver->Submit(*card.chip, {.command = 0x23}, error);
    ASSERT_NE(id, 0u) << error;
    EXPECT_EQ(card.driver->Find(0)->state, GSConversation::State::Queued) << "nothing before the frames";

    const GSConversation c = card.run(id);
    ASSERT_EQ(c.state, GSConversation::State::Done) << c.Text();
    ASSERT_EQ(c.replies.size(), 1u) << c.Text();
    EXPECT_EQ(c.replies[0], 128 / 32 - 1) << "128 KB: the 32 KB pages but the system's; " << c.Text();
    EXPECT_EQ(c.Text(), "#23 Get number of RAM pages -> #03 (pages)");
    EXPECT_FALSE(card.driver->Active());

    // The log saw a careful host: the command, the card taking it, the reply - no anomaly
    bool reply = false, anomaly = false;
    for (const GSCommandLogEntry& e : card.log.History().Snapshot())
    {
        reply = reply || (e.kind == GSCommandLogKind::Reply && !(e.flags & GSCommandLogFlags::kNotReady));
        anomaly = anomaly || e.kind == GSCommandLogKind::Anomaly;
        if (e.kind == GSCommandLogKind::Command)
            EXPECT_TRUE(e.flags & GSCommandLogFlags::kAutomation);
    }
    EXPECT_TRUE(reply);
    EXPECT_FALSE(anomaly);

    // #2C reads its pre byte after writing the reply, which clears the data flag: the reply is read once the card
    // took the command, not waited for
    const uint32_t set = card.driver->Submit(*card.chip, {.command = 0x2C, .pre = 0x00}, error);
    ASSERT_NE(set, 0u) << error;
    const GSConversation s = card.run(set);
    ASSERT_EQ(s.state, GSConversation::State::Done) << s.Text();
    ASSERT_EQ(s.replies.size(), 1u) << s.Text();
    for (const GSCommandLogEntry& e : card.log.History().Snapshot())
        EXPECT_NE(e.kind, GSCommandLogKind::Anomaly) << e.Text();
}

/// The pre byte and the parameters as the profile names them; a wrong count is refused before anything is sent
TEST(GSHostDriver_Test, ChecksAndNamesTheParameters)
{
    LwCard card;
    std::string error;
    EXPECT_EQ(card.driver->Submit(*card.chip, {.command = 0x2C}, error), 0u);
    EXPECT_NE(error.find("takes a pre byte"), std::string::npos) << error;
    EXPECT_EQ(card.driver->Submit(*card.chip, {.command = 0x2F, .pre = 0x34}, error), 0u);
    EXPECT_NE(error.find("takes 1 parameter byte"), std::string::npos) << error;
    EXPECT_EQ(card.driver->Submit(*card.chip, {.command = 0x23, .pre = 1}, error), 0u);
    EXPECT_NE(error.find("takes no pre byte"), std::string::npos) << error;
    EXPECT_FALSE(card.driver->Active()) << "refused: nothing queued";

    const uint32_t id = card.driver->Submit(*card.chip, {.command = 0x2F, .pre = 0x34, .params = {0x12}}, error);
    ASSERT_NE(id, 0u) << error;
    const GSConversation c = card.run(id);
    ASSERT_EQ(c.state, GSConversation::State::Done) << c.Text();
    EXPECT_EQ(c.Text(), "#2F Set current track, track #34, track #12");
}

/// A reply nobody read is read first; a card that never takes the command fails the send; a replay refuses it
TEST(GSHostDriver_Test, DrainsTimesOutAndRefuses)
{
    {
        LleCard card;  // no ROM: the card's CPU runs nothing, nothing is ever taken
        card.chip->portDeviceOutMethod(kCommand, 0x01);
        std::string error;
        const uint32_t id = card.driver->Submit(*card.chip, {.command = 0x23}, error);
        const GSConversation c = card.run(id, GSHostDriver::kStepFrames + 5);
        EXPECT_EQ(c.state, GSConversation::State::Failed);
        EXPECT_NE(c.error.find("status bit 0"), std::string::npos) << c.error;
    }
    {
        LwCard card;
        card.chip->portDeviceOutMethod(kCommand, 0x23);  // its reply left unread
        std::string error;
        const uint32_t id = card.driver->Submit(*card.chip, {.command = 0x23}, error);
        const GSConversation c = card.run(id);
        ASSERT_EQ(c.state, GSConversation::State::Done) << c.Text();
        EXPECT_TRUE(c.drained) << c.Text();
        EXPECT_EQ(c.replies.size(), 1u);
    }
    {
        LwCard card;
        card.refuse = true;
        std::string error;
        const uint32_t id = card.driver->Submit(*card.chip, {.command = 0x23}, error);
        const GSConversation c = card.run(id);
        EXPECT_EQ(c.state, GSConversation::State::Failed);
        EXPECT_NE(c.error.find("replay"), std::string::npos) << c.error;
    }
}

/// F3: what the conversation is at, from the log's decoder - a parameter due, an upload's bytes, nothing when off. The
/// card's taking of a command is seen at the next host access or the frame start, whichever comes first
TEST(GSHostDriver_Test, TheProtocolStateFollowsTheConversation)
{
    LwCard card;
    EXPECT_FALSE(card.log.State().on);

    card.startLog();
    card.chip->portDeviceOutMethod(kData, 0x34);
    card.chip->portDeviceOutMethod(kCommand, 0x2F);
    GSProtocolState state = card.log.State();
    ASSERT_TRUE(state.on);
    EXPECT_EQ(state.command, 0x2F);
    EXPECT_EQ(state.paramsExpected, 2);
    EXPECT_EQ(state.paramsReceived, 1);
    EXPECT_EQ(state.Text(), "#2F Set current track: not taken yet, then parameter 2 of 2 due");
    card.frame();
    state = card.log.State();
    EXPECT_TRUE(state.taken);
    EXPECT_EQ(state.Text(), "#2F Set current track: parameter 2 of 2 due");

    card.chip->portDeviceOutMethod(kData, 0x00);
    card.chip->portDeviceOutMethod(kCommand, 0x30);
    (void)card.chip->portDeviceInMethod(kData);
    for (int i = 0; i < 1000; i++)
        card.chip->portDeviceOutMethod(kData, static_cast<uint8_t>(i));
    state = card.log.State();
    EXPECT_TRUE(state.inTransfer);
    EXPECT_EQ(state.transferDone, 1000u);
    EXPECT_NE(state.Text().find("module upload: 1,000 bytes"), std::string::npos) << state.Text();

    card.chip->portDeviceOutMethod(kCommand, 0xD2);
    card.frame();
    state = card.log.State();
    EXPECT_FALSE(state.inTransfer);
    EXPECT_NE(state.Text().find("idle"), std::string::npos) << state.Text();
}
