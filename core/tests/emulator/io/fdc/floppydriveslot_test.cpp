// FloppyDriveSlot: the floppy drives as media manager slots (fdd.a-d)

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/floppydriveslot.h"
#include "emulator/media/mediamanager.h"

namespace
{
    std::string Fixture(const char* relative)
    {
        const auto path = TestPathHelper::FindProjectRoot() / relative;
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    const std::string kEyeAche = "testdata/loaders/trd/EyeAche.trd";
    const std::string kSatisfaction = "testdata/loaders/trd/Satisfaction.trd";
}  // namespace

/// The slots follow the active controller: four for the Beta Disk WD1793,
/// two on the +3, whose uPD765 serves drives A and B only
TEST(FloppyDriveSlot_Test, SlotsFollowTheActiveController)
{
    Emulator* pentagon = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(pentagon, nullptr);
    MediaManager& manager = *pentagon->GetContext()->pMediaManager;
    for (const char* id : {"fdd.a", "fdd.b", "fdd.c", "fdd.d"})
        EXPECT_TRUE(manager.HasSlot(id)) << id;
    const auto info = manager.Info("fdd.b");
    ASSERT_TRUE(info);
    EXPECT_EQ(info->descriptor.kind, MediaKind::Floppy);
    EXPECT_EQ(info->descriptor.label, "Drive B");
    EXPECT_TRUE(info->descriptor.acceptsFolder);
    EXPECT_EQ(info->descriptor.swapDelayMs, 2000u);
    EmulatorTestHelper::CleanupEmulator(pentagon);

    Emulator* plus3 = EmulatorTestHelper::CreateStandardEmulator("PLUS3", LoggerLevel::LogError);
    ASSERT_NE(plus3, nullptr);
    MediaManager& plus3Media = *plus3->GetContext()->pMediaManager;
    EXPECT_TRUE(plus3Media.HasSlot("fdd.a"));
    EXPECT_TRUE(plus3Media.HasSlot("fdd.b"));
    EXPECT_FALSE(plus3Media.HasSlot("fdd.c")) << "the +3 has no drive C";
    EXPECT_FALSE(plus3Media.HasSlot("fdd.d"));
    std::string error;
    EXPECT_FALSE(plus3->LoadDisk(Fixture(kEyeAche.c_str()), 2, &error));
    EXPECT_NE(error.find("not present"), std::string::npos) << error;
    EmulatorTestHelper::CleanupEmulator(plus3);
}

/// The research bug (research.md §3): ejecting drive B also ejected the
/// WD1793's selected drive (A), and no eject ever freed the image
TEST(FloppyDriveSlot_Test, EjectTakesOnlyItsDriveAndFreesTheDisk)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    MediaManager& manager = *context->pMediaManager;

    ASSERT_TRUE(emulator->LoadDisk(Fixture(kEyeAche.c_str()), 0));
    ASSERT_TRUE(emulator->LoadDisk(Fixture(kSatisfaction.c_str()), 1));
    EXPECT_EQ(context->coreState.diskFilePaths[1], Fixture(kSatisfaction.c_str()));
    EXPECT_EQ(manager.GetMedium("fdd.a")->Floppy(), context->coreState.diskDrives[0]->getDiskImage())
        << "the drive holds the manager's disk";

    ASSERT_TRUE(emulator->EjectDisk(1));
    EXPECT_EQ(context->coreState.diskDrives[1]->getDiskImage(), nullptr);
    EXPECT_EQ(manager.GetMedium("fdd.b"), nullptr) << "the medium left the manager: freed";
    EXPECT_TRUE(context->coreState.diskFilePaths[1].empty());
    ASSERT_NE(context->coreState.diskDrives[0]->getDiskImage(), nullptr) << "drive A keeps its disk";
    EXPECT_EQ(context->coreState.diskFilePaths[0], Fixture(kEyeAche.c_str()));

    EXPECT_TRUE(emulator->EjectDisk(1)) << "an empty drive is not an error";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A written disk stays in unless the caller accepts losing the writes
TEST(FloppyDriveSlot_Test, DirtyDiskNeedsForceToLeave)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(emulator->LoadDisk(Fixture(kEyeAche.c_str()), 0));

