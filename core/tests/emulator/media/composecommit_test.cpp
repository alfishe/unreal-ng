// S3: a graft composite committed into its base image (multi-source phases/c8-commit-writeback.md §2; DT-14), and
// the commit journal that undoes an interrupted commit when the image is opened again.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatguest.h"
#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/commitjournal.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/media/blockformats.h"
#include "emulator/media/mediamanager.h"

namespace
{
    std::vector<uint8_t> Slurp(const std::filesystem::path& path)
    {
        std::vector<uint8_t> bytes(static_cast<size_t>(std::filesystem::file_size(path)));
        std::ifstream in(path, std::ios::binary);
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return bytes;
    }

    bool Has(const std::vector<std::string>& lines, const std::string& text)
    {
        return std::any_of(lines.begin(), lines.end(), [&text](const std::string& l) { return l.find(text) != std::string::npos; });
    }

    class DiskSlot : public IMediaSlot
    {
    public:
        explicit DiskSlot(std::string id)
        {
            _d.id = std::move(id);
            _d.kind = MediaKind::Block;
            _d.label = "disk";
            _d.tags = {"ide", "hdd"};
        }
        const SlotDescriptor& Descriptor() const override { return _d; }
        void Attach(Medium& m) override { attached = &m; }
        void Detach() override { attached = nullptr; }
        void SourceChanged(Medium&) override { sourceChanges++; }
        Medium* attached = nullptr;
        int sourceChanges = 0;

    private:
        SlotDescriptor _d;
    };

    class ComposeCommit_Test : public ::testing::Test
    {
    protected:
        ScratchFolder _folder{"compose-commit"};
        MediaManager _manager{nullptr};
        DiskSlot _slot{"ide0.master"};
        DiskSlot _other{"ide0.slave"};
        std::filesystem::path _base;

        void SetUp() override
        {
            _manager.RegisterSlot(_slot);
            _manager.RegisterSlot(_other);
            ScratchFolder files("compose-commit-base");
            files.File("README.TXT", "from the base");
            files.File("DOS/SYSTEM.SYS", std::string(3000, 's'));
            const FatSourceImage image = FolderToFatDisk(files.Path(), FatType::Fat16);
            ASSERT_TRUE(image.ok()) << image.error;
            _base = _folder.Path() / "base.img";
            ASSERT_TRUE(SaveSparse(image, _base));
            _folder.File("util/TOOL.COM", std::string(5000, 't'));
        }
        void TearDown() override
        {
            EjectOptions discard;
            discard.disposition = Disposition::Discard;
            _manager.Eject("ide0.master", discard);
            _manager.Eject("ide0.slave", discard);
            _manager.UnregisterSlot("ide0.master");
            _manager.UnregisterSlot("ide0.slave");
        }

        MediaResult InsertComposite(const std::string& build = "graft", const std::string& baseName = "base.img")
        {
            const auto descriptor = _folder.File("disk.ucompose.yaml", "version: 1\ntarget: {build: " + build +
                                                                           ", free: 1MiB, fixedTime: 1767268800}\nlayers:\n"
                                                                           "  - {name: base, source: {image: " + baseName + "}}\n"
                                                                           "  - {name: util, source: {folder: util}, mount: /UTIL}\n");
            MediaSource source;
            source.path = FileHelper::FromFsPath(descriptor);
            return _manager.Insert("ide0.master", source, {});
        }

        void GuestWrites()
        {
            FatGuest guest(*_slot.attached->Block());
            ASSERT_TRUE(guest.Create("/GUEST.TXT", std::vector<uint8_t>(700, 'g')));
            ASSERT_TRUE(guest.Mkdir("/NEWDIR"));
            _manager.ApplyPending();
        }

        std::vector<uint8_t> GuestView()
        {
            IBlockDevice& d = *_slot.attached->Block();
            std::vector<uint8_t> all(d.SectorCount() * 512);
            for (uint64_t lba = 0; lba < d.SectorCount(); lba++)
                EXPECT_TRUE(d.ReadSector(lba, all.data() + lba * 512));
            return all;
        }

        SaveOptions Commit(bool plan = false, bool force = false)
        {
            SaveOptions options;
            options.strategy = "commit";
            options.plan = plan;
            options.force = force;
            return options;
        }
    };
}  // namespace

