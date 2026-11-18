// Device boards (debugger-model widget catalog §4, gs-debugger N4 / N8): the GS-slot card publishes its hardware as
// boards built on read; the NeoGS SD board inserts and ejects through the media manager's slot `sd.ngs`

#include "debugger/boards/debugboards.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/neogstestsdcard.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/soundcardscope.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"

namespace
{

/// ATM710 fits a General Sound card by default (data/configs/atm710/unreal.ini)
constexpr const char* kGsCapableModel = "ATM710";

class DebugBoards_Test : public ::testing::Test
{
protected:
    SoundCardScope _gs{TestSound::GeneralSound};  // first: the GS slot must be fitted when the machine is created
    std::unique_ptr<ScratchFatImage> _image;      // outlives the emulator
    Emulator* _emulator = nullptr;
    EmulatorContext* _ctx = nullptr;

    void SetUp() override
    {
        _image = MakeNeoGSTestSd(NeoGSTestSd::Fat16Mbr);
        ASSERT_TRUE(_image->ok()) << _image->error();
        _emulator = EmulatorTestHelper::CreateStandardEmulator(kGsCapableModel, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _ctx = _emulator->GetContext();
        ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::Z80));
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// A control's value in a values-only read
    static const StateNode& Value(const DebugBoardReply& reply, const std::string& control)
    {
        static const StateNode kMissing;
        const StateNode* values = reply.body.find("values");
        const StateNode* v = values ? values->find(control) : nullptr;
        EXPECT_NE(v, nullptr) << "no control " << control;
        return v ? *v : kMissing;
    }

    std::string Ids() const
    {
        std::string ids;
        const DebugBoardReply list = DebugBoards::List(_ctx); // the items live in it
        for (const StateNode& b : list.body.find("boards")->items)
            ids += (ids.empty() ? "" : " ") + b.find("id")->s;
        return ids;
    }
};

} // namespace

/// The classic card: one board, its mailbox and channels; another id is not found and the error lists the boards
TEST_F(DebugBoards_Test, TheClassicCardPublishesTheGsBoard)
{
    EXPECT_EQ(Ids(), "gs gs_stats");
    const DebugBoardReply board = DebugBoards::Read(_ctx, "gs");
    ASSERT_EQ(board.status, 200) << board.text;
    EXPECT_EQ(board.body.find("cpu")->s, "gs");
    EXPECT_NE(board.text.find("Mailbox"), std::string::npos) << board.text;

    const DebugBoardReply values = DebugBoards::Read(_ctx, "gs", /*valuesOnly=*/true);
    const GeneralSoundCard* gs = _ctx->pSoundManager->getGeneralSound();
    EXPECT_EQ(Value(values, "mpag").i, gs->getMPAG());
    EXPECT_EQ(Value(values, "status").i, gs->getStatusRaw());
    EXPECT_EQ(Value(values, "channels[3].volume").i, gs->getChannelVolume(3));
    EXPECT_EQ(values.body.find("columns"), nullptr) << "values only";

    const DebugBoardReply missing = DebugBoards::Read(_ctx, "neogs");
    EXPECT_EQ(missing.status, 404);
    EXPECT_NE(missing.text.find("Boards: gs"), std::string::npos) << missing.text;
}

