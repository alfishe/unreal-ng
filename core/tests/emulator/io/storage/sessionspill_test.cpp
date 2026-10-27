// Session tiers (multi-source phases/c10d-session-spill.md, c10e-session-journal.md): a session keeps at most its
// memory limit of changed sectors in memory and the rest in its journal; reads, iteration, the content id and the
// session delta see one set of changes whichever tier holds them.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "common/filehelper.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/sessiondelta.h"

namespace
{
    constexpr uint64_t kSectors = 32768;  // 16 MiB
    constexpr uint64_t kLimit = 1024 * 1024;

    /// A base whose sector s holds the byte s + 1
    std::unique_ptr<MemoryDisk> MakeBase()
    {
        auto disk = std::make_unique<MemoryDisk>(kSectors);
        for (uint64_t s = 0; s < kSectors; s++)
            std::fill_n(disk->Data() + s * 512, 512, static_cast<uint8_t>(s + 1));
        disk->SetWritable(false);
        return disk;
    }

    /// Another session over the same disk (the same content id underneath)
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

    std::array<uint8_t, 512> Data(uint64_t lba, uint32_t round)
    {
        std::array<uint8_t, 512> d{};
        for (size_t i = 0; i < d.size(); i++)
            d[i] = static_cast<uint8_t>(lba * 31 + round * 7 + i);
        d[0] = static_cast<uint8_t>(0xA0 | (round & 0x0F));  // never the base's byte
        return d;
    }

    /// A memory limit in 32 KiB arenas (the default arena of 1 MiB is larger than these tests' limits)
    void Limit(SessionWriteMap& map, uint64_t bytes)
    {
        map.SetArenaBytes(32 * 1024);
        map.SetMemoryLimit(bytes);
    }

    /// Points new sessions' temp journals at a folder of this test, and back afterwards
    class SpillHere
    {
    public:
        explicit SpillHere(const std::filesystem::path& folder) : _old(SessionWriteMap::SpillFolder())
        {
            SessionWriteMap::SetSpillFolder(FileHelper::FromFsPath(folder));
        }
        ~SpillHere() { SessionWriteMap::SetSpillFolder(_old); }

    private:
        std::string _old;
    };

    size_t SpillFiles(const std::filesystem::path& folder)
    {
        size_t n = 0;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
            n += it->path().extension() == ".spill";
        return n;
    }

    /// Every changed sector of `map` equals `reference`, in order, through every way of reading it
    void ExpectChanges(SessionWriteMap& map, const std::map<uint64_t, std::array<uint8_t, 512>>& reference)
    {
        ASSERT_EQ(map.ChangedSectors(), reference.size());
        std::vector<uint64_t> order;
        uint8_t sector[512];
        EXPECT_TRUE(map.ForEachChange([&](uint64_t lba, const uint8_t* data) {
            order.push_back(lba);
            const auto it = reference.find(lba);
            EXPECT_TRUE(it != reference.end() && std::equal(data, data + 512, it->second.begin())) << lba;
            return true;
        }));
        ASSERT_EQ(order.size(), reference.size());
        auto it = reference.begin();
        for (size_t i = 0; i < order.size(); i++, ++it)
        {
            ASSERT_EQ(order[i], it->first);
            ASSERT_TRUE(map.ReadSector(it->first, sector));
            ASSERT_TRUE(std::equal(sector, sector + 512, it->second.begin())) << it->first;
        }
    }
}  // namespace

