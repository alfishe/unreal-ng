// The tape deck as the media manager's slot "tape": the medium owns the parsed
// image, the deck plays a copy; eject, re-insert, transport stop, TTD refusal,
// export, `auto` slot choice (integration-tape.md §2-§5)

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"
#include "emulator/io/tape/tapeslot.h"
#include "emulator/media/mediacontrol.h"
#include "emulator/media/mediamanager.h"
#include "loaders/tape/loader_tape.h"

namespace
{
    class TapeSlot_Test : public ::testing::Test
    {
    protected:
        Emulator* _emulator = nullptr;
        EmulatorContext* _context = nullptr;

        void SetUp() override
        {
            _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
            ASSERT_NE(_emulator, nullptr);
            _context = _emulator->GetContext();
        }
        void TearDown() override
        {
            if (_emulator)
                EmulatorTestHelper::CleanupEmulator(_emulator);
            MessageCenter::DisposeDefaultMessageCenter();
        }

        MediaManager& Manager() { return *_context->pMediaManager; }
        Tape& Deck() { return *_context->pTape; }

        static std::string TapFile() { return TestPathHelper::GetTestDataPath("loaders/tap/AYtest_v0.2.tap"); }
        static std::string TzxFile() { return TestPathHelper::GetTestDataPath("loaders/tzx/bb-redux.tzx"); }

        MediaResult Insert(const std::string& path)
        {
            MediaSource source;
            source.path = path;
            InsertOptions options;
            options.immediate = true;
            return Manager().Insert(TapeSlot::kId, source, options);
        }
    };
}  // namespace

TEST_F(TapeSlot_Test, TheSlotDescribesTheDeck)
{
    const auto info = Manager().Info("tape");
    ASSERT_TRUE(info.has_value()) << "every machine with a deck has the tape slot";
    EXPECT_EQ(info->descriptor.kind, MediaKind::Tape);
    EXPECT_EQ(info->descriptor.swapDelayMs, 0u) << "no guest-side tape detection";
    EXPECT_TRUE(info->descriptor.acceptsFolder);
    EXPECT_EQ(info->descriptor.defaultAccess, AccessMode::ReadOnly);
    EXPECT_FALSE(info->present);

    std::string slot;
    ASSERT_TRUE(MediaControl::ResolveSelector(Manager(), "TAPE", slot).Ok());
    EXPECT_EQ(slot, "tape");
    ASSERT_TRUE(MediaControl::ResolveSelector(Manager(), "tape:0", slot).Ok());
    EXPECT_EQ(slot, "tape");
}

/// The deck plays the medium's blocks; a transport stop drops the deck's copy,
/// the next play installs it again from the medium
TEST_F(TapeSlot_Test, TheDeckPlaysTheMediumsImage)
{
    ASSERT_TRUE(Insert(TapFile()).Ok());
    const Medium* medium = Manager().GetMedium("tape");
    ASSERT_NE(medium, nullptr);
    ASSERT_NE(medium->Tape(), nullptr);
    EXPECT_EQ(medium->Access(), AccessMode::ReadOnly) << "a tape is only ever read";
    EXPECT_EQ(medium->Format(), "tap");
    EXPECT_EQ(_context->coreState.tapeFilePath, TapFile()) << "the path is mirrored for display";

    ASSERT_TRUE(Deck().EnsureImageLoaded());
    EXPECT_EQ(Deck().GetBlockCatalog().size(), medium->Tape()->blocks.size());
    EXPECT_EQ(Deck().GetLoadedFormatId(), "tap");

    Deck().stopTape();
    EXPECT_TRUE(Deck().GetBlockCatalog().empty()) << "stop drops the deck's copy";
    ASSERT_TRUE(Deck().EnsureImageLoaded()) << "the medium still has the tape";
    EXPECT_EQ(Deck().GetBlockCatalog().size(), medium->Tape()->blocks.size());
}

