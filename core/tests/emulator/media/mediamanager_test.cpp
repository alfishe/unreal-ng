// MediaManager: slots, insert / eject, the queue, swap delay, the "one source,
// one slot" rule, parking, the TTD recording guard
// (docs/inprogress/2026-09-28-storage-manager/technical-design.md §2-§3)

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/rawimage.h"
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
        SlotDescriptor& MutableDescriptor() { return _descriptor; }
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

    /// A minimal FAT image: a VBR (raw, or behind an MBR when `mbr`) with a
    /// BPB of the requested flavour - enough for ProbeFatType, nothing more
    std::string MakeFatImageFile(const char* name, FatType fs, bool mbr)
    {
        std::vector<uint8_t> vbr(512, 0);
        vbr[0] = 0xEB;
        vbr[1] = 0x3C;
        vbr[2] = 0x90;
        std::memcpy(&vbr[3], "MSDOS5.0", 8);
        const uint16_t bytesPerSector = 512;
        std::memcpy(&vbr[11], &bytesPerSector, 2);
        vbr[13] = fs == FatType::Fat32 ? 8 : 1;  // sectors per cluster
        const uint16_t reserved = 1;
        std::memcpy(&vbr[14], &reserved, 2);
        vbr[16] = 2;  // FAT copies
        const uint16_t rootEntries = fs == FatType::Fat32 ? 0 : 512;
        std::memcpy(&vbr[17], &rootEntries, 2);
        const uint16_t total16 = fs == FatType::Fat32 ? 0 : 16384;  // 16 KiB of 1-sector clusters: over FAT12's 4085
        std::memcpy(&vbr[19], &total16, 2);
        vbr[21] = 0xF8;
        const uint16_t fatSectors16 = fs == FatType::Fat32 ? 0 : 2;
        std::memcpy(&vbr[22], &fatSectors16, 2);
        if (fs == FatType::Fat32)
        {
            const uint32_t total32 = 8u * 70000;  // 70 000 clusters of 8 sectors: over FAT32's 65 525 floor
            std::memcpy(&vbr[32], &total32, 4);
            const uint32_t fatSectors32 = 8;
            std::memcpy(&vbr[36], &fatSectors32, 4);
            std::memcpy(&vbr[0x52], "FAT32   ", 8);
        }
        else
        {
            std::memcpy(&vbr[0x36], "FAT16   ", 8);
        }
        vbr[510] = 0x55;
        vbr[511] = 0xAA;

        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (mbr)
        {
            std::vector<uint8_t> table(512, 0);
            uint8_t* entry = &table[446];
            entry[4] = fs == FatType::Fat32 ? 0x0B : 0x06;
            const uint32_t start = 1;
            std::memcpy(entry + 8, &start, 4);
            const uint32_t count = 64;
            std::memcpy(entry + 12, &count, 4);
            table[510] = 0x55;
            table[511] = 0xAA;
            out.write(reinterpret_cast<const char*>(table.data()), static_cast<std::streamsize>(table.size()));
        }
        out.write(reinterpret_cast<const char*>(vbr.data()), static_cast<std::streamsize>(vbr.size()));
        const std::vector<char> pad(63 * 512, 0);
        out.write(pad.data(), static_cast<std::streamsize>(pad.size()));
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

    ASSERT_TRUE(manager.Eject("sd.test", {Disposition::Discard}).Ok());
    manager.UnregisterSlot("sd.test");
    std::remove(path.c_str());
}