TEST(SessionSpill_Test, ReadsAcrossTiers)
{
    ScratchFolder folder("session-spill");
    SpillHere here(folder.Path());
    std::map<uint64_t, std::array<uint8_t, 512>> reference;
    {
        SessionWriteMap map(MakeBase());
        Limit(map, kLimit / 4);
        // 2 MiB scattered over the disk, eight times the limit
        for (uint64_t i = 0; i < 4096; i++)
        {
            const uint64_t lba = (i * 7919) % kSectors;
            reference[lba] = Data(lba, 0);
            ASSERT_TRUE(map.WriteSector(lba, reference[lba].data()));
            ASSERT_LE(map.HotBytes(), map.MemoryCeiling()) << "the limit and what is on its way to the disk";
        }
        EXPECT_GT(map.SpilledSectors(), 0u);
        EXPECT_FALSE(map.SpillPath().empty());
        EXPECT_FALSE(map.SpillFailed());
        ExpectChanges(map, reference);
        uint8_t sector[512];
        ASSERT_TRUE(map.ReadSector(1, sector));
        EXPECT_EQ(sector[0], (reference.count(1) ? reference[1][0] : 2));
        EXPECT_EQ(map.ZeroRun(0), 0u);
    }
    EXPECT_EQ(SpillFiles(folder.Path()), 0u) << "the spill file goes with its session";
}

TEST(SessionSpill_Test, RewriteMovesBack)
{
    ScratchFolder folder("session-spill-rewrite");
    SpillHere here(folder.Path());
    SessionWriteMap map(MakeBase());
    Limit(map, kLimit);
    for (uint64_t lba = 0; lba < 4096; lba++)
        ASSERT_TRUE(map.WriteSector(lba, Data(lba, 0).data()));
    ASSERT_GT(map.SpilledSectors(), 0u);
    ASSERT_TRUE(map.NextChanged(0).has_value());
    const uint64_t spilled = *map.NextChanged(0);  // the oldest chunks went first: sector 0 is in the file
    const size_t changed = map.ChangedSectors();

    // Written again: the new data, still one change
    ASSERT_TRUE(map.WriteSector(spilled, Data(spilled, 1).data()));
    EXPECT_EQ(map.ChangedSectors(), changed);
    uint8_t sector[512];
    ASSERT_TRUE(map.ReadSector(spilled, sector));
    EXPECT_EQ(sector[0], Data(spilled, 1)[0]);

    // Written back to the base's data: no longer a change, in either tier
    const std::vector<uint8_t> original(512, static_cast<uint8_t>(spilled + 1));
    ASSERT_TRUE(map.WriteSector(spilled, original.data()));
    EXPECT_EQ(map.ChangedSectors(), changed - 1);
    EXPECT_NE(map.NextChanged(spilled), std::optional<uint64_t>(spilled));
    // Its neighbour, still in the file, back to the base's data too
    const size_t inFile = map.SpilledSectors();
    ASSERT_TRUE(map.WriteSector(spilled + 1, std::vector<uint8_t>(512, static_cast<uint8_t>(spilled + 2)).data()));
    EXPECT_EQ(map.ChangedSectors(), changed - 2);
    EXPECT_EQ(map.SpilledSectors(), inFile - 1);

    map.Discard();
    EXPECT_EQ(map.ChangedSectors(), 0u);
    EXPECT_EQ(map.SpilledSectors(), 0u);
    EXPECT_TRUE(map.SpillPath().empty());
    ASSERT_TRUE(map.ReadSector(spilled, sector));
    EXPECT_EQ(sector[0], static_cast<uint8_t>(spilled + 1));
}

/// Random writes, rewrites and writes back to the base: a limited session and an unlimited one agree on everything
TEST(SessionSpill_Test, IteratesInOrder)
{
    ScratchFolder folder("session-spill-order");
    SpillHere here(folder.Path());
    std::shared_ptr<IBlockDevice> base = MakeBase();
    SessionWriteMap limited(std::make_unique<SharedBase>(base));
    Limit(limited, 256 * 1024);
    SessionWriteMap unlimited(std::make_unique<SharedBase>(base));
    unlimited.SetMemoryLimit(0);
    std::map<uint64_t, std::array<uint8_t, 512>> reference;
    std::mt19937_64 random(7);
    for (uint32_t i = 0; i < 6000; i++)
    {
        const uint64_t lba = random() % 3000;
        if (random() % 5 == 0)
        {
            const std::vector<uint8_t> original(512, static_cast<uint8_t>(lba + 1));
            ASSERT_TRUE(limited.WriteSector(lba, original.data()));
            ASSERT_TRUE(unlimited.WriteSector(lba, original.data()));
            reference.erase(lba);
        }
        else
        {
            const auto data = Data(lba, i);
            ASSERT_TRUE(limited.WriteSector(lba, data.data()));
            ASSERT_TRUE(unlimited.WriteSector(lba, data.data()));
            reference[lba] = data;
        }
    }
    EXPECT_GT(limited.SpilledSectors(), 0u);
    EXPECT_EQ(unlimited.SpilledSectors(), 0u);
    ExpectChanges(limited, reference);
    for (uint64_t lba = 0; lba < 3100; lba += 37)
        EXPECT_EQ(limited.NextChanged(lba), unlimited.NextChanged(lba)) << lba;
    EXPECT_EQ(limited.ContentId(), unlimited.ContentId()) << "the id follows the content, not the tier";
    EXPECT_TRUE(limited.ChangedIn(0, 3000));
    EXPECT_FALSE(limited.ChangedIn(3000, 100));
}

