// MediaManager: slots, insert / eject, the queue, swap delay, the "one source,
// one slot" rule, parking, the TTD recording guard
// (docs/inprogress/2026-09-28-storage-manager/technical-design.md §2-§3)

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/mediamanager.h"

namespace
{
    /// A block slot that records what happened to it
    class FakeBlockSlot : public IMediaSlot
    {
    public:
        explicit FakeBlockSlot(std::string id, uint32_t swapDelayMs = 0)
        {
            _descriptor.id = std::move(id);
            _descriptor.kind = MediaKind::Block;
            _descriptor.label = "fake";
            _descriptor.swapDelayMs = swapDelayMs;
            _descriptor.acceptsFolder = true;
        }

        const SlotDescriptor& Descriptor() const override { return _descriptor; }
        void Attach(Medium& medium) override
        {
            attached = &medium;
            events.push_back("attach");
        }
        void Detach() override
        {
            attached = nullptr;
            events.push_back("detach");
        }
        bool IsBusy() const override { return busy; }
        void SetWriteProtectSwitch(bool on) override { writeProtect = on; }

        Medium* attached = nullptr;
        std::vector<std::string> events;
        bool busy = false;
        bool writeProtect = false;

    private:
        SlotDescriptor _descriptor;
    };

    std::unique_ptr<Medium> MemoryMedium(uint64_t sectors, AccessMode access = AccessMode::Session)
    {
        MediaSource source;
        source.type = MediaSourceType::Blank;
        return MediaFormatRegistry::WrapBlock(source, access, "memory", std::make_unique<MemoryDisk>(sectors));
    }

    std::string MakeImageFile(const char* name, size_t sectors)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const std::vector<char> sector(512, 0x42);
        for (size_t i = 0; i < sectors; i++)
            out.write(sector.data(), static_cast<std::streamsize>(sector.size()));
        return path;
    }

    void WriteSector(Medium& medium, uint64_t lba, uint8_t fill)
    {
        const std::vector<uint8_t> data(512, fill);
        ASSERT_TRUE(medium.Block()->WriteSector(lba, data.data()));
    }
}  // namespace

TEST(MediaManager_Test, RegisterListAndInsertWhileStopped)
{
    MediaManager manager(nullptr);
    FakeBlockSlot slot("sd.test");
    manager.RegisterSlot(slot);

    ASSERT_TRUE(manager.HasSlot("sd.test"));
    auto info = manager.Info("sd.test");
    ASSERT_TRUE(info.has_value());
    EXPECT_FALSE(info->present);

    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(16)).Ok());
    EXPECT_NE(slot.attached, nullptr) << "not running: applied at once";
    info = manager.Info("sd.test");
    EXPECT_TRUE(info->present);
    EXPECT_EQ(info->format, "memory");
    EXPECT_EQ(manager.List().size(), 1u);

    EXPECT_EQ(manager.Insert("nope", MemoryMedium(16)).error, MediaError::UnknownSlot);
    manager.UnregisterSlot("sd.test");
}

TEST(MediaManager_Test, InsertFromAnImageFileAndAccessModes)
{
    MediaManager manager(nullptr);
    FakeBlockSlot slot("sd.test");
    manager.RegisterSlot(slot);
    const std::string path = MakeImageFile("media-raw.img", 8);

    MediaSource source;
    source.path = path;
    InsertOptions readOnly;
    readOnly.access = AccessMode::ReadOnly;
    ASSERT_TRUE(manager.Insert("sd.test", source, readOnly).Ok());
    ASSERT_NE(slot.attached, nullptr);
    const std::vector<uint8_t> data(512, 0x11);
    EXPECT_FALSE(slot.attached->Block()->WriteSector(0, data.data())) << "ReadOnlyGuard refuses";
    EXPECT_FALSE(slot.attached->Block()->IsWritable());

    ASSERT_TRUE(manager.Insert("sd.test", source).Ok()) << "default access: session";
    ASSERT_NE(slot.attached->Session(), nullptr);
    EXPECT_TRUE(slot.attached->Block()->WriteSector(0, data.data()));
    manager.ApplyPending();
    EXPECT_TRUE(manager.Info("sd.test")->dirty);

    std::ifstream file(path, std::ios::binary);
    EXPECT_EQ(file.get(), 0x42) << "the source file never changes in session access";

    MediaSource missing;
    missing.path = path + ".missing";
    EXPECT_EQ(manager.Insert("sd.test", missing).error, MediaError::UnreadableSource);

    ASSERT_TRUE(manager.Eject("sd.test", {/*force*/ true}).Ok());
    manager.UnregisterSlot("sd.test");
    std::remove(path.c_str());
}

