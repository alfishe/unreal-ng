/// @file ttdmediaundo_test.cpp
/// @brief A seek sets every medium to the version it had there (TTD gaps 11-12): the tracks and sectors the guest
/// wrote, the write-protect switch the user toggled. The media manager's undo log keeps the units as they were while a
/// history exists; the restore sets each medium before the devices load.
///
/// Boots a machine and records a few frames: slower than the 50 ms guideline.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/coordinator/operationlog.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/floppydriveslot.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"

namespace
{
class TTDMediaUndo_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        std::string error;
        const std::string disk = (TestPathHelper::FindProjectRoot() / "testdata/loaders/trd/atarin.trd").string();
        ASSERT_TRUE(_emulator->LoadDisk(disk, 0, &error)) << error;
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    DiskImage::Track* Track0()
    {
        DiskImage* disk = _context->coreState.diskDrives[0]->getDiskImage();
        return disk ? disk->getTrackForCylinderAndSide(0, 0) : nullptr;
    }
    /// The first data byte of track 0's first sector (the index points into the stream again after a seek)
    uint8_t FirstByte()
    {
        DiskImage::Track* track = Track0();
        return track && !track->sectors().empty() && track->sectors()[0].data ? track->sectors()[0].data[0] : 0;
    }
    /// What a controller does when the guest writes: the hook before, the bytes, the marker
    void GuestWrite(uint8_t value)
    {
        DiskImage::Track* track = Track0();
        ASSERT_NE(track, nullptr);
        FloppyDriveSlot::BeforeTrackWrite(_context, 0, track);
        track->sectors()[0].data[0] = value;
        track->markDirty();
        FloppyDriveSlot::NoteSlotWrite(_context, 0, "test write");
    }
};
}  // namespace

TEST_F(TTDMediaUndo_Test, ASeekSetsTheDiskToTheTracksItHadThere)
{
    const uint8_t original = FirstByte();
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    const uint64_t first = _context->emulatorState.frame_counter;  // its checkpoint is before the write
    ASSERT_NO_FATAL_FAILURE(GuestWrite(static_cast<uint8_t>(original ^ 0x55)));
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    const uint64_t second = _context->emulatorState.frame_counter;
    ASSERT_NO_FATAL_FAILURE(GuestWrite(static_cast<uint8_t>(original ^ 0xAA)));
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo({first, 0}));
    EXPECT_EQ(FirstByte(), original) << "before the first write";
    ASSERT_TRUE(_ttd->SeekTo({second, 0}));
    EXPECT_EQ(FirstByte(), original ^ 0x55) << "between the writes";
    ASSERT_TRUE(_ttd->SeekTo({second + 2, 0}));
    EXPECT_EQ(FirstByte(), original ^ 0xAA) << "after both";
    ASSERT_TRUE(_ttd->SeekTo({first, 0}));
    EXPECT_EQ(FirstByte(), original) << "and back again";
}

TEST_F(TTDMediaUndo_Test, AWriteAfterASeekBackDropsTheFuture)
{
    const uint8_t original = FirstByte();
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    const uint64_t first = _context->emulatorState.frame_counter;
    ASSERT_NO_FATAL_FAILURE(GuestWrite(static_cast<uint8_t>(original ^ 0x55)));
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    _ttd->StopRecording();

    // Back in the past, the machine changes the disk again (a new branch): the version above is gone
    ASSERT_TRUE(_ttd->SeekTo({first, 0}));
    ASSERT_EQ(FirstByte(), original);
    std::vector<MediaVersionInfo> versions;
    _context->pMediaManager->CurrentVersions(versions);
    uint64_t at = 0;
    for (const MediaVersionInfo& v : versions)
        if (v.slot == "fdd.a")
            at = v.version;
    ASSERT_NO_FATAL_FAILURE(GuestWrite(0x77));
    EXPECT_FALSE(_context->pMediaManager->SetHead("fdd.a", at + 2)) << "the old future never was on this branch";
    EXPECT_TRUE(_context->pMediaManager->SetHead("fdd.a", at));
    EXPECT_EQ(FirstByte(), original);
}