TEST(SessionSpill_Test, DeltaAcrossTiers)
{
    ScratchFolder folder("session-spill-delta");
    SpillHere here(folder.Path());
    std::shared_ptr<IBlockDevice> base = MakeBase();
    SessionWriteMap limited(std::make_unique<SharedBase>(base));
    Limit(limited, kLimit / 4);
    std::map<uint64_t, std::array<uint8_t, 512>> reference;
    for (uint64_t i = 0; i < 2000; i++)
    {
        const uint64_t lba = (i * 13) % kSectors;
        reference[lba] = Data(lba, 3);
        ASSERT_TRUE(limited.WriteSector(lba, reference[lba].data()));
    }
    ASSERT_GT(limited.SpilledSectors(), 0u);

    DeltaIdentity identity;
    identity.contentId = limited.Base().ContentId();
    identity.sectorCount = kSectors;
    const auto path = folder.Path() / "disk.delta";
    ASSERT_TRUE(SessionDelta::Save(path, limited, identity).Ok());

    // Restored into a limited session again: the load spills as it goes
    SessionWriteMap restored(std::make_unique<SharedBase>(base));
    Limit(restored, kLimit / 4);
    std::string detail;
    ASSERT_EQ(SessionDelta::Load(path, restored, identity, detail), DeltaLoad::Restored) << detail;
    ExpectChanges(restored, reference);
    EXPECT_GT(restored.SpilledSectors(), 0u);
    EXPECT_EQ(restored.ContentId(), limited.ContentId());
}

TEST(SessionSpill_Test, SpillFailureKeepsData)
{
    ScratchFolder folder("session-spill-fail");
    // A regular file where the spill folder should be: no spill file can be made under it
    const auto blocked = folder.Path() / "not-a-folder";
    std::ofstream(blocked) << "x";
    SpillHere here(blocked / "spill");
    SessionWriteMap map(MakeBase());
    Limit(map, 64 * 1024);
    std::map<uint64_t, std::array<uint8_t, 512>> reference;
    for (uint64_t lba = 0; lba < 1000; lba++)
    {
        reference[lba] = Data(lba, 5);
        ASSERT_TRUE(map.WriteSector(lba, reference[lba].data())) << "a write never fails because of the spill";
    }
    EXPECT_TRUE(map.SpillFailed());
    EXPECT_EQ(map.SpilledSectors(), 0u);
    EXPECT_GT(map.HotBytes(), 64u * 1024) << "over the limit: kept in memory rather than lost";
    ExpectChanges(map, reference);
}