/// Re-inserting the same file starts a fresh tape (the old path-keyed parse
/// kept the old cursor); a swap to another format replaces the image
TEST_F(TapeSlot_Test, ReinsertAndSwapStartAfresh)
{
    ASSERT_TRUE(Insert(TapFile()).Ok());
    ASSERT_TRUE(Deck().EnsureImageLoaded());
    Deck().ConsumeBlock(0);
    ASSERT_EQ(Deck().GetConsumptionCursor(), 1u);

    ASSERT_TRUE(Insert(TapFile()).Ok());
    ASSERT_TRUE(Deck().EnsureImageLoaded());
    EXPECT_EQ(Deck().GetConsumptionCursor(), 0u);

    ASSERT_TRUE(Insert(TzxFile()).Ok());
    ASSERT_TRUE(Deck().EnsureImageLoaded());
    EXPECT_EQ(Deck().GetLoadedFormatId(), "tzx");
    EXPECT_EQ(_context->coreState.tapeFilePath, TzxFile());
}

TEST_F(TapeSlot_Test, EjectEmptiesTheDeck)
{
    ASSERT_TRUE(_emulator->LoadTape(TapFile()));
    ASSERT_TRUE(Deck().EnsureImageLoaded());

    std::string error;
    ASSERT_TRUE(_emulator->EjectTape(&error)) << error;
    EXPECT_EQ(Manager().GetMedium("tape"), nullptr);
    EXPECT_TRUE(_context->coreState.tapeFilePath.empty());
    EXPECT_FALSE(Deck().EnsureImageLoaded());
    EXPECT_TRUE(_emulator->EjectTape(&error)) << "an empty deck ejects again without complaint";
}

/// The media set is fixed while TTD records: a tape insert or eject is refused
TEST_F(TapeSlot_Test, RecordingRefusesTapeChanges)
{
    ASSERT_TRUE(_emulator->LoadTape(TapFile()));
    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ASSERT_TRUE(_context->pTimeTravelManager->StartRecording());

    std::string error;
    EXPECT_FALSE(_emulator->EjectTape(&error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(Manager().GetMedium("tape"), nullptr);
    EXPECT_FALSE(_emulator->LoadTape(TzxFile(), &error));
    EXPECT_EQ(_context->coreState.tapeFilePath, TapFile());

    EjectOptions end;
    end.endRecording = true;
    ASSERT_TRUE(Manager().Eject("tape", end).Ok());
    EXPECT_FALSE(_context->pTimeTravelManager->IsRecording());
}

/// Any registry format loads; content decides, and a non-tape answers why
TEST_F(TapeSlot_Test, EveryRegistryFormatAndNothingElse)
{
    const std::vector<std::string> extensions = TapeLoaderRegistry::Instance().SupportedExtensions();
    for (const char* ext : {"tap", "spc", "sta", "ltp", "zxt", "tzx"})
        EXPECT_NE(std::find(extensions.begin(), extensions.end(), ext), extensions.end()) << ext;

    const std::string junk = TestPathHelper::GetUniqueTestScratchPath("tapeslot-junk.tap");
    FILE* file = fopen(junk.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    fputs("not a tape at all", file);
    fclose(file);
    const MediaResult refused = Insert(junk);
    EXPECT_EQ(refused.error, MediaError::UnknownFormat) << refused.message;
    EXPECT_EQ(Manager().GetMedium("tape"), nullptr);
    std::remove(junk.c_str());
}

/// A tape exports to .tzx always, to .tap when every block is a ROM-standard
/// byte block; the export reloads to the same blocks
TEST_F(TapeSlot_Test, ExportWritesTzxOrTap)
{
    ASSERT_TRUE(Insert(TapFile()).Ok());
    const size_t blocks = Manager().GetMedium("tape")->Tape()->blocks.size();

    for (const char* name : {"tapeslot-export.tzx", "tapeslot-export.tap"})
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
        const MediaResult exported = Manager().Export("tape", path);
        ASSERT_TRUE(exported.Ok()) << name << ": " << exported.message;
        ASSERT_TRUE(Insert(path).Ok()) << name;
        EXPECT_EQ(Manager().GetMedium("tape")->Tape()->blocks.size(), blocks) << name;
        ASSERT_TRUE(Insert(TapFile()).Ok());
        std::remove(path.c_str());
    }
}

/// `media insert auto <file.tzx>` goes to the tape slot
TEST_F(TapeSlot_Test, AutoInsertPicksTheDeck)
{
    MediaControl control(_context);
    MediaRequest request;
    request.verb = "insert";
    request.selector = "auto";
    request.path = TzxFile();
    const MediaReply reply = control.Execute(request);
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.slot, "tape");
    EXPECT_NE(Manager().GetMedium("tape"), nullptr);
}
