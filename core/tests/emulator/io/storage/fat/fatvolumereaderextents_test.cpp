// FatVolumeReader for image sources: ChainExtents, partitions, the FAT window
// cache (multi-source phases/c3-image-sources.md §3, §5)

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/subrangedevice.h"

namespace
{
    constexpr uint64_t kFloppyDataStart = 33;  ///< Fat12Floppy: 1 reserved + 2 x 9 FAT + 14 root sectors

    std::vector<uint8_t> Bytes(size_t size, uint8_t seed)
    {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; i++)
            data[i] = static_cast<uint8_t>(seed + i * 7 + i / 512);
        return data;
    }

    /// FAT12 entry `cluster` of the first FAT of a Fat12Floppy
    void SetFat12(SparseDisk& disk, uint32_t cluster, uint32_t value)
    {
        uint8_t s[1024];
        const uint32_t at = cluster + cluster / 2;
        const uint64_t sector = 1 + at / 512;
        disk.ReadSector(sector, s);
        disk.ReadSector(sector + 1, s + 512);
        const uint32_t within = at % 512;
        uint16_t v = static_cast<uint16_t>(s[within] | (s[within + 1] << 8));
        v = (cluster & 1) ? static_cast<uint16_t>((v & 0x000F) | (value << 4)) : static_cast<uint16_t>((v & 0xF000) | value);
        s[within] = static_cast<uint8_t>(v);
        s[within + 1] = static_cast<uint8_t>(v >> 8);
        disk.WriteSector(sector, s);
        disk.WriteSector(sector + 1, s + 512);
    }
}  // namespace

/// A contiguous file is one extent; a chain 2 -> 5 -> 4 is three; extents are device LBAs
TEST(FatVolumeReader_Test, ChainExtentsCoalesces)
{
    auto disk = Fat12Floppy({{"A.BIN", Bytes(3 * 512, 1)}, {"B.BIN", Bytes(3 * 512, 2)}});
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*disk));
    ASSERT_EQ(reader.Type(), FatReaderType::Fat12);

    std::vector<FatChainExtent> extents;
    ASSERT_TRUE(reader.ChainExtents(2, 3 * 512, extents));
    ASSERT_EQ(extents.size(), 1u);
    EXPECT_EQ(extents[0].lba, kFloppyDataStart);
    EXPECT_EQ(extents[0].sectors, 3u);

    ASSERT_TRUE(reader.ChainExtents(2, 100, extents));
    ASSERT_EQ(extents.size(), 1u);
    EXPECT_EQ(extents[0].sectors, 1u) << "cut to the file's size";

    SetFat12(*disk, 2, 5);
    SetFat12(*disk, 5, 4);
    SetFat12(*disk, 4, 0xFFF);
    ASSERT_TRUE(reader.Open(*disk));
    ASSERT_TRUE(reader.ChainExtents(2, 3 * 512, extents));
    ASSERT_EQ(extents.size(), 3u);
    EXPECT_EQ(extents[0].lba, kFloppyDataStart + 0);
    EXPECT_EQ(extents[1].lba, kFloppyDataStart + 3);
    EXPECT_EQ(extents[2].lba, kFloppyDataStart + 2);
}

/// Broken chains fail with the reason: shorter than the file, outside the volume, a loop
TEST(FatVolumeReader_Test, ChainExtentsBrokenChains)
{
    auto disk = Fat12Floppy({{"A.BIN", Bytes(3 * 512, 1)}});
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*disk));
    std::vector<FatChainExtent> extents;
    std::string error;
    EXPECT_FALSE(reader.ChainExtents(2, 4 * 512, extents, &error));
    EXPECT_NE(error.find("shorter"), std::string::npos) << error;

    SetFat12(*disk, 3, 0xF00);  // a cluster number past the volume
    ASSERT_TRUE(reader.Open(*disk));
    EXPECT_FALSE(reader.ChainExtents(2, 3 * 512, extents, &error));
    EXPECT_NE(error.find("leaves the volume"), std::string::npos) << error;

    SetFat12(*disk, 3, 2);  // 2 -> 3 -> 2
    ASSERT_TRUE(reader.Open(*disk));
    EXPECT_FALSE(reader.ChainExtents(2, 4u << 20, extents, &error));
    EXPECT_NE(error.find("loops"), std::string::npos) << error;
}

/// A 400-cluster FAT12 file crosses FAT sectors (and an entry straddling two sectors):
/// one extent, the bytes right, a handful of FAT sector reads instead of two per cluster
TEST(FatVolumeReader_Test, ChainExtentsReadsFewFatSectors)
{
    const std::vector<uint8_t> big = Bytes(400 * 512, 9);
    auto disk = Fat12Floppy({{"BIG.BIN", big}});
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*disk));
    const uint64_t before = reader.FatSectorReads();
    std::vector<FatChainExtent> extents;
    ASSERT_TRUE(reader.ChainExtents(2, big.size(), extents));
    ASSERT_EQ(extents.size(), 1u);
    EXPECT_EQ(extents[0].sectors, 400u);
    EXPECT_LE(reader.FatSectorReads() - before, 4u) << "a sliding two-sector window over 2 FAT sectors";

    std::vector<uint8_t> data;
    ASSERT_TRUE(reader.ReadFile("/BIG.BIN", data));
    EXPECT_EQ(data, big);
}