TEST_F(ComposeCommit_Test, PlanCountsPatchGraftAndGuest)
{
    ASSERT_TRUE(InsertComposite().Ok());
    GuestWrites();
    const std::vector<uint8_t> before = Slurp(_base);
    const MediaResult plan = _manager.Save("ide0.master", Commit(/*plan*/ true));
    ASSERT_TRUE(plan.Ok()) << plan.message;
    ASSERT_GE(plan.report.size(), 2u);
    EXPECT_NE(plan.report[0].find("re-encoded"), std::string::npos) << plan.report[0];
    EXPECT_NE(plan.report[0].find("of grafted files"), std::string::npos) << plan.report[0];
    EXPECT_TRUE(Has(plan.report, "plan only")) << plan.report.back();
    EXPECT_EQ(Slurp(_base), before) << "a plan writes nothing";
    EXPECT_TRUE(_manager.Info("ide0.master")->dirty);
}

TEST_F(ComposeCommit_Test, CommitWritesWhatTheGuestSees)
{
    ASSERT_TRUE(InsertComposite().Ok());
    GuestWrites();
    const std::vector<uint8_t> expected = GuestView();

    SaveOutcome outcome;
    const MediaResult committed = _manager.Save("ide0.master", Commit(), &outcome);
    ASSERT_TRUE(committed.Ok()) << committed.message;
    EXPECT_EQ(outcome.savedPath, FileHelper::FromFsPath(_base));
    EXPECT_FALSE(std::filesystem::exists(CommitJournal::PathFor(_base))) << "the journal goes once the image is written";
    std::vector<uint8_t> bytes = Slurp(_base);
    bytes.resize(expected.size());
    EXPECT_TRUE(bytes == expected) << "the base alone is the disk the guest saw";

    auto info = _manager.Info("ide0.master");
    EXPECT_EQ(info->source, FileHelper::FromFsPath(_base));
    EXPECT_FALSE(info->dirty);
    EXPECT_EQ(_slot.attached->Composite(), nullptr) << "the slot holds the base itself";
    EXPECT_EQ(_slot.sourceChanges, 1);
    FatVolumeReader reader;
    auto image = HddImageFormats::OpenBlock(FileHelper::FromFsPath(_base), "raw", RawImage::Access::ReadOnly);
    ASSERT_NE(image, nullptr);
    ASSERT_TRUE(reader.Open(*image));
    FatDirEntryInfo entry;
    EXPECT_TRUE(reader.Stat("/UTIL/TOOL.COM", entry)) << "the folder layer's file";
    EXPECT_TRUE(reader.Stat("/GUEST.TXT", entry)) << "the guest's file";
    EXPECT_TRUE(reader.Stat("/NEWDIR", entry) && entry.isDirectory);
    EXPECT_TRUE(reader.Stat("/README.TXT", entry));
}

TEST_F(ComposeCommit_Test, Refusals)
{
    // A rebuild is not a graft
    ASSERT_TRUE(InsertComposite("rebuild").Ok());
    EXPECT_EQ(_manager.Save("ide0.master", Commit()).error, MediaError::BadRequest);
    EjectOptions discard;
    discard.disposition = Disposition::Discard;
    ASSERT_TRUE(_manager.Eject("ide0.master", discard).Ok());

    // The base in another slot
    ASSERT_TRUE(InsertComposite().Ok());
    MediaSource plain;
    plain.path = FileHelper::FromFsPath(_base);
    InsertOptions readOnly;
    readOnly.access = AccessMode::ReadOnly;
    ASSERT_TRUE(_manager.Insert("ide0.slave", plain, readOnly).Ok());
    EXPECT_EQ(_manager.Save("ide0.master", Commit()).error, MediaError::InUse);
    ASSERT_TRUE(_manager.Eject("ide0.slave", discard).Ok());

    // Lost clusters: refused, then with force
    {
        FatGuest guest(*_slot.attached->Block());
        ASSERT_TRUE(guest.LoseClusters(2));
    }
    _manager.ApplyPending();
    const MediaResult lost = _manager.Save("ide0.master", Commit());
    EXPECT_EQ(lost.error, MediaError::Dirty) << lost.message;
    EXPECT_NE(lost.message.find("lost clusters"), std::string::npos) << lost.message;
    EXPECT_TRUE(_manager.Save("ide0.master", Commit(false, /*force*/ true)).Ok());
    ASSERT_TRUE(_manager.Eject("ide0.master", discard).Ok());

    // A CHD base is not written in place
    std::string error;
    auto raw = HddImageFormats::OpenBlock(FileHelper::FromFsPath(_base), "raw", RawImage::Access::ReadOnly, &error);
    ASSERT_NE(raw, nullptr) << error;
    BlockWriteOptions chd;
    chd.compression = "none";
    ASSERT_TRUE(BlockFormats::Write(*raw, FileHelper::FromFsPath(_folder.Path() / "base.chd"), chd, {}).Ok());
    raw.reset();
    ASSERT_TRUE(InsertComposite("graft", "base.chd").Ok());
    EXPECT_EQ(_manager.Save("ide0.master", Commit()).error, MediaError::NotSupported);
}

