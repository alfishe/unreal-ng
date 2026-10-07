// PartitionedDisk (multi-source phases/c7-partitions.md §2, §3): the synthesized MBR and EBR chain, the types, the
// hidden-sectors field of each partition's boot sector, and short partition devices.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/partitioneddisk.h"

namespace
{
    uint32_t Get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

    PartitionedDisk::Part Part(const std::string& name, uint64_t sectors, int bits, uint64_t deviceSectors = 0)
    {
        PartitionedDisk::Part p;
        p.name = name;
        p.sectors = sectors;
        p.fatBits = bits;
        p.device = std::make_shared<SparseDisk>(deviceSectors ? deviceSectors : sectors);
        return p;
    }

    struct Entry
    {
        uint8_t type;
        uint32_t first;
        uint32_t count;
        uint8_t chs[3];
    };

    Entry Read(const uint8_t* table, int slot)
    {
        const uint8_t* e = table + 446 + 16 * slot;
        return Entry{e[4], Get32(e + 8), Get32(e + 12), {e[1], e[2], e[3]}};
    }
}  // namespace

TEST(PartitionedDisk_Test, MbrEntriesAndTypes)
{
    std::vector<PartitionedDisk::Part> parts{Part("small", 20 * 2048, 16), Part("dos", 64 * 2048, 16), Part("big", 100 * 2048, 32)};
    std::string error;
    auto disk = PartitionedDisk::Build(std::move(parts), std::nullopt, "test disk", &error);
    ASSERT_NE(disk, nullptr) << error;

    uint8_t mbr[512];
    ASSERT_TRUE(disk->ReadSector(0, mbr));
    EXPECT_EQ(mbr[510], 0x55);
    EXPECT_EQ(mbr[511], 0xAA);
    EXPECT_NE(Get32(mbr + 440), 0u) << "a disk signature";
    const Entry a = Read(mbr, 0), b = Read(mbr, 1), c = Read(mbr, 2), d = Read(mbr, 3);
    EXPECT_EQ(a.type, 0x04) << "FAT16 below 32 MiB";
    EXPECT_EQ(a.first, 2048u);
    EXPECT_EQ(a.count, 20u * 2048);
    EXPECT_EQ(b.type, 0x06);
    EXPECT_EQ(b.first, 2048u + 20 * 2048) << "1 MiB aligned, in order";
    EXPECT_EQ(c.type, 0x0B) << "FAT32 within CHS reach";
    EXPECT_EQ(c.first % 2048, 0u);
    EXPECT_EQ(d.type, 0x00);
    // CHS of LBA 2048 with 255 heads, 63 sectors: cylinder 0, head 32, sector 33
    EXPECT_EQ(a.chs[0], 32);
    EXPECT_EQ(a.chs[1], 33);
    EXPECT_EQ(a.chs[2], 0);
    EXPECT_EQ(disk->SectorCount(), static_cast<uint64_t>(c.first) + c.count);

    uint8_t gap[512];
    std::memset(gap, 0xAA, sizeof gap);
    ASSERT_TRUE(disk->ReadSector(1, gap));
    EXPECT_TRUE(std::all_of(gap, gap + 512, [](uint8_t v) { return v == 0; }));
    EXPECT_FALSE(disk->ReadSector(disk->SectorCount(), gap));
    EXPECT_FALSE(disk->WriteSector(5000, gap)) << "a composite is written through its change layer only";

    EXPECT_EQ(PartitionedDisk::FatType(16, 16450000, 4096), 0x0E) << "FAT16 past CHS reach";
    EXPECT_EQ(PartitionedDisk::FatType(32, 16450000, 4096), 0x0C);
    EXPECT_EQ(PartitionedDisk::FatType(12, 2048, 2880), 0x01);

    std::vector<PartitionedDisk::Part> again{Part("dos", 64 * 2048, 16)};
    EXPECT_EQ(PartitionedDisk::Build(std::move(again), 1000, "small", &error), nullptr);
    EXPECT_NE(error.find("partitions need"), std::string::npos) << error;
}