/// FindPartition picks MBR entry n; the reader on a SubRangeDevice of it sees that volume
TEST(FatVolumeReader_Test, SelectsPartitionN)
{
    ScratchFolder one("reader-part-1");
    one.File("first.txt", "partition one");
    ScratchFolder two("reader-part-2");
    two.File("second.txt", "partition two");
    const FatSourceImage a = FolderToFatDisk(one.Path(), FatType::Fat16, CodePage::Cp866, false);
    const FatSourceImage b = FolderToFatDisk(two.Path(), FatType::Fat16, CodePage::Cp866, false);
    ASSERT_TRUE(a.ok()) << a.error;
    ASSERT_TRUE(b.ok()) << b.error;

    const uint64_t firstStart = 2048;
    const uint64_t secondStart = firstStart + a.disk->SectorCount();
    auto disk = std::make_shared<SparseDisk>(secondStart + b.disk->SectorCount());
    uint8_t s[512];
    for (uint64_t lba = 0; lba < a.usedEnd; lba++)
    {
        a.disk->ReadSector(lba, s);
        disk->WriteSector(firstStart + lba, s);
    }
    for (uint64_t lba = 0; lba < b.usedEnd; lba++)
    {
        b.disk->ReadSector(lba, s);
        disk->WriteSector(secondStart + lba, s);
    }
    std::memset(s, 0, sizeof s);
    auto entry = [&s](int index, uint8_t type, uint64_t first, uint64_t count) {
        uint8_t* e = s + 446 + index * 16;
        e[4] = type;
        for (int i = 0; i < 4; i++)
        {
            e[8 + i] = static_cast<uint8_t>(first >> (8 * i));
            e[12 + i] = static_cast<uint8_t>(count >> (8 * i));
        }
    };
    entry(0, 0x06, firstStart, a.disk->SectorCount());
    entry(1, 0x0E, secondStart, b.disk->SectorCount());
    entry(2, 0x83, 1, 1);  // not FAT
    s[510] = 0x55;
    s[511] = 0xAA;
    disk->WriteSector(0, s);

    FatPartition partition;
    ASSERT_TRUE(FatVolumeReader::FindPartition(*disk, 2, partition));
    EXPECT_EQ(partition.first, secondStart);
    SubRangeDevice window(disk, partition.first, partition.count);
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(window));
    std::vector<uint8_t> data;
    ASSERT_TRUE(reader.ReadFile("/second.txt", data));
    EXPECT_EQ(std::string(data.begin(), data.end()), "partition two");

    std::string error;
    EXPECT_FALSE(FatVolumeReader::FindPartition(*disk, 3, partition, &error));
    EXPECT_NE(error.find("#83"), std::string::npos) << error;
    EXPECT_FALSE(FatVolumeReader::FindPartition(*disk, 5, partition, &error));
    EXPECT_FALSE(FatVolumeReader::FindPartition(*a.disk, 1, partition, &error)) << "a superfloppy has no partitions";
}

TEST(FatVolumeReader_Test, DosToUnix)
{
    EXPECT_EQ(FatVolumeReader::DosToUnix((46 << 9) | (1 << 5) | 1, 12 << 11), 1767268800) << "2026-01-01 12:00:00";
    EXPECT_EQ(FatVolumeReader::DosToUnix((0 << 9) | (1 << 5) | 1, 0), 315532800) << "the DOS epoch";
    EXPECT_EQ(FatVolumeReader::DosToUnix((20 << 9) | (2 << 5) | 29, (23 << 11) | (59 << 5) | 29), 951868798)
        << "2000-02-29 23:59:58";
}

/// Reads and writes inside the window map to first + lba; outside fail; writable as the base
TEST(SubRangeDevice_Test, Bounds)
{
    auto base = std::make_shared<MemoryDisk>(100);
    uint8_t s[512];
    std::memset(s, 0x42, sizeof s);
    ASSERT_TRUE(base->WriteSector(15, s));

    SubRangeDevice window(base, 10, 20);
    EXPECT_EQ(window.SectorCount(), 20u);
    uint8_t got[512] = {};
    ASSERT_TRUE(window.ReadSector(5, got));
    EXPECT_EQ(got[0], 0x42);
    EXPECT_FALSE(window.ReadSector(20, got));
    EXPECT_FALSE(window.WriteSector(20, s));
    std::memset(s, 0x17, sizeof s);
    ASSERT_TRUE(window.WriteSector(0, s));
    ASSERT_TRUE(base->ReadSector(10, got));
    EXPECT_EQ(got[0], 0x17);
    EXPECT_NE(window.ContentId(), base->ContentId());
    EXPECT_NE(window.ContentId(), SubRangeDevice(base, 11, 20).ContentId());

    base->SetWritable(false);
    EXPECT_FALSE(window.IsWritable());
    EXPECT_FALSE(window.WriteSector(0, s));
}
