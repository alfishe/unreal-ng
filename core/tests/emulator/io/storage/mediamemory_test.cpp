// What media keep in memory, in every mode (multi-source phases/c10d-session-spill.md §5, c10-sparse-memory.md §8):
// - accounting, exact and fast: a session's in-memory tier stays under its limit and its spill index stays small for
//   every write pattern; a sparse memory disk holds its written chunks and its chunk table only;
// - resident memory of the process, measured: read-only and write-through image files, sessions with and without a
//   limit, a blank medium, a session delta saved and loaded, exports to raw and dynamic VHD, a big composite. Each
//   bound is checked against a control that must grow (an unlimited session); when it does not, the platform's
//   measurement cannot tell and the test skips.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "_helpers/processmemory.h"
#include "_helpers/scratchfolder.h"
#include "common/filehelper.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/io/storage/sparsememorydisk.h"
#include "emulator/io/storage/vhdimage.h"
#include "emulator/media/blockformats.h"
#include "emulator/media/composedescriptor.h"
#include "emulator/media/compositemediumfactory.h"
#include "emulator/media/sessiondelta.h"

namespace
{
    constexpr uint64_t kMiB = 1024 * 1024;

    std::vector<uint8_t> Sector(uint64_t lba, uint32_t round = 0)
    {
        std::vector<uint8_t> data(512, static_cast<uint8_t>(0x40 + round));
        data[0] = static_cast<uint8_t>(lba | 1);
        data[1] = static_cast<uint8_t>(lba >> 8);
        return data;
    }

    /// Points spill files at a test folder, and back afterwards
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

    /// A write pattern over `sectors`: the lba of write i
    struct Pattern
    {
        const char* name;
        std::function<uint64_t(uint64_t i)> lba;
        bool revert = false;  ///< every third write puts the base's data back
    };

    std::vector<Pattern> Patterns(uint64_t sectors)
    {
        auto random = std::make_shared<std::mt19937_64>(11);
        return {
            {"sequential", [](uint64_t i) { return i; }},
            {"stride (one sector per chunk)", [sectors](uint64_t i) { return (i * SessionWriteMap::kChunkSectors) % sectors + i / (sectors / SessionWriteMap::kChunkSectors); }},
            {"random", [random, sectors](uint64_t) { return (*random)() % sectors; }},
            {"hot spot (100 sectors rewritten)", [](uint64_t i) { return i % 100; }},
            {"two chunks in turn", [](uint64_t i) { return (i % 2) * SessionWriteMap::kChunkSectors * 50 + (i / 2) % 128; }},
            {"write and revert", [random, sectors](uint64_t) { return (*random)() % sectors; }, true},
        };
    }
}  // namespace

/// For every pattern: the in-memory tier never exceeds the limit, the index stays a fraction of what was spilled,
/// and what was written reads back
TEST(SessionMemory_Test, InMemoryTierBoundedForEveryPattern)
{
    ScratchFolder folder("session-memory");
    SpillHere here(folder.Path());
    constexpr uint64_t kSectors = 16384;  // 8 MiB
    constexpr uint64_t kLimit = 128 * 1024;
    for (const Pattern& pattern : Patterns(kSectors))
    {
        SessionWriteMap session(std::make_unique<SparseMemoryDisk>(kSectors));
        session.SetArenaBytes(32 * 1024);  // four arenas under the limit
        session.SetMemoryLimit(kLimit);
        std::map<uint64_t, std::vector<uint8_t>> reference;
        for (uint64_t i = 0; i < 2500; i++)
        {
            const uint64_t lba = pattern.lba(i) % kSectors;
            if (pattern.revert && i % 3 == 2)
            {
                ASSERT_TRUE(session.WriteSector(lba, std::vector<uint8_t>(512, 0).data()));
                reference.erase(lba);
            }
            else
            {
                reference[lba] = Sector(lba, static_cast<uint32_t>(i % 7));
                ASSERT_TRUE(session.WriteSector(lba, reference[lba].data()));
            }
            ASSERT_LE(session.HotBytes(), session.MemoryCeiling()) << pattern.name << ", write " << i;
            ASSERT_LE(session.MemoryCeiling(), kLimit * 3 / 2);
        }
        // One leaf of the index (a pointer per group of 1 GiB: 128 KiB), a node per touched group, the arenas' slots
        const uint64_t touchedChunks = (kSectors + SessionWriteMap::kChunkSectors - 1) / SessionWriteMap::kChunkSectors;
        EXPECT_LE(session.IndexBytes(), 128 * 1024 + 16 * 1024 + touchedChunks * 256) << pattern.name;
        EXPECT_EQ(session.ChangedSectors(), reference.size()) << pattern.name;
        std::vector<uint8_t> read(512);
        for (const auto& [lba, data] : reference)
        {
            ASSERT_TRUE(session.ReadSector(lba, read.data()));
            ASSERT_EQ(read, data) << pattern.name << ", sector " << lba;
        }
    }
}