TEST(MediaManager_Test, DirtyEjectNeedsForceAndDiscardCleans)
{
    MediaManager manager(nullptr);
    FakeBlockSlot slot("sd.test");
    manager.RegisterSlot(slot);
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(16)).Ok());

    // A MemoryDisk under a session map: the write lands in the map
    WriteSector(*slot.attached, 3, 0x7E);
    manager.ApplyPending();
    EXPECT_EQ(manager.Info("sd.test")->changedUnits, 1u);

    EXPECT_EQ(manager.Eject("sd.test").error, MediaError::Dirty);
    EXPECT_NE(slot.attached, nullptr);

    ASSERT_TRUE(manager.Discard("sd.test").Ok());
    EXPECT_FALSE(manager.Info("sd.test")->dirty);
    ASSERT_TRUE(manager.Eject("sd.test").Ok());
    EXPECT_EQ(slot.attached, nullptr);
    EXPECT_EQ(slot.events, (std::vector<std::string>{"attach", "detach"}));
}

TEST(MediaManager_Test, RunningMachineQueuesAndHonorsSwapDelay)
{
    MediaManager manager(nullptr);
    bool running = false;
    manager.SetApplyNowProbe([&running] { return !running; });
    FakeBlockSlot slot("sd.test", /*swapDelayMs*/ 60);  // 3 frames of 20 ms
    manager.RegisterSlot(slot);
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(16)).Ok());
    Medium* first = slot.attached;

    running = true;
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(32)).Ok());
    EXPECT_EQ(slot.attached, first) << "queued, not applied from the caller's thread";
    EXPECT_TRUE(manager.Info("sd.test")->pending);

    // 60 ms = 3 frames: out at frame boundary 1, empty for frames 1-3, in at boundary 4
    manager.ApplyPending();
    EXPECT_EQ(slot.attached, nullptr) << "boundary 1: the old card is out";
    manager.ApplyPending();
    manager.ApplyPending();
    EXPECT_EQ(slot.attached, nullptr) << "boundaries 2-3: the slot stays empty";
    manager.ApplyPending();
    ASSERT_NE(slot.attached, nullptr) << "boundary 4: the new card is in";
    EXPECT_EQ(slot.attached->Block()->SectorCount(), 32u);
    EXPECT_FALSE(manager.Info("sd.test")->pending);
}

TEST(MediaManager_Test, BusySlotWaitsForTheNextFrame)
{
    MediaManager manager(nullptr);
    bool running = true;
    manager.SetApplyNowProbe([&running] { return !running; });
    FakeBlockSlot slot("sd.test");
    manager.RegisterSlot(slot);
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(8)).Ok());

    slot.busy = true;
    manager.ApplyPending();
    EXPECT_EQ(slot.attached, nullptr) << "a transfer in flight: wait";
    slot.busy = false;
    manager.ApplyPending();
    EXPECT_NE(slot.attached, nullptr);
}

TEST(MediaManager_Test, OneSourceInOneSlotUnlessBothReadOnly)
{
    MediaManager manager(nullptr);
    FakeBlockSlot sd("sd.test");
    FakeBlockSlot hdd("ide0.master");
    manager.RegisterSlot(sd);
    manager.RegisterSlot(hdd);
    const std::string path = MakeImageFile("media-shared.img", 4);  // the ATM3 wc.img case
    MediaSource source;
    source.path = path;

    ASSERT_TRUE(manager.Insert("sd.test", source).Ok());
    MediaResult second = manager.Insert("ide0.master", source);
    EXPECT_EQ(second.error, MediaError::InUse);
    EXPECT_NE(second.message.find("sd.test"), std::string::npos) << "names the slot that has it";

    InsertOptions readOnly;
    readOnly.access = AccessMode::ReadOnly;
    ASSERT_TRUE(manager.Insert("sd.test", source, readOnly).Ok());
    EXPECT_TRUE(manager.Insert("ide0.master", source, readOnly).Ok()) << "two read-only users may share";

    ASSERT_TRUE(manager.Insert("sd.test", source, readOnly).Ok()) << "the same slot may take its own source again";
    manager.UnregisterSlot("sd.test");
    manager.UnregisterSlot("ide0.master");
    std::remove(path.c_str());
}