TEST(ChangeView_Test, MapAndWindow)
{
    MapChangeView::Map changes;
    for (uint64_t lba : {3ull, 10ull, 11ull, 40ull})
        changes[lba].fill(static_cast<uint8_t>(lba));
    const MapChangeView view(changes);
    EXPECT_EQ(view.ChangedSectors(), 4u);
    EXPECT_EQ(view.NextChanged(0), std::optional<uint64_t>(3));
    EXPECT_EQ(view.NextChanged(4), std::optional<uint64_t>(10));
    EXPECT_EQ(view.NextChanged(41), std::nullopt);
    EXPECT_TRUE(view.ChangedIn(9, 2));
    EXPECT_FALSE(view.ChangedIn(12, 28));
    uint8_t sector[512];
    EXPECT_FALSE(view.ReadChanged(4, sector));
    ASSERT_TRUE(view.ReadChanged(40, sector));
    EXPECT_EQ(sector[0], 40);
    std::vector<uint64_t> visited;
    EXPECT_FALSE(view.ForEachChange([&](uint64_t lba, const uint8_t*) {
        visited.push_back(lba);
        return lba < 10;
    })) << "stopped by the visitor";
    EXPECT_EQ(visited, (std::vector<uint64_t>{3, 10}));

    // [10, 40): 10 and 11, numbered from the window's start
    const WindowChangeView window(view, 10, 30);
    EXPECT_EQ(window.ChangedSectors(), 2u);
    EXPECT_EQ(window.NextChanged(0), std::optional<uint64_t>(0));
    EXPECT_EQ(window.NextChanged(2), std::nullopt) << "40 is past the window";
    ASSERT_TRUE(window.ReadChanged(1, sector));
    EXPECT_EQ(sector[0], 11);
    EXPECT_FALSE(window.ReadChanged(30, sector));
}

/// A damaged delta changes nothing (the whole file is checked before anything is applied), spilled or not
TEST(SessionSpill_Test, DamagedDeltaLeavesTheSession)
{
    ScratchFolder folder("session-spill-damaged");
    SpillHere here(folder.Path());
    std::shared_ptr<IBlockDevice> base = MakeBase();
    SessionWriteMap source(std::make_unique<SharedBase>(base));
    Limit(source, kLimit);
    for (uint64_t lba = 0; lba < 4000; lba++)
        ASSERT_TRUE(source.WriteSector(lba, Data(lba, 1).data()));
    DeltaIdentity identity;
    identity.contentId = base->ContentId();
    identity.sectorCount = kSectors;
    const auto path = folder.Path() / "disk.delta";
    ASSERT_TRUE(SessionDelta::Save(path, source, identity).Ok());
    std::filesystem::resize_file(path, std::filesystem::file_size(path) - 20);

    SessionWriteMap target(std::make_unique<SharedBase>(base));
    Limit(target, kLimit);
    ASSERT_TRUE(target.WriteSector(7, Data(7, 9).data()));
    std::string detail;
    EXPECT_EQ(SessionDelta::Load(path, target, identity, detail), DeltaLoad::Damaged);
    EXPECT_EQ(target.ChangedSectors(), 1u) << "untouched: " << detail;
    uint8_t sector[512];
    ASSERT_TRUE(target.ReadSector(7, sector));
    EXPECT_EQ(sector[0], Data(7, 9)[0]);
}

TEST(SessionSpill_Test, DiscardDropsTheSpillFile)
{
    ScratchFolder folder("session-spill-discard");
    SpillHere here(folder.Path());
    SessionWriteMap map(MakeBase());
    Limit(map, 64 * 1024);
    for (uint64_t lba = 0; lba < 1000; lba++)
        ASSERT_TRUE(map.WriteSector(lba, Data(lba, 2).data()));
    ASSERT_FALSE(map.SpillPath().empty());
    const uint64_t generation = map.Generation();
    map.Discard();
    EXPECT_TRUE(map.SpillPath().empty());
    EXPECT_EQ(SpillFiles(folder.Path()), 0u);
    EXPECT_GT(map.Generation(), generation);
    EXPECT_EQ(map.HotBytes(), 0u);

    // And it spills again afterwards
    for (uint64_t lba = 0; lba < 1000; lba++)
        ASSERT_TRUE(map.WriteSector(lba, Data(lba, 3).data()));
    EXPECT_GT(map.SpilledSectors(), 0u);
    uint8_t sector[512];
    ASSERT_TRUE(map.ReadSector(0, sector));
    EXPECT_EQ(sector[0], Data(0, 3)[0]);
}