TEST(SessionMemory_Test, UnlimitedHoldsEverySectorInMemory)
{
    SessionWriteMap session(std::make_unique<SparseMemoryDisk>(8192));
    session.SetMemoryLimit(0);
    for (uint64_t lba = 0; lba < 4000; lba++)
        ASSERT_TRUE(session.WriteSector(lba, Sector(lba).data()));
    EXPECT_EQ(session.HotBytes(), 2 * uint64_t(session.ArenaBytes())) << "4000 sectors: two arenas of 2048";
    EXPECT_EQ(session.SpilledSectors(), 0u);
    EXPECT_TRUE(session.SpillPath().empty()) << "no journal without a limit, a name or a timeout";
}

TEST(SessionMemory_Test, LowerLimitSpillsAtOnce)
{
    ScratchFolder folder("session-memory-lower");
    SpillHere here(folder.Path());
    SessionWriteMap session(std::make_unique<SparseMemoryDisk>(8192));
    session.SetArenaBytes(32 * 1024);
    session.SetMemoryLimit(0);
    for (uint64_t lba = 0; lba < 4000; lba++)
        ASSERT_TRUE(session.WriteSector(lba, Sector(lba).data()));
    EXPECT_EQ(session.HotBytes(), 63u * 32 * 1024);
    session.SetMemoryLimit(128 * 1024);
    EXPECT_LE(session.HotBytes(), 128u * 1024);
    EXPECT_EQ(session.ChangedSectors(), 4000u);
    EXPECT_GE(session.SpilledSectors(), 4000u - 4 * 64) << "all but four arenas in the journal";
}

TEST(SessionMemory_Test, NewSessionsTakeTheDefaults)
{
    const SessionSettings old = SessionWriteMap::Defaults();
    const SessionSettings plain;
    EXPECT_EQ(plain.memoryLimit, 16 * kMiB) << "[MEDIA] SessionMemoryLimit: 16 MiB unless a config says otherwise";
    EXPECT_EQ(plain.arenaBytes, 1024u * 1024);
    EXPECT_EQ(plain.flushSeconds, 30u);
    EXPECT_EQ(plain.syncSeconds, 30u);
    EXPECT_FALSE(plain.journal) << "lean by default (c10e §9): [MEDIA] SessionJournal = on or journal: replay";
    EXPECT_EQ(plain.ioThreads, 1u) << "one journal writer thread by default";
    EXPECT_FALSE(old.journal) << "the test runner keeps journals away from test data";

    SessionSettings changed = old;
    changed.memoryLimit = 3 * kMiB;
    changed.arenaBytes = 64 * 1024;
    SessionWriteMap::SetDefaults(changed);
    SessionWriteMap session(std::make_unique<SparseMemoryDisk>(64));
    EXPECT_EQ(session.MemoryLimit(), 3 * kMiB);
    EXPECT_EQ(session.ArenaBytes(), 64u * 1024);
    SessionWriteMap::SetDefaults(old);
}

