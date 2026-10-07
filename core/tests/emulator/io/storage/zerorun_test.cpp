// IBlockDevice::ZeroRun (multi-source phases/c10-sparse-memory.md §2): a run a device reports as known zeros reads as
// zeros, on every device that answers; exports and the CHD writer skip such runs without reading them. And the
// SparseMemoryDisk that blank media use (§3).

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/compose/graftvolume.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/partitioneddisk.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/io/storage/sparsememorydisk.h"
#include "emulator/io/storage/subrangedevice.h"
#include "emulator/media/blockformats.h"
#include "emulator/media/compositemediumfactory.h"

namespace
{
    /// Every claimed run reads zeros: checked from many places (each run's first and last 32 sectors)
    uint64_t CheckRuns(IBlockDevice& device, int samples = 200)
    {
        std::mt19937_64 random(42);
        uint64_t claimed = 0;
        uint8_t sector[512];
        for (int i = 0; i < samples; i++)
        {
            const uint64_t lba = random() % device.SectorCount();
            const uint64_t run = device.ZeroRun(lba);
            EXPECT_LE(lba + run, device.SectorCount()) << lba;
            claimed += run;
            for (uint64_t s = lba; s < lba + run; s++)
            {
                if (s == lba + 32 && run > 64)
                    s = lba + run - 32;
                EXPECT_TRUE(device.ReadSector(s, sector));
                if (!std::all_of(sector, sector + 512, [](uint8_t b) { return b == 0; }))
                {
                    ADD_FAILURE() << device.Describe() << ": sector " << s << " in the run from " << lba << " is not zero";
                    return claimed;
                }
            }
        }
        return claimed;
    }

    /// Counts the sectors read through it (ZeroRun passes through, or is hidden)
    class Counting : public IBlockDevice
    {
    public:
        Counting(IBlockDevice& inner, bool zeroRuns) : _inner(inner), _zeroRuns(zeroRuns) {}
        uint64_t SectorCount() const override { return _inner.SectorCount(); }
        bool ReadSector(uint64_t lba, uint8_t* dst) override
        {
            reads++;
            return _inner.ReadSector(lba, dst);
        }
        bool WriteSector(uint64_t, const uint8_t*) override { return false; }
        bool IsWritable() const override { return false; }
        std::string Describe() const override { return "counting"; }
        uint64_t ContentId() const override { return _inner.ContentId(); }
        uint64_t ZeroRun(uint64_t lba) override { return _zeroRuns ? _inner.ZeroRun(lba) : 0; }
        uint64_t reads = 0;

    private:
        IBlockDevice& _inner;
        bool _zeroRuns;
    };

}  // namespace

TEST(SparseMemoryDisk_Test, StoresOnlyWhatIsWritten)
{
    SparseMemoryDisk disk(4ull * 1024 * 1024 * 2);  // 4 GiB
    EXPECT_EQ(disk.StoredBytes(), 0u);
    std::vector<uint8_t> data(512, 0x5A), zero(512, 0), read(512);
    ASSERT_TRUE(disk.WriteSector(1000, zero.data()));
    EXPECT_EQ(disk.StoredBytes(), 0u) << "a zero write stores nothing";
    ASSERT_TRUE(disk.WriteSector(1000, data.data()));
    EXPECT_EQ(disk.StoredBytes(), 64u * 1024);
    ASSERT_TRUE(disk.ReadSector(1000, read.data()));
    EXPECT_EQ(read, data);
    ASSERT_TRUE(disk.ReadSector(1001, read.data()));
    EXPECT_EQ(read, zero);
    EXPECT_EQ(disk.ZeroRun(1000), 0u);
    EXPECT_EQ(disk.ZeroRun(0), 896u) << "up to the stored chunk (sectors 896-1023)";
    EXPECT_EQ(disk.ZeroRun(1024), disk.SectorCount() - 1024);
    ASSERT_TRUE(disk.WriteSector(1000, zero.data()));
    EXPECT_EQ(disk.StoredBytes(), 0u) << "written back to zeros: freed";
    EXPECT_FALSE(disk.ReadSector(disk.SectorCount(), read.data()));
}

