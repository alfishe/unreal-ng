// Session journal (multi-source phases/c10e-session-journal.md): arenas in memory, a journal next to the medium that
// survives a crash of the emulator and is replayed by the next insert, flushed by the arena limit and by a timeout.
// A "crash" here is a copy of the journal file taken while the session lives: what the disk held at that moment.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "common/filehelper.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/mediamanager.h"

namespace
{
    constexpr uint64_t kSectors = 8192;  // 4 MiB

    /// A base whose sector s holds the byte s + 1, shared by the sessions of a test (the same content id)
    std::shared_ptr<MemoryDisk> MakeBase()
    {
        auto disk = std::make_shared<MemoryDisk>(kSectors);
        for (uint64_t s = 0; s < kSectors; s++)
            std::fill_n(disk->Data() + s * 512, 512, static_cast<uint8_t>(s + 1));
        disk->SetWritable(false);
        return disk;
    }

    class SharedBase : public IBlockDevice
    {
    public:
        explicit SharedBase(std::shared_ptr<IBlockDevice> disk) : _disk(std::move(disk)) {}
        uint64_t SectorCount() const override { return _disk->SectorCount(); }
        bool ReadSector(uint64_t lba, uint8_t* dst) override { return _disk->ReadSector(lba, dst); }
        bool WriteSector(uint64_t, const uint8_t*) override { return false; }
        bool IsWritable() const override { return false; }
        std::string Describe() const override { return _disk->Describe(); }
        uint64_t ContentId() const override { return _disk->ContentId(); }

    private:
        std::shared_ptr<IBlockDevice> _disk;
    };

    std::unique_ptr<SessionWriteMap> Session(const std::shared_ptr<MemoryDisk>& base, uint32_t arena = 4096, uint64_t limit = 8192)
    {
        auto session = std::make_unique<SessionWriteMap>(std::make_unique<SharedBase>(base));
        session->SetArenaBytes(arena);
        session->SetMemoryLimit(limit);
        return session;
    }

    std::vector<uint8_t> Data(uint64_t lba, uint8_t round)
    {
        std::vector<uint8_t> d(512, static_cast<uint8_t>(0xC0 + round));
        d[0] = static_cast<uint8_t>(lba);
        d[1] = static_cast<uint8_t>(lba >> 8);
        return d;
    }

    std::vector<uint8_t> Read(IBlockDevice& device, uint64_t lba)
    {
        std::vector<uint8_t> d(512);
        EXPECT_TRUE(device.ReadSector(lba, d.data()));
        return d;
    }

    /// The journal as the disk holds it now (the emulator "crashes" after this)
    void Snapshot(const std::filesystem::path& journal, const std::filesystem::path& copy)
    {
        std::filesystem::copy_file(journal, copy, std::filesystem::copy_options::overwrite_existing);
    }
}  // namespace

TEST(SessionArena_Test, SlotsReusedAndRewritesInPlace)
{
    auto base = MakeBase();
    auto s = Session(base, 4096, 0);  // 8 slots an arena, no limit
    for (uint64_t lba = 0; lba < 8; lba++)
        ASSERT_TRUE(s->WriteSector(lba, Data(lba, 0).data()));
    EXPECT_EQ(s->HotBytes(), 4096u);
    ASSERT_TRUE(s->WriteSector(3, Data(3, 1).data()));
    EXPECT_EQ(s->HotBytes(), 4096u) << "a rewrite goes to its slot";
    EXPECT_EQ(Read(*s, 3), Data(3, 1));
    ASSERT_TRUE(s->WriteSector(5, std::vector<uint8_t>(512, 6).data()));  // back to the base's data
    EXPECT_EQ(s->ChangedSectors(), 7u);
    ASSERT_TRUE(s->WriteSector(100, Data(100, 0).data()));
    EXPECT_EQ(s->HotBytes(), 4096u) << "the freed slot is reused";
    ASSERT_TRUE(s->WriteSector(101, Data(101, 0).data()));
    EXPECT_EQ(s->HotBytes(), 8192u) << "a second arena";
    s->Discard();
    EXPECT_EQ(s->HotBytes(), 0u);
    EXPECT_EQ(s->ChangedSectors(), 0u);
}