TEST(MediaManager_Test, UnregisterParksAndRegisterRestoresSessionWrites)
{
    MediaManager manager(nullptr);
    {
        FakeBlockSlot card("sd.ngs");
        manager.RegisterSlot(card);
        ASSERT_TRUE(manager.Insert("sd.ngs", MemoryMedium(8)).Ok());
        WriteSector(*card.attached, 2, 0x5A);
        manager.UnregisterSlot("sd.ngs");  // the card is removed
        EXPECT_EQ(card.attached, nullptr);
    }
    EXPECT_FALSE(manager.HasSlot("sd.ngs"));

    FakeBlockSlot again("sd.ngs");  // the card is fitted again
    manager.RegisterSlot(again);
    ASSERT_NE(again.attached, nullptr);
    uint8_t sector[512];
    ASSERT_TRUE(again.attached->Block()->ReadSector(2, sector));
    EXPECT_EQ(sector[0], 0x5A) << "session writes survived the parking";
    manager.UnregisterSlot("sd.ngs");
}

TEST(MediaManager_Test, KindAndFolderChecks)
{
    MediaManager manager(nullptr);
    FakeBlockSlot slot("sd.test");
    manager.RegisterSlot(slot);

    MediaSource folder;
    folder.type = MediaSourceType::Folder;
    folder.path = TestPathHelper::GetTestScratchPath(".");
    InsertOptions writeThrough;
    writeThrough.access = AccessMode::WriteThrough;
    EXPECT_EQ(manager.Insert("sd.test", folder, writeThrough).error, MediaError::KindMismatch)
        << "a folder is never written";
    manager.UnregisterSlot("sd.test");
}

TEST(MediaManager_Test, ExportWritesTheGuestView)
{
    MediaManager manager(nullptr);
    FakeBlockSlot slot("sd.test");
    manager.RegisterSlot(slot);
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(4)).Ok());
    WriteSector(*slot.attached, 1, 0xC3);

    const std::string path = TestPathHelper::GetUniqueTestScratchPath("media-export.img");
    ASSERT_TRUE(manager.Export("sd.test", path).Ok());
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_EQ(bytes.size(), 4u * 512u);
    EXPECT_EQ(static_cast<uint8_t>(bytes[512]), 0xC3);
    EXPECT_EQ(static_cast<uint8_t>(bytes[0]), 0x00);

    bool running = true;
    manager.SetApplyNowProbe([&running] { return !running; });
    EXPECT_EQ(manager.Export("sd.test", path).error, MediaError::NotSupported) << "a running machine is paused first";
    manager.SetApplyNowProbe(nullptr);
    ASSERT_TRUE(manager.Eject("sd.test", {/*force*/ true}).Ok());
    manager.UnregisterSlot("sd.test");
    std::remove(path.c_str());
}

/// The media set is fixed while TTD records (integration-ttd-snapshots.md §2)
TEST(MediaManager_Test, RecordingRefusesChangesUnlessAskedToEndIt)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_NE(context->pMediaManager, nullptr) << "every emulator has a media manager";
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);

    MediaManager& manager = *context->pMediaManager;
    FakeBlockSlot slot("sd.test");
    manager.RegisterSlot(slot);
    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());

    EXPECT_EQ(manager.Insert("sd.test", MemoryMedium(8)).error, MediaError::Recording);
    EXPECT_EQ(slot.attached, nullptr);

    InsertOptions end;
    end.endRecording = true;
    ASSERT_TRUE(manager.Insert("sd.test", MemoryMedium(8), end).Ok());
    EXPECT_FALSE(context->pTimeTravelManager->IsRecording());
    EXPECT_NE(slot.attached, nullptr);

    manager.UnregisterSlot("sd.test");
    EmulatorTestHelper::CleanupEmulator(emulator);
}