    DiskImage* disk = context->coreState.diskDrives[0]->getDiskImage();
    const std::vector<uint8_t> sector(256, 0x42);
    disk->getTrack(5)->writeSectorData(0, sector.data(), sector.size());
    ASSERT_TRUE(context->pMediaManager->Info("fdd.a")->dirty);

    std::string error;
    EXPECT_FALSE(emulator->EjectDisk(0, /*force*/ false, &error));
    EXPECT_NE(error.find("unsaved"), std::string::npos) << error;
    EXPECT_NE(context->coreState.diskDrives[0]->getDiskImage(), nullptr);
    EXPECT_TRUE(emulator->EjectDisk(0, /*force*/ true));
    EXPECT_EQ(context->coreState.diskDrives[0]->getDiskImage(), nullptr);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A read-only medium and the slot's switch both open the disk's write-protect tab
TEST(FloppyDriveSlot_Test, ReadOnlyAndTheSwitchProtectTheDisk)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    MediaManager& manager = *context->pMediaManager;
    FDD* driveA = context->coreState.diskDrives[0];

    MediaSource source;
    source.path = Fixture(kEyeAche.c_str());
    InsertOptions readOnly;
    readOnly.access = AccessMode::ReadOnly;
    ASSERT_TRUE(manager.Insert("fdd.a", source, readOnly).Ok());
    EXPECT_TRUE(driveA->isWriteProtect());

    InsertOptions writable;
    ASSERT_TRUE(manager.Insert("fdd.a", source, writable).Ok());
    EXPECT_FALSE(driveA->isWriteProtect());

    InsertOptions tab;
    tab.writeProtect = true;
    ASSERT_TRUE(manager.Insert("fdd.a", source, tab).Ok());
    EXPECT_TRUE(driveA->isWriteProtect()) << "the switch alone protects a session disk";

    ASSERT_TRUE(manager.Eject("fdd.a").Ok());
    EXPECT_FALSE(driveA->isWriteProtect()) << "an empty drive senses no tab";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// "Save as" makes the disk stand for the new file; a later plain save goes there
TEST(FloppyDriveSlot_Test, SaveAsRebasesTheDisk)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(emulator->CreateBlankDisk(0));
    EXPECT_EQ(context->coreState.diskFilePaths[0], "<blank>");

    const Emulator::DiskSaveResult noPath = emulator->SaveDisk(0);
    EXPECT_FALSE(noPath.saved) << "a blank disk has no file of its own";

    ScratchFolder folder("floppy-save-as");
    const auto target = folder.Path() / "saved.udi";
    const auto u8 = target.u8string();
    const std::string targetPath(u8.begin(), u8.end());
    const Emulator::DiskSaveResult saved = emulator->SaveDisk(0, targetPath);
    ASSERT_TRUE(saved.saved) << saved.reason;
    EXPECT_EQ(context->coreState.diskFilePaths[0], targetPath);
    EXPECT_EQ(context->pMediaManager->GetMedium("fdd.a")->Source().type, MediaSourceType::File);

    const Emulator::DiskSaveResult again = emulator->SaveDisk(0);
    EXPECT_TRUE(again.saved) << again.reason;
    EXPECT_EQ(again.savedPath, targetPath);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// [MEDIA] fdd.a = <image> puts the disk in before the first reset
TEST(FloppyDriveSlot_Test, ConfiguredDiskIsInAtStart)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    MediaSetEntry entry;
    entry.slotId = "fdd.b";
    entry.source.path = Fixture(kEyeAche.c_str());
    const auto problems = emulator->GetContext()->pMediaManager->ApplyConfiguredMedia({entry});
    EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems.front());
    EXPECT_NE(emulator->GetContext()->coreState.diskDrives[1]->getDiskImage(), nullptr);
    EXPECT_EQ(emulator->GetContext()->pMediaManager->Info("fdd.b")->format, "trd");
    EmulatorTestHelper::CleanupEmulator(emulator);
}