TEST(SparseMemory_Test, HoldsWrittenChunksAndItsTable)
{
    // A 128 GiB blank card: a pointer per GiB
    SparseMemoryDisk card(128ull * 1024 * kMiB / 512);
    EXPECT_EQ(card.StoredBytes(), 0u);
    EXPECT_EQ(card.TableBytes(), 128 * sizeof(void*));

    // Formatting-like: the first MiB, whole chunks
    for (uint64_t lba = 0; lba < 2048; lba++)
        ASSERT_TRUE(card.WriteSector(lba, Sector(lba).data()));
    EXPECT_EQ(card.StoredBytes(), kMiB);
    EXPECT_EQ(card.TableBytes(), 128 * sizeof(void*) + 16384 * sizeof(void*)) << "the first GiB's leaf";

    // One sector in each of 100 far chunks: a chunk each (the worst case, 64 KiB per scattered sector)
    for (uint64_t i = 0; i < 100; i++)
        ASSERT_TRUE(card.WriteSector(1000000 + i * 1000, Sector(i).data()));
    EXPECT_EQ(card.StoredBytes(), kMiB + 100 * 64 * 1024);

    // Written back to zeros: freed
    const std::vector<uint8_t> zero(512, 0);
    for (uint64_t i = 0; i < 100; i++)
        ASSERT_TRUE(card.WriteSector(1000000 + i * 1000, zero.data()));
    EXPECT_EQ(card.StoredBytes(), kMiB);
}