TEST(PartitionedDisk_Test, LogicalPartitionsInAnEbrChain)
{
    std::vector<PartitionedDisk::Part> parts;
    for (int i = 0; i < 6; i++)
        parts.push_back(Part("p" + std::to_string(i + 1), 8192, 16));
    std::string error;
    auto disk = PartitionedDisk::Build(std::move(parts), std::nullopt, "six", &error);
    ASSERT_NE(disk, nullptr) << error;

    uint8_t mbr[512];
    ASSERT_TRUE(disk->ReadSector(0, mbr));
    std::vector<uint64_t> starts;
    for (int slot = 0; slot < 3; slot++)
        starts.push_back(Read(mbr, slot).first);
    const Entry extended = Read(mbr, 3);
    ASSERT_EQ(extended.type, 0x0F);
    // Walk the EBR chain as DOS does
    uint64_t ebr = extended.first;
    for (int guard = 0; guard < 8 && ebr; guard++)
    {
        uint8_t table[512];
        ASSERT_TRUE(disk->ReadSector(ebr, table));
        ASSERT_EQ(table[510], 0x55);
        const Entry part = Read(table, 0);
        EXPECT_EQ(part.type, 0x04);
        EXPECT_EQ(part.count, 8192u);
        starts.push_back(ebr + part.first);
        const Entry link = Read(table, 1);
        ebr = link.type == 0x05 ? extended.first + link.first : 0;
    }
    ASSERT_EQ(starts.size(), 6u);
    for (size_t i = 0; i < 6; i++)
    {
        EXPECT_EQ(starts[i], disk->Parts()[i].start) << i;
        EXPECT_EQ(starts[i] % 2048, 0u) << i;
        EXPECT_EQ(disk->Parts()[i].logical, i >= 3) << i;
    }
    EXPECT_EQ(extended.first + extended.count, disk->Parts()[5].start + 8192);
}

TEST(PartitionedDisk_Test, HiddenSectorsFollowTheStart)
{
    ScratchFolder folder("partitioned-hidden");
    folder.File("A.TXT", "a");
    const FatSourceImage fat16 = FolderToFatDisk(folder.Path(), FatType::Fat16, CodePage::Cp866, /*mbr*/ false);
    const FatSourceImage fat32 = FolderToFatDisk(folder.Path(), FatType::Fat32, CodePage::Cp866, /*mbr*/ false);
    ASSERT_TRUE(fat16.ok() && fat32.ok());
    uint8_t s[512];
    ASSERT_TRUE(fat16.disk->ReadSector(0, s));
    ASSERT_EQ(Get32(s + 28), 0u) << "built at LBA 0";

    std::vector<PartitionedDisk::Part> parts(2);
    parts[0].name = "a";
    parts[0].device = fat16.disk;
    parts[0].sectors = fat16.disk->SectorCount();
    parts[0].fatBits = 16;
    parts[1].name = "b";
    parts[1].device = fat32.disk;
    parts[1].sectors = fat32.disk->SectorCount();
    parts[1].fatBits = 32;
    std::string error;
    auto disk = PartitionedDisk::Build(std::move(parts), std::nullopt, "hidden", &error);
    ASSERT_NE(disk, nullptr) << error;
    for (const PartitionedDisk::Part& p : disk->Parts())
    {
        ASSERT_TRUE(disk->ReadSector(p.start, s));
        EXPECT_EQ(Get32(s + 28), p.start) << p.name;
    }
    const PartitionedDisk::Part& b = disk->Parts()[1];
    ASSERT_TRUE(disk->ReadSector(b.start + 6, s));
    EXPECT_EQ(Get32(s + 28), b.start) << "the FAT32 backup boot sector too";
    ASSERT_TRUE(disk->ReadSector(b.start + 1, s));
    EXPECT_EQ(std::memcmp(s, "RRaA", 4), 0) << "FSInfo is left alone";
}

TEST(PartitionedDisk_Test, ShortDeviceReadsZeros)
{
    std::vector<PartitionedDisk::Part> parts{Part("cut", 8192, 16, /*deviceSectors*/ 100)};
    const uint8_t ones[512] = {1};
    parts[0].device->WriteSector(99, ones);
    std::string error;
    auto disk = PartitionedDisk::Build(std::move(parts), std::nullopt, "cut", &error);
    ASSERT_NE(disk, nullptr) << error;
    uint8_t s[512];
    ASSERT_TRUE(disk->ReadSector(2048 + 99, s));
    EXPECT_EQ(s[0], 1);
    std::memset(s, 0xAA, sizeof s);
    ASSERT_TRUE(disk->ReadSector(2048 + 100, s));
    EXPECT_EQ(s[0], 0) << "past the device: zeros";
    ASSERT_TRUE(disk->ReadSector(2048 + 8191, s));
    EXPECT_EQ(disk->SectorCount(), 2048u + 8192);
}