TEST(SessionJournal_Test, ReplayAfterCrash)
{
    ScratchFolder folder("session-journal-replay");
    const auto journal = folder.Path() / "disk.img.usession";
    auto base = MakeBase();
    uint64_t id = 0;
    {
        auto a = Session(base);
        EXPECT_EQ(a->OpenJournal(FileHelper::FromFsPath(journal), SessionWriteMap::JournalMode::Replay).outcome,
                  SessionWriteMap::JournalOpen::Outcome::Created);
        for (uint64_t i = 0; i < 300; i++)
            ASSERT_TRUE(a->WriteSector(i * 7, Data(i * 7, 2).data()));
        ASSERT_TRUE(a->WriteSector(14, std::vector<uint8_t>(512, 15).data()));  // a revert of a journaled sector
        EXPECT_TRUE(a->JournalRecoverable());
        ASSERT_TRUE(a->FlushJournal());
        id = a->ContentId();
        Snapshot(journal, folder.Path() / "crash.usession");
        a->CloseJournal(false);  // the clean end deletes it; the snapshot is what a crash would have left
    }
    EXPECT_FALSE(std::filesystem::exists(journal));
    std::filesystem::rename(folder.Path() / "crash.usession", journal);

    auto b = Session(base);
    const SessionWriteMap::JournalOpen open = b->OpenJournal(FileHelper::FromFsPath(journal), SessionWriteMap::JournalMode::Replay);
    ASSERT_EQ(open.outcome, SessionWriteMap::JournalOpen::Outcome::Replayed);
    EXPECT_EQ(open.sectors, 299u);
    EXPECT_EQ(b->ChangedSectors(), 299u);
    EXPECT_EQ(b->ContentId(), id);
    EXPECT_EQ(Read(*b, 7), Data(7, 2));
    EXPECT_EQ(Read(*b, 14), std::vector<uint8_t>(512, 15)) << "the revert was journaled too";
    EXPECT_EQ(Read(*b, 8), std::vector<uint8_t>(512, 9));
    b->CloseJournal(false);
}

TEST(SessionJournal_Test, TimeoutFlushes)
{
    ScratchFolder folder("session-journal-timeout");
    const auto journal = folder.Path() / "disk.img.usession";
    auto base = MakeBase();
    uint64_t now = 1000;
    auto a = Session(base, 4096, 0);  // no limit: only the timeout writes the journal
    a->SetClock([&now] { return now; });
    a->SetFlushSeconds(30);
    a->OpenJournal(FileHelper::FromFsPath(journal), SessionWriteMap::JournalMode::Replay);
    ASSERT_TRUE(a->WriteSector(42, Data(42, 3).data()));
    a->Tick();
    EXPECT_FALSE(std::filesystem::exists(journal));
    now += 29000;
    a->Tick();
    EXPECT_FALSE(std::filesystem::exists(journal)) << "29 s: still only in memory";
    now += 1000;
    a->Tick();
    ASSERT_TRUE(std::filesystem::exists(journal)) << "30 s: in the journal";

    Snapshot(journal, folder.Path() / "copy.usession");
    auto b = Session(base);
    ASSERT_EQ(b->OpenJournal(FileHelper::FromFsPath(folder.Path() / "copy.usession"), SessionWriteMap::JournalMode::Replay).sectors, 1u);
    EXPECT_EQ(Read(*b, 42), Data(42, 3));
    b->CloseJournal(false);
    a->CloseJournal(false);
}