/// N4: the NeoGS card's boards carry its state, the same numbers as neogsState
TEST_F(DebugBoards_Test, TheNeoGSBoardsCarryTheCardsState)
{
    ASSERT_TRUE(FitNeoGSWithSd(_ctx, _image->path()));
    EXPECT_EQ(Ids(), "neogs neogs_dma neogs_sd neogs_mp3 neogs_flash gs_stats");
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);

    NeoGSStateInfo s;
    ASSERT_TRUE(_ctx->pSoundManager->getGeneralSound()->neogsState(s));
    const DebugBoardReply neogs = DebugBoards::Read(_ctx, "neogs", true);
    EXPECT_EQ(Value(neogs, "gscfg0").i, s.gscfg0);
    EXPECT_EQ(Value(neogs, "clock_hz").i, s.clockHz);
    EXPECT_EQ(Value(neogs, "pages[1]").i, s.pages[1]);
    EXPECT_EQ(Value(neogs, "sstat").i, s.sstat);
    EXPECT_EQ(Value(neogs, "sstat").i & 0x02, 0) << "/SD_DET low: a card is in";
    EXPECT_EQ(Value(neogs, "channels[7].volume").i, _ctx->pSoundManager->getGeneralSound()->getChannelVolume(7));
    EXPECT_EQ(Value(neogs, "flash_title").s, s.flashTitle);

    const DebugBoardReply dma = DebugBoards::Read(_ctx, "neogs_dma", true);
    EXPECT_EQ(Value(dma, "zx.mode").s, s.zxMode);
    EXPECT_EQ(Value(dma, "modules[sd].address").i, s.dmaAddress[1] & 0x1FFFFF);

    const DebugBoardReply sd = DebugBoards::Read(_ctx, "neogs_sd", true);
    EXPECT_TRUE(Value(sd, "present").b);
    EXPECT_EQ(Value(sd, "source").s, _image->path());
    EXPECT_EQ(Value(sd, "source_type").s, "image");
    EXPECT_EQ(Value(sd, "size_bytes").i, int64_t(s.sdSizeBytes));

    const DebugBoardReply mp3 = DebugBoards::Read(_ctx, "neogs_mp3", true);
    EXPECT_EQ(mp3.body.find("available")->b, s.mp3Fitted);
    EXPECT_EQ(Value(mp3, "sci[11]").i, s.mp3Sci[11]);

    const DebugBoardReply flash = DebugBoards::Read(_ctx, "neogs_flash");
    ASSERT_EQ(flash.status, 200);
    EXPECT_NE(flash.text.find("actions: save"), std::string::npos) << flash.text;
}

/// N8: a folder inserted through the SD board is the medium of the slot `sd.ngs`; eject empties it
TEST_F(DebugBoards_Test, TheSdBoardInsertsAFolderThroughTheMediaManager)
{
    ASSERT_TRUE(FitNeoGSWithSd(_ctx, _image->path()));
    ScratchFolder folder("debugboards-sd-folder");
    folder.File("README.TXT", "a host folder as the NeoGS SD card\n");
    const auto u8 = folder.Path().u8string();
    const std::string path(u8.begin(), u8.end());

    const DebugBoardReply eject = DebugBoards::Action(_ctx, "neogs_sd", "eject", "");
    ASSERT_EQ(eject.status, 200) << eject.text;
    EXPECT_FALSE(_ctx->pMediaManager->Info(SoundChip_NeoGS::SD_SLOT_ID)->present);
    EXPECT_FALSE(Value(DebugBoards::Read(_ctx, "neogs_sd", true), "present").b);

    const DebugBoardReply insert = DebugBoards::Action(_ctx, "neogs_sd", "insert", path);
    ASSERT_EQ(insert.status, 200) << insert.text;
    const std::optional<SlotInfo> slot = _ctx->pMediaManager->Info(SoundChip_NeoGS::SD_SLOT_ID);
    ASSERT_TRUE(slot && slot->present);
    EXPECT_EQ(slot->source, path) << "the media list shows the folder in sd.ngs";
    const DebugBoardReply sd = DebugBoards::Read(_ctx, "neogs_sd", true);
    EXPECT_TRUE(Value(sd, "present").b);
    EXPECT_EQ(Value(sd, "source_type").s, "folder");

    EXPECT_EQ(DebugBoards::Action(_ctx, "neogs_sd", "insert", "").status, 400);
    EXPECT_EQ(DebugBoards::Action(_ctx, "neogs_sd", "format", "").status, 404);
    EXPECT_EQ(DebugBoards::Action(_ctx, "neogs_sd", "insert", path + ".missing").status, 422);
    EXPECT_EQ(DebugBoards::Action(_ctx, "gs", "eject", "").status, 404) << "the classic card has no SD board";
}