/// Resident memory per mode. 64 MiB moves through each mode (about 1.5 s in all, over the 50 ms rule on purpose):
/// small enough for the suite, large enough that a mode holding its data stands out from the allocator's noise
TEST(MediaMemory_Test, ResidentGrowthPerMode)
{
    ScratchFolder folder("media-memory");
    SpillHere here(folder.Path());
    constexpr uint64_t kSectors = 64 * kMiB / 512;
    constexpr uint64_t kLimit = 8 * kMiB;
    constexpr uint64_t kSlack = 12 * kMiB;  // the heap's own growth, page tables, the test's buffers

    // The control: an unlimited session holds all 64 MiB
    uint64_t control = 0;
    uint64_t afterDiscard = 0;
    {
        ProcessMemory::Meter meter;
        if (!meter.Known())
            GTEST_SKIP() << "the resident size is not known on this platform";
        SessionWriteMap session(std::make_unique<SparseMemoryDisk>(kSectors));
        session.SetMemoryLimit(0);
        for (uint64_t lba = 0; lba < kSectors; lba++)
            session.WriteSector(lba, Sector(lba).data());
        control = meter.Growth();
        // Its arenas are pages of their own: a discard gives them back to the OS, no heap trim needed
        session.Discard();
        afterDiscard = meter.Growth();
    }
    if (control < 48 * kMiB)
        GTEST_SKIP() << "an unlimited session grew only " << control / kMiB << " MiB for 64 MiB of writes: the measurement cannot "
                        "tell bounded from unbounded here";
    EXPECT_LE(afterDiscard, kSlack) << "after a discard (it held " << control / kMiB << " MiB)";

    // A session with a limit, and what is built from it
    {
        ProcessMemory::Meter meter;
        SessionWriteMap session(std::make_unique<SparseMemoryDisk>(kSectors));
        session.SetMemoryLimit(kLimit);
        for (uint64_t lba = 0; lba < kSectors; lba++)
            session.WriteSector(lba, Sector(lba).data());
        EXPECT_LE(meter.Growth(), kLimit + kSlack) << "a limited session (control: " << control / kMiB << " MiB)";
        EXPECT_GT(session.SpilledSectors(), kSectors / 2);

        ProcessMemory::Meter save;
        DeltaIdentity identity;
        identity.contentId = session.Base().ContentId();
        identity.sectorCount = kSectors;
        ASSERT_TRUE(SessionDelta::Save(folder.Path() / "big.delta", session, identity).Ok());
        EXPECT_LE(save.Growth(), kSlack) << "a delta saved from a spilled session streams";

        ProcessMemory::Meter load;
        {
            SessionWriteMap restored(std::make_unique<SparseMemoryDisk>(kSectors));
            restored.SetMemoryLimit(kLimit);
            std::string detail;
            ASSERT_EQ(SessionDelta::Load(folder.Path() / "big.delta", restored, identity, detail), DeltaLoad::Restored)
                << detail;
            EXPECT_EQ(restored.ChangedSectors(), kSectors);
            EXPECT_LE(load.Growth(), kLimit + kSlack) << "a delta loaded into a limited session";
        }

        ProcessMemory::Meter raw;
        ASSERT_TRUE(BlockFormats::Write(session, FileHelper::FromFsPath(folder.Path() / "big.img"), {}).Ok());
        EXPECT_LE(raw.Growth(), kSlack) << "an export to a raw image";

        ProcessMemory::Meter vhd;
        BlockWriteOptions dynamic;
        dynamic.vhd = "dynamic";
        ASSERT_TRUE(BlockFormats::Write(session, FileHelper::FromFsPath(folder.Path() / "big.vhd"), dynamic).Ok());
        EXPECT_LE(vhd.Growth(), kSlack) << "an export to a dynamic VHD";
    }

    // Image files: read-only and write-through hold nothing per sector
    {
        ProcessMemory::Meter meter;
        auto image = RawImage::Open(FileHelper::FromFsPath(folder.Path() / "big.img"), RawImage::Access::ReadWrite);
        ASSERT_NE(image, nullptr);
        std::vector<uint8_t> sector(512);
        for (uint64_t lba = 0; lba < kSectors; lba++)
            image->ReadSector(lba, sector.data());
        for (uint64_t lba = 0; lba < kSectors; lba++)
            image->WriteSector(lba, Sector(lba, 2).data());
        image->Flush();
        EXPECT_LE(meter.Growth(), kSlack) << "a raw image read and written through";

        ProcessMemory::Meter dynamicMeter;
        auto dynamic = VhdDynamicImage::Open(FileHelper::FromFsPath(folder.Path() / "big.vhd"), VhdDynamicImage::Access::ReadWrite);
        ASSERT_NE(dynamic, nullptr);
        for (uint64_t lba = 0; lba < kSectors; lba++)
            dynamic->ReadSector(lba, sector.data());
        for (uint64_t lba = 0; lba < kSectors; lba += 7)
            dynamic->WriteSector(lba, Sector(lba, 3).data());
        EXPECT_LE(dynamicMeter.Growth(), kSlack) << "a dynamic VHD read and written in place";
    }

    // A blank 8 GiB medium, and a 4 GiB composite with a few files
    {
        ProcessMemory::Meter meter;
        SessionWriteMap blank(std::make_unique<SparseMemoryDisk>(8ull * 1024 * kMiB / 512));
        EXPECT_LE(meter.Growth(), 2 * kMiB + kSlack) << "a blank 8 GiB medium: its chunk table (1 MiB)";

        std::filesystem::create_directories(folder.Path() / "files");
        std::ofstream(folder.Path() / "files" / "A.TXT") << std::string(100000, 'a');
        std::ofstream(folder.Path() / "folder.ucompose.yaml")
            << "version: 1\ntarget: {fs: fat32, size: 4GiB, build: rebuild}\nlayers: [{source: {folder: files}}]\n";
        ProcessMemory::Meter composite;
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo info;
        ASSERT_TRUE(CompositeMediumFactory::Build(ComposeDescriptor::Load(folder.Path() / "folder.ucompose.yaml"), {}, volume, info).Ok());
        EXPECT_LE(composite.Growth(), kSlack) << "a 4 GiB composite: its metadata";
    }
}