/// A crash between a slot's sector data and its header: each sector reads old or new
TEST(SessionJournal_Test, TornSlotKeepsOldOrNew)
{
    ScratchFolder folder("session-journal-torn");
    const auto journal = folder.Path() / "disk.img.usession";
    auto base = MakeBase();
    auto a = Session(base, 4096, 0);
    a->OpenJournal(FileHelper::FromFsPath(journal), SessionWriteMap::JournalMode::Replay);
    ASSERT_TRUE(a->WriteSector(1, Data(1, 1).data()));
    ASSERT_TRUE(a->FlushJournal());
    std::vector<uint8_t> oldHeader(512);
    {
        std::ifstream in(journal, std::ios::binary);
        in.seekg(4096);
        in.read(reinterpret_cast<char*>(oldHeader.data()), 512);
    }
    ASSERT_TRUE(a->WriteSector(1, Data(1, 2).data()));  // rewritten in place in the slot
    ASSERT_TRUE(a->WriteSector(2, Data(2, 2).data()));  // new in the slot
    ASSERT_TRUE(a->FlushJournal());
    Snapshot(journal, folder.Path() / "torn.usession");
    {
        // The new header never reached the disk
        std::fstream f(folder.Path() / "torn.usession", std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(4096);
        f.write(reinterpret_cast<const char*>(oldHeader.data()), 512);
    }
    auto b = Session(base);
    ASSERT_EQ(b->OpenJournal(FileHelper::FromFsPath(folder.Path() / "torn.usession"), SessionWriteMap::JournalMode::Replay).outcome,
              SessionWriteMap::JournalOpen::Outcome::Replayed);
    const std::vector<uint8_t> one = Read(*b, 1);
    EXPECT_TRUE(one == Data(1, 1) || one == Data(1, 2));
    EXPECT_EQ(Read(*b, 2), std::vector<uint8_t>(512, 3)) << "not named by the old header: the base's data";
    b->CloseJournal(false);
    a->CloseJournal(false);
}

TEST(SessionJournal_Test, StaleAndDamagedSetAside)
{
    ScratchFolder folder("session-journal-stale");
    const auto journal = folder.Path() / "disk.img.usession";
    auto base = MakeBase();
    {
        auto a = Session(base);
        a->OpenJournal(FileHelper::FromFsPath(journal), SessionWriteMap::JournalMode::Replay);
        ASSERT_TRUE(a->WriteSector(9, Data(9, 0).data()));
        a->CloseJournal(true);  // exit with unsaved writes: kept
    }
    ASSERT_TRUE(std::filesystem::exists(journal));

    // Over another disk: not replayed, renamed aside
    auto other = MakeBase();
    auto b = Session(other);
    const SessionWriteMap::JournalOpen open = b->OpenJournal(FileHelper::FromFsPath(journal), SessionWriteMap::JournalMode::Replay);
    EXPECT_EQ(open.outcome, SessionWriteMap::JournalOpen::Outcome::Stale);
    EXPECT_EQ(open.kept, "disk.img.usession.1.stale");
    EXPECT_EQ(b->ChangedSectors(), 0u);
    EXPECT_TRUE(std::filesystem::exists(folder.Path() / "disk.img.usession.1.stale"));
    b->CloseJournal(false);

    // A damaged header: the same
    std::ofstream(journal, std::ios::binary) << std::string(5000, 'x');
    auto c = Session(base);
    EXPECT_EQ(c->OpenJournal(FileHelper::FromFsPath(journal), SessionWriteMap::JournalMode::Replay).outcome,
              SessionWriteMap::JournalOpen::Outcome::Stale);
    EXPECT_TRUE(std::filesystem::exists(folder.Path() / "disk.img.usession.2.stale"));
    c->CloseJournal(false);

    // Two sessions cannot own one journal
    auto d = Session(base);
    auto e = Session(base);
    d->OpenJournal(FileHelper::FromFsPath(journal), SessionWriteMap::JournalMode::Replay);
    EXPECT_EQ(e->OpenJournal(FileHelper::FromFsPath(journal), SessionWriteMap::JournalMode::Replay).outcome,
              SessionWriteMap::JournalOpen::Outcome::InUse);
    EXPECT_FALSE(e->JournalRecoverable());
    d->CloseJournal(false);
    e->CloseJournal(false);
}

namespace
{
    class DiskSlot : public IMediaSlot
    {
    public:
        DiskSlot()
        {
            _descriptor.id = "ide0.master";
            _descriptor.kind = MediaKind::Block;
            _descriptor.label = "hard disk";
            _descriptor.defaultAccess = AccessMode::Session;
        }
        const SlotDescriptor& Descriptor() const override { return _descriptor; }
        void Attach(Medium& medium) override { attached = &medium; }
        void Detach() override { attached = nullptr; }
        void SourceChanged(Medium&) override {}
        Medium* attached = nullptr;

    private:
        SlotDescriptor _descriptor;
    };

    bool Has(const std::vector<std::string>& lines, const std::string& text)
    {
        return std::any_of(lines.begin(), lines.end(), [&text](const std::string& l) { return l.find(text) != std::string::npos; });
    }

    /// A manager with one hard-disk slot, the image inserted in session access
    struct Rig
    {
        MediaManager manager{nullptr};
        DiskSlot slot;
        std::filesystem::path image;
        std::filesystem::path journal;

        explicit Rig(const std::filesystem::path& folder)
        {
            image = folder / "disk.img";
            journal = folder / "disk.img.usession";
            if (!std::filesystem::exists(image))
                std::ofstream(image, std::ios::binary) << std::string(1024 * 1024, '\0');
            manager.RegisterSlot(slot);
        }
        ~Rig()
        {
            if (slot.attached)
            {
                EjectOptions discard;
                discard.disposition = Disposition::Discard;
                manager.Eject("ide0.master", discard);
            }
            manager.UnregisterSlot("ide0.master");
        }
        MediaResult Insert(JournalChoice journal)
        {
            MediaSource source;
            source.path = FileHelper::FromFsPath(image);
            InsertOptions options;
            options.journal = journal;
            return manager.Insert("ide0.master", source, options);
        }
        void Write(uint64_t lba, uint8_t fill)
        {
            const std::vector<uint8_t> data(512, fill);
            ASSERT_TRUE(slot.attached->Block()->WriteSector(lba, data.data()));
            manager.ApplyPending();
        }
        SessionWriteMap& Session() { return *slot.attached->Session(); }
    };
}  // namespace

TEST(SessionJournal_Test, InsertReplaysDiscardsOrIgnores)
{
    ScratchFolder folder("session-journal-insert");
    const auto crash = folder.Path() / "crash.usession";
    {
        Rig rig(folder.Path());
        ASSERT_TRUE(rig.Insert(JournalChoice::Replay).Ok());
        rig.Write(10, 0xAA);
        rig.Write(11, 0xBB);
        ASSERT_TRUE(rig.Session().FlushJournal());
        Snapshot(rig.journal, crash);
    }  // the rig ejects with discard: its journal goes
    ASSERT_FALSE(std::filesystem::exists(folder.Path() / "disk.img.usession"));

    // Replayed: dirty with the crash's writes
    std::filesystem::copy_file(crash, folder.Path() / "disk.img.usession");
    {
        Rig rig(folder.Path());
        const MediaResult r = rig.Insert(JournalChoice::Replay);
        ASSERT_TRUE(r.Ok()) << r.message;
        EXPECT_TRUE(Has(r.report, "session journal disk.img.usession replayed: 2 sector(s)"));
        EXPECT_TRUE(rig.manager.Info("ide0.master")->dirty);
        EXPECT_EQ(Read(*rig.slot.attached->Block(), 10), std::vector<uint8_t>(512, 0xAA));
    }

    // Discarded unread
    std::filesystem::copy_file(crash, folder.Path() / "disk.img.usession");
    {
        Rig rig(folder.Path());
        const MediaResult r = rig.Insert(JournalChoice::Discard);
        EXPECT_TRUE(Has(r.report, "discarded unread"));
        EXPECT_FALSE(rig.manager.Info("ide0.master")->dirty);
        EXPECT_FALSE(std::filesystem::exists(folder.Path() / "disk.img.usession"));
    }

    // Off: no journal next to the medium, the old one left alone
    std::filesystem::copy_file(crash, folder.Path() / "disk.img.usession");
    {
        Rig rig(folder.Path());
        ASSERT_TRUE(rig.Insert(JournalChoice::Off).Ok());
        EXPECT_FALSE(rig.manager.Info("ide0.master")->dirty);
        rig.Write(12, 0xCC);
        EXPECT_FALSE(rig.Session().JournalRecoverable());
    }
    EXPECT_TRUE(std::filesystem::exists(folder.Path() / "disk.img.usession")) << "journal: off leaves it as it was";

    // Default follows [MEDIA] SessionJournal (the test runner: off)
    {
        Rig rig(folder.Path());
        ASSERT_TRUE(rig.Insert(JournalChoice::Default).Ok());
        EXPECT_FALSE(rig.manager.Info("ide0.master")->dirty);
    }
}

TEST(SessionJournal_Test, CleanEndsDeleteItExitKeepsIt)
{
    ScratchFolder folder("session-journal-ends");
    const auto journal = folder.Path() / "disk.img.usession";

    // A discard on eject
    {
        Rig rig(folder.Path());
        ASSERT_TRUE(rig.Insert(JournalChoice::Replay).Ok());
        rig.Write(5, 1);
        ASSERT_TRUE(rig.Session().FlushJournal());
        ASSERT_TRUE(std::filesystem::exists(journal));
        EjectOptions discard;
        discard.disposition = Disposition::Discard;
        ASSERT_TRUE(rig.manager.Eject("ide0.master", discard).Ok());
        EXPECT_FALSE(std::filesystem::exists(journal));
    }

    // A save into the image itself
    {
        Rig rig(folder.Path());
        ASSERT_TRUE(rig.Insert(JournalChoice::Replay).Ok());
        rig.Write(6, 2);
        ASSERT_TRUE(rig.Session().FlushJournal());
        ASSERT_TRUE(rig.manager.Save("ide0.master", {}).Ok());
        EXPECT_FALSE(std::filesystem::exists(journal)) << "saved: nothing left to recover";
    }

    // The manager goes with a dirty medium (the emulator exits): the journal stays for the next insert
    {
        Rig rig(folder.Path());
        ASSERT_TRUE(rig.Insert(JournalChoice::Replay).Ok());
        rig.Write(7, 3);
        rig.slot.attached = nullptr;  // the rig's own teardown must not eject: the manager just goes
    }
    EXPECT_TRUE(std::filesystem::exists(journal)) << "unsaved writes at exit";
    {
        Rig rig(folder.Path());
        const MediaResult r = rig.Insert(JournalChoice::Replay);
        EXPECT_TRUE(Has(r.report, "replayed: 1 sector(s)")) << (r.report.empty() ? "" : r.report.front());
    }
}