TEST_F(TTDMediaUndo_Test, AWriteProtectToggleWhileRecordingIsAnEventAndASeekSetsTheSwitch)
{
    MediaManager& manager = *_context->pMediaManager;
    FDD* drive = _context->coreState.diskDrives[0];
    ASSERT_FALSE(drive->isWriteProtect());
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    const uint64_t before = _context->emulatorState.frame_counter;

    ASSERT_TRUE(manager.SetWriteProtect("fdd.a", true).Ok());
    EXPECT_FALSE(drive->isWriteProtect()) << "never inside a frame: it waits for the boundary";
    EXPECT_TRUE(manager.Info("fdd.a")->writeProtect) << "the switch as the user set it";
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    const uint64_t landed = _context->emulatorState.frame_counter;
    EXPECT_TRUE(drive->isWriteProtect()) << "landed at the boundary";
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    EXPECT_TRUE(_ttd->IsRecording()) << "the recording goes on";
    _ttd->StopRecording();

    size_t changes = 0;
    for (const ttd::TTDEvent& ev : _ttd->GetEngine().Events().Events())
        changes += ev.kind == ttd::TTDEventKind::MediaChange;
    EXPECT_EQ(changes, 1u) << "one event, where it landed (frame " << landed << ")";

    ASSERT_TRUE(_ttd->SeekTo({before, 0}));
    EXPECT_FALSE(drive->isWriteProtect()) << "before the toggle";
    EXPECT_FALSE(manager.Info("fdd.a")->writeProtect);
    ASSERT_TRUE(_ttd->SeekTo({landed + 1, 0}));
    EXPECT_TRUE(drive->isWriteProtect()) << "after it";
    EXPECT_TRUE(manager.Info("fdd.a")->writeProtect);
}

/// F13: a write-through disk the history sets back turns into a session one. The rollback and every later write stay
/// in memory; the file keeps what the recording wrote until the user saves
TEST_F(TTDMediaUndo_Test, AWriteThroughDiskSetBackKeepsItsFileUntilSaved)
{
    const std::string image = TestPathHelper::GetUniqueTestScratchPath("ttd-writethrough.img");
    {
        std::ofstream out(image, std::ios::binary | std::ios::trunc);
        out << std::string(64 * 512, '\x11');
    }
    MediaSource source;
    source.path = image;
    InsertOptions options;
    options.immediate = true;
    options.access = AccessMode::WriteThrough;
    MediaManager& manager = *_context->pMediaManager;
    ASSERT_TRUE(manager.Insert("ide0.master", source, options).Ok());
    Medium* medium = manager.GetMedium("ide0.master");
    ASSERT_NE(medium, nullptr);
    ASSERT_EQ(medium->Access(), AccessMode::WriteThrough);
    auto fileByte = [&]() {
        std::ifstream in(image, std::ios::binary);
        in.seekg(5 * 512);
        return static_cast<uint8_t>(in.get());
    };
    auto mediumByte = [&]() {
        uint8_t sector[512] = {};
        manager.GetMedium("ide0.master")->ReadTap()->Base().ReadSector(5, sector);
        return sector[0];
    };

    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    const uint64_t before = _context->emulatorState.frame_counter;
    const std::string written(512, '\x22');
    ASSERT_TRUE(medium->Block()->WriteSector(5, reinterpret_cast<const uint8_t*>(written.data())));  // the guest
    _context->pMediaManager->NoteWrite("ide0.master", "test write");
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    _ttd->StopRecording();
    ASSERT_EQ(fileByte(), 0x22) << "written through while recording";

    ASSERT_TRUE(_ttd->SeekTo({before, 0}));
    EXPECT_EQ(manager.GetMedium("ide0.master")->Access(), AccessMode::Session) << "set back: a session medium now";
    EXPECT_EQ(mediumByte(), 0x11) << "the medium as it was there";
    EXPECT_EQ(fileByte(), 0x22) << "the file keeps its content until saved";
    EXPECT_GT(manager.GetMedium("ide0.master")->ChangedUnits(), 0u) << "the rollback is an unsaved change";
    std::remove(image.c_str());
}

/// F11 / F12: the same file inserted again is another history. Where the recording's disk was untouched it is the
/// same content (the identity is a hash of the file), and a seek takes it; after the recording's write it is not
/// that disk, and the seek says so
TEST_F(TTDMediaUndo_Test, TheSameDiskInsertedAgainIsTheSameOnlyWhereItWasUntouched)
{
    const uint8_t original = FirstByte();
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    const uint64_t first = _context->emulatorState.frame_counter;
    ASSERT_NO_FATAL_FAILURE(GuestWrite(static_cast<uint8_t>(original ^ 0x55)));
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    const uint64_t written = _context->emulatorState.frame_counter;
    std::string error;
    const std::string disk = (TestPathHelper::FindProjectRoot() / "testdata/loaders/trd/atarin.trd").string();
    ASSERT_TRUE(_emulator->LoadDisk(disk, 0, &error)) << error;  // an event of the recording
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    _ttd->StopRecording();

    auto mediaIssues = [this]() {
        size_t n = 0;
        for (const ttd::TTDRestoreIssue& issue : _ttd->LastEngineCheck().issues)
            n += issue.kind == ttd::TTDRestoreIssueKind::MediaVersionDiffers && issue.detail.rfind("fdd.a", 0) == 0;
        return n;
    };
    const uint32_t mid = _ttd->FrameSpan() / 2;
    ASSERT_TRUE(_ttd->SeekTo({first, mid}));
    EXPECT_EQ(mediaIssues(), 0u) << "untouched there: the same content " << _ttd->LastEngineCheck().message;
    ASSERT_TRUE(_ttd->SeekTo({written, mid}));
    EXPECT_EQ(mediaIssues(), 1u) << "written there: not this insert's history";
}

