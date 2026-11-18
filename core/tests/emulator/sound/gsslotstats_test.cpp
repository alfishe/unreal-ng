// GS-slot statistics (gsslotstats.h, gs-debugger N7): one block for every card - the common sections, the card's own;
// baselines, the error total, published at frame ends only while a reader asks

#include "emulator/sound/chips/gs/gsslotstats.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/neogstestsdcard.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/soundcardscope.h"
#include "debugger/boards/debugboards.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"

namespace
{

/// ATM710 fits a General Sound card by default (data/configs/atm710/unreal.ini)
constexpr const char* kGsCapableModel = "ATM710";

class GSSlotStats_Test : public ::testing::Test
{
protected:
    SoundCardScope _gs{TestSound::GeneralSound};  // first: the GS slot must be fitted when the machine is created
    std::unique_ptr<ScratchFatImage> _image;      // outlives the emulator
    Emulator* _emulator = nullptr;
    EmulatorContext* _ctx = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(kGsCapableModel, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _ctx = _emulator->GetContext();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    GeneralSoundCard* Card() const { return _ctx->pSoundManager->getGeneralSound(); }

    GSStatsReply Read(const std::string& section = "", uint32_t since = 0) const
    {
        GSStatsQuery query;
        query.section = section;
        query.since = since;
        return GSStatsRead(_ctx, query);
    }

    /// A leaf by its dotted path ("mailbox.host_commands")
    static const StateNode* At(const StateNode& body, const std::string& path)
    {
        const StateNode* node = &body;
        size_t start = 0;
        while (node)
        {
            const size_t dot = path.find('.', start);
            node = node->find(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
            if (dot == std::string::npos)
                break;
            start = dot + 1;
        }
        EXPECT_NE(node, nullptr) << "no " << path;
        return node;
    }

    static int64_t Count(const StateNode& body, const std::string& path)
    {
        const StateNode* node = At(body, path);
        return node ? node->i : -1;
    }

    static bool Has(const GSStatsReply& reply, const std::string& section)
    {
        const StateNode* capabilities = reply.body.find("capabilities");
        if (!capabilities)
            return false;
        return std::any_of(capabilities->items.begin(), capabilities->items.end(),
                           [&](const StateNode& c) { return c.s == section; });
    }
};

} // namespace

/// The classic card: both sides of the mailbox, a write over an unread command is an overwrite (and an error); no
/// NeoGS sections, a missing one is a 404 naming the card's
TEST_F(GSSlotStats_Test, TheClassicCardCountsTheMailboxAndHasNoNeoGSSections)
{
    ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::Z80));
    const GSStatsReply before = Read();
    ASSERT_EQ(before.status, 200) << before.text;
    const int64_t commands = Count(before.body, "mailbox.host_commands");

    Card()->sendCommand(0xF3);
    Card()->sendCommand(0xF4); // the card has not run: the first is still unread

    const GSStatsReply stats = Read();
    ASSERT_EQ(stats.status, 200) << stats.text;
    EXPECT_FALSE(stats.pending);
    EXPECT_EQ(stats.body.find("implementation")->s, "lle");
    EXPECT_EQ(Count(stats.body, "mailbox.host_commands"), commands + 2);
    EXPECT_EQ(Count(stats.body, "mailbox.command_overwrites"), Count(before.body, "mailbox.command_overwrites") + 1);
    EXPECT_TRUE(At(stats.body, "mailbox.unconfirmed")->b);
    EXPECT_GE(Count(stats.body, "errors.total"), 1);
    EXPECT_EQ(Count(stats.body, "card.clock_hz"), 12000000);
    EXPECT_TRUE(Has(stats, "cpu") && Has(stats, "interrupts") && Has(stats, "audio") && Has(stats, "rates"));
    EXPECT_FALSE(Has(stats, "dma") || Has(stats, "sd") || Has(stats, "flash"));
    EXPECT_NE(stats.text.find("mailbox: "), std::string::npos) << stats.text;

    const GSStatsReply one = Read("mailbox");
    ASSERT_EQ(one.status, 200) << one.text;
    EXPECT_NE(one.body.find("mailbox"), nullptr);
    EXPECT_EQ(one.body.find("cpu"), nullptr) << "one section";

    const GSStatsReply missing = Read("dma");
    EXPECT_EQ(missing.status, 404);
    EXPECT_NE(missing.text.find("Sections: card, mailbox"), std::string::npos) << missing.text;
}

/// reset_counters is a baseline: a read since it counts from it, the machine keeps counting; an unknown id is a 404
TEST_F(GSSlotStats_Test, ABaselineCountsFromTheReset)
{
    ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::Z80));
    Card()->sendCommand(0xF3);
    const GSStatsReply reset = GSStatsResetCounters(_ctx, GSStatsQuery{});
    ASSERT_EQ(reset.status, 200) << reset.text;
    const uint32_t baseline = static_cast<uint32_t>(reset.body.find("baseline")->i);
    EXPECT_GT(baseline, 0u);

    Card()->sendCommand(0xF4);
    const GSStatsReply since = Read("", baseline);
    ASSERT_EQ(since.status, 200) << since.text;
    EXPECT_EQ(Count(since.body, "mailbox.host_commands"), 1);
    EXPECT_EQ(Count(since.body, "mailbox.command_overwrites"), 1);
    EXPECT_EQ(Count(since.body, "errors.command_overwrites"), 1);
    EXPECT_EQ(since.body.find("since")->i, baseline);
    EXPECT_GT(Count(Read().body, "mailbox.host_commands"), 1) << "the machine's counters are not reset";

    EXPECT_EQ(Read("", baseline + 100).status, 404);
}