/// The journal of a commit cut short: written in full, then half the planned sectors written; the next open puts
/// every journaled sector back
TEST(CommitJournal_Test, InterruptedCommitRolledBack)
{
    ScratchFolder folder("commit-journal");
    const auto image = folder.File("disk.img", std::string(64 * 512, '\x11'));
    const std::vector<uint8_t> before = Slurp(image);
    std::string error;
    {
        auto device = HddImageFormats::OpenBlock(FileHelper::FromFsPath(image), "raw", RawImage::Access::ReadWrite, &error);
        ASSERT_NE(device, nullptr) << error;
        ASSERT_TRUE(CommitJournal::Write(image, *device, {3, 4, 20, 63}, &error)) << error;
        const std::vector<uint8_t> junk(512, 0xEE);
        ASSERT_TRUE(device->WriteSector(3, junk.data()));
        ASSERT_TRUE(device->WriteSector(20, junk.data()));
    }
    ASSERT_NE(Slurp(image), before);

    std::string detail;
    EXPECT_EQ(CommitJournal::Recover(image, &detail), CommitJournal::Recovery::RolledBack);
    EXPECT_NE(detail.find("rolled back"), std::string::npos) << detail;
    EXPECT_EQ(Slurp(image), before);
    EXPECT_FALSE(std::filesystem::exists(CommitJournal::PathFor(image)));
    EXPECT_EQ(CommitJournal::Recover(image, &detail), CommitJournal::Recovery::None);
}

TEST(CommitJournal_Test, UnfinishedJournalDropped)
{
    ScratchFolder folder("commit-journal-cut");
    const auto image = folder.File("disk.img", std::string(16 * 512, '\x22'));
    std::string error;
    {
        auto device = HddImageFormats::OpenBlock(FileHelper::FromFsPath(image), "raw", RawImage::Access::ReadWrite, &error);
        ASSERT_TRUE(CommitJournal::Write(image, *device, {1, 2}, &error)) << error;
    }
    const auto journal = CommitJournal::PathFor(image);
    std::filesystem::resize_file(journal, std::filesystem::file_size(journal) - 12);
    std::string detail;
    EXPECT_EQ(CommitJournal::Recover(image, &detail), CommitJournal::Recovery::Dropped);
    EXPECT_FALSE(std::filesystem::exists(journal));
    EXPECT_EQ(Slurp(image), std::vector<uint8_t>(16 * 512, 0x22));
}

TEST(CommitJournal_Test, DamagedJournalKeptAndReported)
{
    ScratchFolder folder("commit-journal-bad");
    const auto image = folder.File("disk.img", std::string(16 * 512, '\x33'));
    std::string error;
    {
        auto device = HddImageFormats::OpenBlock(FileHelper::FromFsPath(image), "raw", RawImage::Access::ReadWrite, &error);
        ASSERT_TRUE(CommitJournal::Write(image, *device, {5}, &error)) << error;
    }
    const auto journal = CommitJournal::PathFor(image);
    {
        std::fstream f(journal, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(24 + 8 + 10);  // inside the saved sector
        f.put('\x7F');
    }
    std::string detail;
    EXPECT_EQ(CommitJournal::Recover(image, &detail), CommitJournal::Recovery::Damaged);
    std::filesystem::path bad = journal;
    bad += ".bad";
    EXPECT_TRUE(std::filesystem::exists(bad)) << detail;
}

/// The registry undoes an interrupted commit when the image goes into a slot, and says so
TEST_F(ComposeCommit_Test, InsertRollsBackAnInterruptedCommit)
{
    const std::vector<uint8_t> before = Slurp(_base);
    std::string error;
    {
        auto device = HddImageFormats::OpenBlock(FileHelper::FromFsPath(_base), "raw", RawImage::Access::ReadWrite, &error);
        ASSERT_TRUE(CommitJournal::Write(_base, *device, {0, 1}, &error)) << error;
        const std::vector<uint8_t> junk(512, 0xEE);
        ASSERT_TRUE(device->WriteSector(0, junk.data()));
    }
    MediaSource plain;
    plain.path = FileHelper::FromFsPath(_base);
    const MediaResult inserted = _manager.Insert("ide0.slave", plain, {});
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    EXPECT_TRUE(Has(inserted.report, "rolled back"));
    EXPECT_EQ(Slurp(_base), before);
}