TEST(ZeroRun_Test, EveryDeviceClaimsOnlyZeros)
{
    ScratchFolder folder("zerorun");
    folder.File("base/README.TXT", std::string(3000, 'r'));
    folder.File("base/GAMES/ELITE.TRD", std::string(70000, 'e'));
    folder.File("up/TOOL.COM", std::string(9000, 't'));

    // A rebuilt FAT32 volume of 300 MiB: almost all free space
    folder.File("rebuild.ucompose.yaml", "version: 1\ntarget: {fs: fat32, size: 300MiB, build: rebuild}\n"
                                         "layers: [{source: {folder: base}}, {source: {folder: up}, mount: /UP}]\n");
    std::unique_ptr<IBlockDevice> rebuilt;
    CompositeInfo info;
    ASSERT_TRUE(CompositeMediumFactory::Build(ComposeDescriptor::Load(folder.Path() / "rebuild.ucompose.yaml"), {}, rebuilt, info).Ok());
    EXPECT_GT(CheckRuns(*rebuilt), 0u);
    EXPECT_GT(rebuilt->ZeroRun(rebuilt->SectorCount() - 10), 0u) << "the end of the volume is free space";

    // A session over it with writes in the free space: runs stop at them
    SessionWriteMap session(std::move(rebuilt));
    const std::vector<uint8_t> data(512, 0x77);
    const uint64_t written = session.SectorCount() - 100;
    ASSERT_TRUE(session.WriteSector(written, data.data()));
    EXPECT_EQ(session.ZeroRun(written), 0u);
    EXPECT_EQ(session.ZeroRun(written - 5), 5u);
    CheckRuns(session);

    // A graft over a FAT16 image, and a partitioned disk around it
    const FatSourceImage image = FolderToFatDisk(folder.Path() / "base", FatType::Fat16);
    ASSERT_TRUE(image.ok()) << image.error;
    ASSERT_TRUE(SaveSparse(image, folder.Path() / "base.img"));
    folder.File("graft.ucompose.yaml", "version: 1\nlayers: [{source: {image: base.img}}, {source: {folder: up}, mount: /UP}]\n");
    std::unique_ptr<IBlockDevice> grafted;
    ASSERT_TRUE(CompositeMediumFactory::Build(ComposeDescriptor::Load(folder.Path() / "graft.ucompose.yaml"), {}, grafted, info).Ok());
    ASSERT_EQ(info.build, "graft");
    CheckRuns(*grafted);

    folder.File("parts.ucompose.yaml", "version: 1\npartitions:\n  - {source: {image: base.img, partition: 1}}\n"
                                       "  - {fs: fat16, compose: {free: 8MiB, layers: [{source: {folder: up}}]}}\n");
    std::unique_ptr<IBlockDevice> parted;
    ASSERT_TRUE(CompositeMediumFactory::Build(ComposeDescriptor::Load(folder.Path() / "parts.ucompose.yaml"), {}, parted, info).Ok());
    EXPECT_GT(CheckRuns(*parted), 0u);
    EXPECT_EQ(parted->ZeroRun(0), 0u) << "the MBR";

    // A sparse host image file and a window of it
    const auto sparse = folder.Path() / "sparse.img";
    {
        std::ofstream out(sparse, std::ios::binary);
        out << "data";
    }
    std::filesystem::resize_file(sparse, 64ull * 1024 * 1024);
    std::shared_ptr<IBlockDevice> raw(RawImage::Open(FileHelper::FromFsPath(sparse), RawImage::Access::ReadOnly));
    ASSERT_NE(raw, nullptr);
    CheckRuns(*raw);
    SubRangeDevice window(raw, 1000, 50000);
    CheckRuns(window);
    SparseMemoryDisk memory(100000);
    ASSERT_TRUE(memory.WriteSector(5000, data.data()));
    CheckRuns(memory);
}

/// An export of a 1 GiB FAT32 composite holding a file reads the file system's sectors, not the free space
TEST(ZeroRun_Test, ExportSkipsFreeSpaceWithoutReading)
{
    ScratchFolder folder("zerorun-export");
    folder.File("base/A.TXT", std::string(20000, 'a'));
    folder.File("big.ucompose.yaml", "version: 1\ntarget: {fs: fat32, size: 1GiB, build: rebuild}\nlayers: [{source: {folder: base}}]\n");
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    ASSERT_TRUE(CompositeMediumFactory::Build(ComposeDescriptor::Load(folder.Path() / "big.ucompose.yaml"), {}, volume, info).Ok());

    Counting counting(*volume, /*zeroRuns*/ true);
    const auto out = folder.Path() / "big.img";
    ASSERT_TRUE(BlockFormats::Write(counting, FileHelper::FromFsPath(out), {}, {}).Ok());
    EXPECT_EQ(std::filesystem::file_size(out), volume->SectorCount() * 512);
    EXPECT_LT(counting.reads, 20000u) << "of " << volume->SectorCount() << " sectors";

    // The same bytes as reading every sector would give (spot checks at the start and the files)
    Counting all(*volume, /*zeroRuns*/ false);
    std::vector<uint8_t> a(512), b(512);
    std::ifstream in(out, std::ios::binary);
    for (uint64_t lba = 0; lba < 2000; lba++)
    {
        ASSERT_TRUE(all.ReadSector(lba, a.data()));
        in.read(reinterpret_cast<char*>(b.data()), 512);
        ASSERT_EQ(a, b) << lba;
    }

    // A CHD of it as well
    Counting chdCount(*volume, true);
    BlockWriteOptions chd;
    chd.compression = "none";
    ASSERT_TRUE(BlockFormats::Write(chdCount, FileHelper::FromFsPath(folder.Path() / "big.chd"), chd, {}).Ok());
    EXPECT_LT(chdCount.reads, 40000u);
    std::filesystem::remove(out);
}