namespace
{
/// A recording on atarin.trd during which another disk went into the drive (an event of the recording): before that
/// frame the drive holds a disk the recording never had there
class TTDMediaCut_Test : public TTDMediaUndo_Test
{
protected:
    uint64_t _before = 0;

    void SetUp() override
    {
        TTDMediaUndo_Test::SetUp();
        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
        _before = _context->emulatorState.frame_counter;
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        std::string error;
        const std::string other = (TestPathHelper::FindProjectRoot() / "testdata/loaders/trd/EyeAche.trd").string();
        ASSERT_TRUE(_emulator->LoadDisk(other, 0, &error)) << error;
        _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        ASSERT_TRUE(_ttd->SeekTo({_before, 0}));
    }
    ttd::TTDReply Resume(const std::string& media)
    {
        std::map<std::string, std::string> options;
        if (!media.empty())
            options["media"] = media;
        return ttd::TTDControl(_context).Execute({"resume", options});
    }
    std::vector<coordinator::OperationLogEntry> Cuts() const
    {
        std::vector<coordinator::OperationLogEntry> cuts;
        for (const coordinator::OperationLogEntry& e : _emulator->GetOperationLog().Entries())
            if (e.kind == coordinator::OpKind::TimelineCut)
                cuts.push_back(e);
        return cuts;
    }
};
}  // namespace

/// M5, F11: Rec From Here where the drive holds a disk the recording did not have there. "ask" (Qt) names the slot
/// and changes nothing; "keep" goes on with the disk as it is, and the decision is in the operation log
TEST_F(TTDMediaCut_Test, AMediumThatCannotGoBackIsAskedAboutAndKeptOnRequest)
{
    const std::vector<ttd::TTDRestoreIssue> plan = _ttd->PlanCut({_before, 0});
    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(plan[0].detail.rfind("fdd.a: another medium", 0), 0u) << plan[0].detail;

    const ttd::TTDReply asked = Resume("ask");
    ASSERT_TRUE(asked.Ok()) << asked.message;
    EXPECT_FALSE(asked.body.find("resumed")->b);
    ASSERT_NE(asked.body.find("needs_decision"), nullptr);
    EXPECT_TRUE(asked.body.find("needs_decision")->b);
    ASSERT_EQ(asked.body.find("media")->items.size(), 1u);
    EXPECT_EQ(asked.body.find("media")->items[0].find("slot")->s, "fdd.a");
    EXPECT_FALSE(_ttd->IsRecording()) << "asking changes nothing";
    EXPECT_TRUE(Cuts().empty());

    Medium* disk = _context->pMediaManager->GetMedium("fdd.a");
    const ttd::TTDReply kept = Resume("keep");
    ASSERT_TRUE(kept.Ok()) << kept.message;
    EXPECT_TRUE(kept.body.find("resumed")->b);
    EXPECT_TRUE(_ttd->IsRecording());
    EXPECT_EQ(kept.body.find("media")->items[0].find("decision")->s, "keep");
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    EXPECT_EQ(_context->pMediaManager->GetMedium("fdd.a"), disk) << "the disk stays";
    const auto cuts = Cuts();
    ASSERT_EQ(cuts.size(), 1u);
    EXPECT_NE(cuts[0].detail.find("fdd.a"), std::string::npos);
    EXPECT_NE(cuts[0].detail.find("keep"), std::string::npos) << cuts[0].detail;
    _ttd->StopRecording();
}

/// Where nobody can be asked (WebAPI, CLI, scripts: no "media") the slot is emptied, never set back onto a disk
/// the recording did not have; the eject is an event of the recording that goes on
TEST_F(TTDMediaCut_Test, WhereNobodyCanBeAskedTheSlotIsEmptied)
{
    const ttd::TTDReply resumed = Resume("");
    ASSERT_TRUE(resumed.Ok()) << resumed.message;
    EXPECT_TRUE(resumed.body.find("resumed")->b);
    EXPECT_EQ(resumed.body.find("media")->items[0].find("decision")->s, "empty");
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    EXPECT_EQ(_context->pMediaManager->GetMedium("fdd.a"), nullptr) << "the slot is empty";
    EXPECT_TRUE(_ttd->IsRecording());
    ASSERT_EQ(Cuts().size(), 1u);
    EXPECT_NE(Cuts()[0].detail.find("empty"), std::string::npos) << Cuts()[0].detail;
    _ttd->StopRecording();
}
