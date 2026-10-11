/// @file ttdmediaundo_test.cpp
/// @brief A seek sets every medium to the version it had there (TTD gaps 11-12): the tracks and sectors the guest
/// wrote, the write-protect switch the user toggled. The media manager's undo log keeps the units as they were while a
/// history exists; the restore sets each medium before the devices load.
///
/// Boots a machine and records a few frames: slower than the 50 ms guideline.

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
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