/// Paths are UTF-8 strings everywhere; FileHelper turns them into native paths
/// (UTF-16 on Windows), so a non-ASCII image name opens on every host
TEST(MediaManager_Test, NonAsciiImagePathOpens)
{
    MediaManager manager(nullptr);
    FakeBlockSlot slot("sd.test");
    manager.RegisterSlot(slot);

    const std::string path = TestPathHelper::GetUniqueTestScratchPath("карта-диск.img");
    {
        std::ofstream out(FileHelper::ToFsPath(path), std::ios::binary | std::ios::trunc);
        const std::vector<char> sector(512, 0x24);
        out.write(sector.data(), static_cast<std::streamsize>(sector.size()));
    }
    MediaSource source;
    source.path = path;
    MediaResult result = manager.Insert("sd.test", source);
    ASSERT_TRUE(result.Ok()) << result.message;
    uint8_t sector[512];
    ASSERT_TRUE(slot.attached->Block()->ReadSector(0, sector));
    EXPECT_EQ(sector[0], 0x24);

    const std::string exported = TestPathHelper::GetUniqueTestScratchPath("экспорт.img");
    ASSERT_TRUE(manager.Export("sd.test", exported).Ok());
    EXPECT_TRUE(FileHelper::FileExists(exported));

    ASSERT_TRUE(manager.Eject("sd.test").Ok());
    manager.UnregisterSlot("sd.test");
    std::error_code ec;
    std::filesystem::remove(FileHelper::ToFsPath(path), ec);
    std::filesystem::remove(FileHelper::ToFsPath(exported), ec);
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

/// A card fitted after the machine was created (a sound card switch) gets
/// what the config states for its slot; a medium parked for the slot wins,
/// and before the configured media went in nothing is inserted twice
TEST(MediaManager_Test, SlotRegisteredLaterGetsItsConfiguredMedium)
{
    MediaManager manager(nullptr);
    const std::string path = MakeImageFile("media-configured.img", 8);
    MediaSetEntry entry;
    entry.slotId = "sd.ngs";
    entry.source.path = path;
    entry.access = AccessMode::ReadOnly;
    entry.writeProtect = true;
    entry.legacy = true;

    {
        FakeBlockSlot early("sd.ngs");
        manager.RegisterSlot(early);  // at creation, before the configured media
        EXPECT_EQ(early.attached, nullptr) << "nothing configured yet";
        manager.UnregisterSlot("sd.ngs");
    }

    EXPECT_TRUE(manager.ApplyConfiguredMedia({entry}).empty()) << "a legacy entry for a missing slot is quiet";

    {
        FakeBlockSlot fitted("sd.ngs");
        manager.RegisterSlot(fitted);
        ASSERT_NE(fitted.attached, nullptr) << "fitted later: the configured medium goes in";
        EXPECT_EQ(fitted.attached->Source().path, path);
        EXPECT_EQ(fitted.attached->Access(), AccessMode::ReadOnly);
        EXPECT_TRUE(manager.Info("sd.ngs")->writeProtect);

        ASSERT_TRUE(manager.Insert("sd.ngs", MemoryMedium(8), {}).Ok());
        manager.UnregisterSlot("sd.ngs");  // the memory disk is parked
    }

    FakeBlockSlot again("sd.ngs");
    manager.RegisterSlot(again);
    ASSERT_NE(again.attached, nullptr);
    EXPECT_EQ(again.attached->Format(), "memory") << "the parked medium comes back, not the configured one";
    manager.UnregisterSlot("sd.ngs");
    std::remove(path.c_str());
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

/// BUGS.md #1: a slot whose controller reads one FAT flavour only - folder
/// volumes are built in it (the default clamps into the matrix), an explicit
/// request for another flavour is a caller error, and an inserted image of
/// another flavour is refused by the sector-0 probe
TEST(MediaManager_Test, Fat32OnlySlotBuildsFoldersAsFat32AndChecksImages)
{
    MediaManager manager(nullptr);
    FakeBlockSlot slot("sd.test");
    slot.MutableDescriptor().fsCompatibility = {FatType::Fat32};
    manager.RegisterSlot(slot);

    ScratchFolder files("matrix-fat32");
    files.File("boot.$C", "boot");
    MediaSource source;
    const auto u8 = files.Path().u8string();
    source.path = std::string(u8.begin(), u8.end());

    // The default Fat16 clamps into the matrix: the folder becomes FAT32
    ASSERT_TRUE(manager.Insert("sd.test", source).Ok());
    EXPECT_EQ(manager.Info("sd.test")->format, "folder-fat32");

    InsertOptions fat16;
    fat16.fs = FatType::Fat16;
    const MediaResult refused = manager.Insert("sd.test", source, fat16);
    EXPECT_EQ(refused.error, MediaError::BadRequest);
    EXPECT_NE(refused.message.find("fat32"), std::string::npos) << refused.message;

    MediaSource fat16Image;
    fat16Image.path = MakeFatImageFile("matrix-fat16.img", FatType::Fat16, false);
    const MediaResult refusedImage = manager.Insert("sd.test", fat16Image);
    EXPECT_EQ(refusedImage.error, MediaError::BadRequest);
    EXPECT_NE(refusedImage.message.find("fat16 volume"), std::string::npos) << refusedImage.message;

    MediaSource fat32Image;
    fat32Image.path = MakeFatImageFile("matrix-fat32-mbr.img", FatType::Fat32, true);
    ASSERT_TRUE(manager.Insert("sd.test", fat32Image).Ok()) << "FAT32 behind an MBR passes the probe";

    // A non-FAT image is none of the matrix's business: the guest may format it
    MediaSource raw;
    raw.path = MakeImageFile("matrix-raw.img", 4);
    ASSERT_TRUE(manager.Insert("sd.test", raw).Ok());
    manager.UnregisterSlot("sd.test");
}

/// BUGS.md #2: a folder over the FAT16 ceiling becomes FAT32 when the slot
/// can read it; a single-flavour slot keeps the honest error
TEST(MediaManager_Test, OversizedFolderSwitchesToFAT32WhenAllowed)
{
    MediaManager manager(nullptr);
    FakeBlockSlot slot("sd.test");
    manager.RegisterSlot(slot);  // no matrix: both flavours

    ScratchFolder files("matrix-oversize");
    files.File("big.trd", std::string(1000, 'x'));
    MediaSource source;
    const auto u8 = files.Path().u8string();
    source.path = std::string(u8.begin(), u8.end());
    InsertOptions huge;
    huge.freeBytes = 3ull * 1024 * 1024 * 1024;  // over FAT16's 2 GiB ceiling

    const MediaResult inserted = manager.Insert("sd.test", source, huge);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    EXPECT_EQ(manager.Info("sd.test")->format, "folder-fat32");
    bool noted = false;
    for (const std::string& line : inserted.report)
        noted = noted || line.find("FAT32") != std::string::npos;
    EXPECT_TRUE(noted) << "the switch is reported";

    FakeBlockSlot fat16Only("sd.f16");
    fat16Only.MutableDescriptor().fsCompatibility = {FatType::Fat16};
    manager.RegisterSlot(fat16Only);
    const MediaResult refused = manager.Insert("sd.f16", source, huge);
    EXPECT_EQ(refused.error, MediaError::DoesNotFit);
    EXPECT_NE(refused.message.find("does not fit a FAT16"), std::string::npos) << refused.message;
    manager.UnregisterSlot("sd.f16");
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
    ASSERT_TRUE(manager.Eject("sd.test", {Disposition::Discard}).Ok());
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

/// A host folder in a block slot: a FAT volume under a session layer; the
/// folder never changes; the export carries the guest's writes (technical design §5-§6)
TEST(MediaManager_Test, FolderBecomesAFatVolumeAndStaysUntouched)
{
    ScratchFolder folder("media-folder");
    folder.File("SD_BOOT.$C", "boot code");
    folder.File("games/Игра.trd", std::string(3000, 'g'));
    folder.File(".DS_Store", "service");
    folder.File(".gitignore", "service");
    folder.File(".unreal-media.yaml", "label: MY CARD\ncodepage: cp1251\n");

    MediaManager manager(nullptr);
    FakeBlockSlot slot("sd.test");
    manager.RegisterSlot(slot);
    MediaSource source;
    const auto u8 = folder.Path().u8string();
    source.path = std::string(u8.begin(), u8.end());
    InsertOptions small;
    small.freeBytes = 1024 * 1024;  // the export below writes the whole volume
    const MediaResult inserted = manager.Insert("sd.test", source, small);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    EXPECT_EQ(manager.Info("sd.test")->format, "folder-fat16");
    bool reportedService = false;
    bool reportedOsNoise = false;
    for (const std::string& line : inserted.report)
    {
        reportedService = reportedService || line.find(".gitignore: skipped, service (vcs)") != std::string::npos;
        reportedOsNoise = reportedOsNoise || line.find(".DS_Store") != std::string::npos;
    }
    EXPECT_TRUE(reportedService) << "skipped entries are reported";
    EXPECT_FALSE(reportedOsNoise) << "host-OS housekeeping is filtered without a trace";

    // The guest's view: an independent FAT reader over the slot's block device
    Medium* medium = slot.attached;
    ASSERT_NE(medium, nullptr);
    FatVolumeReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(*medium->Block(), CodePage::Cp1251, &error)) << error;
    EXPECT_EQ(reader.Label(), "MY CARD") << "the manifest's label";
    std::vector<FatDirEntryInfo> games;
    ASSERT_TRUE(reader.List("/games", games));
    ASSERT_EQ(games.size(), 1u);
    EXPECT_EQ(games[0].shortName, "ИГРА.TRD") << "the manifest's code page for short names";

    // A guest write lands in the session layer; the folder stays as it was
    const std::vector<uint8_t> junk(512, 0xEE);
    ASSERT_TRUE(medium->Block()->WriteSector(medium->Block()->SectorCount() - 1, junk.data()));
    manager.ApplyPending();
    EXPECT_TRUE(manager.Info("sd.test")->dirty);
    std::ifstream boot(folder.Path() / "SD_BOOT.$C");
    EXPECT_EQ(std::string((std::istreambuf_iterator<char>(boot)), std::istreambuf_iterator<char>()), "boot code");

    // Export: the guest's view as an image, readable by the independent reader
    const std::string image = TestPathHelper::GetUniqueTestScratchPath("folder-export.img");
    ASSERT_TRUE(manager.Export("sd.test", image).Ok());
    auto exported = RawImage::Open(image, RawImage::Access::ReadOnly);
    ASSERT_NE(exported, nullptr);
    FatVolumeReader again;
    ASSERT_TRUE(again.Open(*exported, CodePage::Cp1251));
    std::vector<uint8_t> data;
    ASSERT_TRUE(again.ReadFile("/games/Игра.trd", data));
    EXPECT_EQ(data.size(), 3000u);
    uint8_t last[512];
    ASSERT_TRUE(exported->ReadSector(exported->SectorCount() - 1, last));
    EXPECT_EQ(last[0], 0xEE) << "the export carries the guest's write";

    exported.reset();
    std::remove(image.c_str());
    ASSERT_TRUE(manager.Eject("sd.test", {Disposition::Discard}).Ok());
    manager.UnregisterSlot("sd.test");
}

TEST(MediaManager_Test, Fat32FolderVolumeOnRequest)
{
    ScratchFolder folder("media-folder-fat32");
    folder.File("a.bin", "a");
    MediaManager manager(nullptr);
    FakeBlockSlot slot("sd.test");
    manager.RegisterSlot(slot);
    MediaSource source;
    const auto u8 = folder.Path().u8string();
    source.path = std::string(u8.begin(), u8.end());

    InsertOptions fat32;
    fat32.fs = FatType::Fat32;
    ASSERT_TRUE(manager.Insert("sd.test", source, fat32).Ok());
    EXPECT_EQ(manager.Info("sd.test")->format, "folder-fat32");
    manager.UnregisterSlot("sd.test");
}

namespace
{
    std::string FloppyFixture(const char* relative)
    {
        const auto u8 = (TestPathHelper::FindProjectRoot() / relative).u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::string Utf8Path(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::string Slurp(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    /// Change one byte of track 5 the way a controller write does (the track turns dirty)
    void GuestWrite(DiskImage& disk, uint8_t fill)
    {
        const std::vector<uint8_t> sector(256, fill);
        disk.getTrack(5)->writeSectorData(0, sector.data(), sector.size());
    }
}  // namespace

/// Floppies: an export is a copy (the disk keeps its source and its unsaved
/// writes); a save makes it clean; a discard opens the source again.
/// A real machine plus three 640 KB image writes: slower than 50 ms
TEST(MediaManager_Test, FloppyExportSaveAndDiscard)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    MediaManager& manager = *emulator->GetContext()->pMediaManager;
    ScratchFolder folder("floppy-export");
    const auto original = folder.File("game.trd", Slurp(FileHelper::ToFsPath(FloppyFixture("testdata/loaders/trd/EyeAche.trd"))));
    MediaSource source;
    source.path = Utf8Path(original);
    ASSERT_TRUE(manager.Insert("fdd.a", source).Ok());
    GuestWrite(*manager.GetMedium("fdd.a")->Floppy(), 0x5A);
    ASSERT_TRUE(manager.Info("fdd.a")->dirty);

    const auto copy = folder.Path() / "copy.trd";
    ASSERT_TRUE(manager.Export("fdd.a", Utf8Path(copy)).Ok());
    EXPECT_TRUE(manager.Info("fdd.a")->dirty) << "an export is a copy, not a save";
    EXPECT_EQ(manager.Info("fdd.a")->changes, "1 track: 1 sector") << "the sector flags survive the export";
    EXPECT_EQ(manager.Info("fdd.a")->source, source.path) << "the disk still stands for its file";
    EXPECT_NE(Slurp(copy), Slurp(original)) << "the copy carries the write";

    ASSERT_TRUE(manager.Discard("fdd.a").Ok());
    EXPECT_FALSE(manager.Info("fdd.a")->dirty) << "the source, opened again";
    const std::string before = Slurp(original);

    GuestWrite(*manager.GetMedium("fdd.a")->Floppy(), 0x5A);
    ASSERT_TRUE(manager.Save("fdd.a").Ok());
    EXPECT_FALSE(manager.Info("fdd.a")->dirty);
    EXPECT_NE(Slurp(original), before) << "the save wrote into its own file";
    EXPECT_EQ(Slurp(copy), Slurp(original)) << "the same write as the export carried";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// Write-through: the disk goes back to its file at the frame boundary after a write.
/// A real machine plus a 640 KB image write: slower than 50 ms
TEST(MediaManager_Test, FloppyWriteThroughSavesAtTheFrameBoundary)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    MediaManager& manager = *emulator->GetContext()->pMediaManager;
    ScratchFolder folder("floppy-writethrough");
    const auto file = folder.File("game.trd", Slurp(FileHelper::ToFsPath(FloppyFixture("testdata/loaders/trd/EyeAche.trd"))));
    const std::string before = Slurp(file);
    MediaSource source;
    source.path = Utf8Path(file);
    InsertOptions options;
    options.access = AccessMode::WriteThrough;
    ASSERT_TRUE(manager.Insert("fdd.a", source, options).Ok());

    GuestWrite(*manager.GetMedium("fdd.a")->Floppy(), 0x77);
    manager.ApplyPending();
    EXPECT_FALSE(manager.Info("fdd.a")->dirty);
    EXPECT_NE(Slurp(file), before) << "the file has the write";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A folder in a floppy drive is a TR-DOS disk built from it; it is never written back
TEST(MediaManager_Test, FolderInAFloppyDriveIsATrdosDisk)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    MediaManager& manager = *emulator->GetContext()->pMediaManager;
    ScratchFolder folder("floppy-folder");
    folder.File("game.bin", std::string(1000, 'g'));
    MediaSource source;
    source.path = Utf8Path(folder.Path());

    InsertOptions writeThrough;
    writeThrough.access = AccessMode::WriteThrough;
    EXPECT_EQ(manager.Insert("fdd.a", source, writeThrough).error, MediaError::KindMismatch);

    ASSERT_TRUE(manager.Insert("fdd.a", source).Ok());
    EXPECT_EQ(manager.Info("fdd.a")->format, "folder-trd");
    GuestWrite(*manager.GetMedium("fdd.a")->Floppy(), 0x11);
    EXPECT_EQ(manager.Save("fdd.a").error, MediaError::NotSupported) << "no image file of its own: save to a path";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// A model switch moves the live media (technical design §8): each into the
/// slot with the same id, replacing what the new machine had there; a dirty one
/// without a slot is kept detached, a clean one closed
TEST(MediaManager_Test, AMediaSetMovesToAnotherManager)
{
    MediaManager from(nullptr);
    FakeBlockSlot fromA("sd.a"), fromB("sd.b"), fromC("sd.c");
    from.RegisterSlot(fromA);
    from.RegisterSlot(fromB);
    from.RegisterSlot(fromC);
    ASSERT_TRUE(from.Insert("sd.a", MemoryMedium(8)).Ok());
    ASSERT_TRUE(from.Insert("sd.b", MemoryMedium(8)).Ok());
    ASSERT_TRUE(from.Insert("sd.c", MemoryMedium(8)).Ok());
    ASSERT_TRUE(from.SetWriteProtect("sd.a", true).Ok());
    const std::vector<uint8_t> data(512, 0x77);
    ASSERT_TRUE(from.GetMedium("sd.a")->Block()->WriteSector(1, data.data()));
    ASSERT_TRUE(from.GetMedium("sd.b")->Block()->WriteSector(2, data.data()));
    const Medium* a = from.GetMedium("sd.a");

    MediaTransfer transfer = from.TakeMediaSet();
    EXPECT_EQ(transfer.entries.size(), 3u);
    EXPECT_EQ(fromA.attached, nullptr) << "the old slots are empty";
    EXPECT_FALSE(from.Info("sd.a")->present);

    MediaManager to(nullptr);
    FakeBlockSlot toA("sd.a");
    to.RegisterSlot(toA);
    ASSERT_TRUE(to.Insert("sd.a", MemoryMedium(4)).Ok());  // what the new machine's config put there

    const MediaTransferReport report = to.AdoptMediaSet(std::move(transfer));
    EXPECT_EQ(report.attached, std::vector<std::string>{"sd.a"});
    EXPECT_EQ(report.detached, std::vector<std::string>{"sd.b"});
    EXPECT_EQ(report.closed, std::vector<std::string>{"sd.c"});
    EXPECT_EQ(report.lines.size(), 3u);

    EXPECT_EQ(to.GetMedium("sd.a"), a) << "the same live medium";
    EXPECT_EQ(toA.attached, a);
    const auto info = to.Info("sd.a");
    EXPECT_TRUE(info->dirty);
    EXPECT_TRUE(info->writeProtect) << "the slot's switch came along";
    ASSERT_EQ(to.Detached().size(), 1u);
    EXPECT_EQ(to.Detached()[0].descriptor.id, "sd.b");
    EXPECT_EQ(to.Detached()[0].changes, "1 sector");

    to.UnregisterSlot("sd.a");
    from.UnregisterSlot("sd.a");
    from.UnregisterSlot("sd.b");
    from.UnregisterSlot("sd.c");
}