/// The lightweight player: no coprocessor sections, its parameter queue's overflows are an error counter
TEST_F(GSSlotStats_Test, TheLightweightPlayerHasNoCpu)
{
    ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::LW));
    const GSStatsReply stats = Read();
    ASSERT_EQ(stats.status, 200) << stats.text;
    EXPECT_EQ(stats.body.find("implementation")->s, "lightweight");
    EXPECT_FALSE(Has(stats, "cpu") || Has(stats, "interrupts"));
    EXPECT_GE(Count(stats.body, "mailbox.param_queue_overflows"), 0);
    EXPECT_GE(Count(stats.body, "errors.param_queue_overflows"), 0);
}

/// Publishing: nothing at frame ends without a reader; a read keeps it on for 100 frames, then it stops. The rates
/// come from the window of published frames
TEST_F(GSSlotStats_Test, FrameEndsPublishOnlyWhileRead)
{
    ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::Z80));
    GSStatsPublisher publisher;
    for (int i = 0; i < 3; i++)
        publisher.FrameEnd(Card(), 0.02, nullptr);
    EXPECT_EQ(publisher.Latest(), nullptr) << "no reader: no block";

    publisher.Want();
    publisher.FrameEnd(Card(), 0.02, nullptr);
    ASSERT_NE(publisher.Latest(), nullptr);
    EXPECT_EQ(publisher.Latest()->frame, 4u);
    EXPECT_EQ(publisher.Latest()->rates.find("window_frames"), nullptr) << "one frame: no rate yet";
    Card()->sendCommand(0xF3);
    publisher.FrameEnd(Card(), 0.02, nullptr);
    const std::shared_ptr<const GSStatsBlock> two = publisher.Latest();
    ASSERT_NE(two->rates.find("commands"), nullptr);
    EXPECT_DOUBLE_EQ(two->rates.find("commands")->d, 50.0) << "one command in a 20 ms frame";

    for (uint64_t i = 0; i < GSStatsPublisher::kWantedFrames + 5; i++)
        publisher.FrameEnd(Card(), 0.02, nullptr);
    EXPECT_EQ(publisher.Latest()->frame, 3 + GSStatsPublisher::kWantedFrames) << "stops 100 frames after the read";
    EXPECT_TRUE(publisher.WaitFor(1, 0));
    EXPECT_FALSE(publisher.WaitFor(publisher.Frame() + 1, 1));
}

/// The machine's own frame ends publish into SoundManager's block while a reader asks
TEST_F(GSSlotStats_Test, TheMachinesFrameEndsPublish)
{
    ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::Z80));
    GSStatsPublisher& publisher = _ctx->pSoundManager->gsStats();
    publisher.Want();
    const uint64_t frame = publisher.Frame();
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    ASSERT_NE(publisher.Latest(), nullptr);
    EXPECT_EQ(publisher.Latest()->frame, frame + 2);
    EXPECT_GT(publisher.Latest()->rates.find("cpu_steps")->d, 0.0);
    EXPECT_FALSE(publisher.Latest()->replaying);
}

/// NeoGS adds its device sections; the Stats board shows the same numbers as the read
TEST_F(GSSlotStats_Test, NeoGSAddsItsDevicesAndTheBoardShowsTheSameBlock)
{
    _image = MakeNeoGSTestSd(NeoGSTestSd::Fat16Mbr);
    ASSERT_TRUE(_image->ok()) << _image->error();
    ASSERT_TRUE(FitNeoGSWithSd(_ctx, _image->path()));
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);

    const GSStatsReply stats = Read();
    ASSERT_EQ(stats.status, 200) << stats.text;
    EXPECT_EQ(stats.body.find("implementation")->s, "ngs");
    for (const char* section : {"cpu", "interrupts", "audio", "dma", "spi", "sd", "flash", "rates", "errors"})
        EXPECT_TRUE(Has(stats, section)) << section;
    NeoGSStateInfo s;
    ASSERT_TRUE(Card()->neogsState(s));
    EXPECT_EQ(Count(stats.body, "card.clock_hz"), s.clockHz);
    EXPECT_EQ(Count(stats.body, "card.gscfg0"), s.gscfg0);
    EXPECT_TRUE(At(stats.body, "sd.present")->b);
    EXPECT_GE(Count(stats.body, "sd.inserts"), 1);
    EXPECT_GE(Count(stats.body, "interrupts.by_source.timer"), 0);
    EXPECT_GE(Count(stats.body, "dma.sd.starts"), 0);
    EXPECT_GE(Count(stats.body, "spi.sd.bytes"), 0);
    EXPECT_GE(Count(stats.body, "flash.bytes_programmed"), 0);
    EXPECT_GE(Count(stats.body, "errors.sd_crc"), 0);
    EXPECT_EQ(Count(Read("sd").body, "sd.inserts"), Count(stats.body, "sd.inserts"));

    const DebugBoardReply board = DebugBoards::Read(_ctx, "gs_stats", /*valuesOnly=*/true);
    ASSERT_EQ(board.status, 200) << board.text;
    const StateNode* values = board.body.find("values");
    ASSERT_NE(values, nullptr);
    ASSERT_NE(values->find("mailbox.host_status_polls"), nullptr) << board.text;
    EXPECT_EQ(values->find("mailbox.host_status_polls")->i, Count(stats.body, "mailbox.host_status_polls"));
    EXPECT_EQ(values->find("card.clock_hz")->i, s.clockHz);
}
