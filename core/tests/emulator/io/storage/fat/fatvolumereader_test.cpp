// FatVolumeReader: the independent FAT reader (the test oracle for HostFolderFat)
// checked against images it did not build: the FatImageBuilder test helper
// (FAT16 behind an MBR, FAT32 superfloppy) and a FAT12 volume laid out by hand

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatimagebuilder.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/rawimage.h"

namespace
{
    std::vector<uint8_t> Pattern(size_t size, uint8_t seed)
    {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; i++)
            data[i] = static_cast<uint8_t>(i * 13 + seed);
        return data;
    }

    const FatDirEntryInfo* FindEntry(const std::vector<FatDirEntryInfo>& entries, const std::string& name)
    {
        auto it = std::find_if(entries.begin(), entries.end(), [&name](const FatDirEntryInfo& e) { return e.name == name; });
        return it == entries.end() ? nullptr : &*it;
    }

    /// 64 sectors, one sector per cluster: 60 clusters, so FAT12 by count.
    /// Root: the label, then "<0x80>BC.TXT" (600 bytes in clusters 2 -> 3)
    std::unique_ptr<MemoryDisk> HandMadeFat12()
    {
        auto disk = std::make_unique<MemoryDisk>(64);
        uint8_t* d = disk->Data();
        const uint8_t boot[] = {0xEB, 0x3C, 0x90, 'M', 'S', 'D', 'O', 'S', '5', '.', '0',
                                0x00, 0x02,   // 512 bytes per sector
                                0x01,         // sectors per cluster
                                0x01, 0x00,   // reserved sectors
                                0x02,         // FATs
                                0x10, 0x00,   // root entries
                                0x40, 0x00,   // total sectors
                                0xF8,         // media
                                0x01, 0x00};  // sectors per FAT
        std::memcpy(d, boot, sizeof(boot));
        d[510] = 0x55;
        d[511] = 0xAA;
        // FAT12: entries 0-1 reserved, cluster 2 -> 3, cluster 3 = end of chain
        const uint8_t fat[] = {0xF8, 0xFF, 0xFF, 0x03, 0xF0, 0xFF};
        std::memcpy(d + 1 * 512, fat, sizeof(fat));
        std::memcpy(d + 2 * 512, fat, sizeof(fat));
        uint8_t* root = d + 3 * 512;
        std::memcpy(root, "HANDMADE   ", 11);
        root[11] = 0x08;
        uint8_t* file = root + 32;
        std::memcpy(file, "\x80" "BC     TXT", 11);
        file[11] = 0x20;
        file[26] = 0x02;                          // first cluster
        file[28] = 600 & 0xFF;                    // size
        file[29] = 600 >> 8;
        const std::vector<uint8_t> data = Pattern(600, 7);
        std::memcpy(d + 4 * 512, data.data(), data.size());  // cluster 2 = sector 4, cluster 3 = sector 5
        return disk;
    }
}  // namespace

TEST(FatVolumeReader_Test, Fat16BehindAnMbr)
{
    FatImageSpec spec;
    spec.fat = 16;
    spec.sizeBytes = 8ull << 20;
    spec.label = "READER";
    spec.files = {{"SMALL.TXT", {'h', 'e', 'l', 'l', 'o'}}, {"BIG.BIN", Pattern(50000, 3)}};
    ScratchFatImage built("fatreader-fat16.img", spec);
    ASSERT_TRUE(built.ok()) << built.error();
    auto image = RawImage::Open(built.path(), RawImage::Access::ReadOnly);
    ASSERT_NE(image, nullptr);

    FatVolumeReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(*image, CodePage::Cp866, &error)) << error;
    EXPECT_EQ(reader.Type(), FatReaderType::Fat16);
    EXPECT_EQ(reader.VolumeStart(), 2048u);
    EXPECT_EQ(reader.ClusterCount(), built.layout().clusters);
    EXPECT_EQ(reader.Label(), "READER");

    std::vector<FatDirEntryInfo> root;
    ASSERT_TRUE(reader.List("/", root, &error)) << error;
    ASSERT_EQ(root.size(), 2u) << "the label entry is not a file";
    const FatDirEntryInfo* big = FindEntry(root, "BIG.BIN");
    ASSERT_NE(big, nullptr);
    EXPECT_EQ(big->size, 50000u);
    EXPECT_FALSE(big->isDirectory);

    std::vector<uint8_t> data;
    ASSERT_TRUE(reader.ReadFile("/BIG.BIN", data, &error)) << error;
    EXPECT_EQ(data, Pattern(50000, 3)) << "a chain over many clusters";
    ASSERT_TRUE(reader.ReadFile("/small.txt", data, &error)) << "names match ASCII case-insensitively: " << error;
    EXPECT_EQ(std::string(data.begin(), data.end()), "hello");
    EXPECT_FALSE(reader.ReadFile("/MISSING.TXT", data, &error));
    EXPECT_FALSE(error.empty());
}

/// FAT32 needs >= 65 526 clusters: the builder's smallest FAT32 volume
/// (36 MiB, written sparse)
TEST(FatVolumeReader_Test, Fat32Superfloppy)
{
    FatImageSpec spec;
    spec.fat = 32;
    spec.sizeBytes = 36ull << 20;
    spec.mbr = false;
    spec.files = {{"DATA.BIN", Pattern(9000, 5)}};
    ScratchFatImage built("fatreader-fat32.img", spec);
    ASSERT_TRUE(built.ok()) << built.error();
    auto image = RawImage::Open(built.path(), RawImage::Access::ReadOnly);
    ASSERT_NE(image, nullptr);

    FatVolumeReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(*image, CodePage::Cp866, &error)) << error;
    EXPECT_EQ(reader.Type(), FatReaderType::Fat32);
    EXPECT_EQ(reader.VolumeStart(), 0u);
    EXPECT_GE(reader.ClusterCount(), 65526u);
    std::vector<uint8_t> data;
    ASSERT_TRUE(reader.ReadFile("/DATA.BIN", data, &error)) << error;
    EXPECT_EQ(data, Pattern(9000, 5));
}

/// The type comes from the cluster count alone; short names decode through
/// the code page asked for
TEST(FatVolumeReader_Test, Fat12ByClusterCountAndShortNameCodePages)
{
    auto disk = HandMadeFat12();
    FatVolumeReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(*disk, CodePage::Cp866, &error)) << error;
    EXPECT_EQ(reader.Type(), FatReaderType::Fat12);
    EXPECT_EQ(reader.ClusterCount(), 60u);
    EXPECT_EQ(reader.Label(), "HANDMADE");

    std::vector<FatDirEntryInfo> root;
    ASSERT_TRUE(reader.List("/", root, &error)) << error;
    ASSERT_EQ(root.size(), 1u);
    EXPECT_EQ(root[0].name, "\xD0\x90" "BC.TXT") << "CP866 #80 is U+0410";
    std::vector<uint8_t> data;
    ASSERT_TRUE(reader.ReadFile("/" + root[0].name, data, &error)) << error;
    EXPECT_EQ(data, Pattern(600, 7)) << "a two-cluster chain through packed 12-bit entries";

    ASSERT_TRUE(reader.Open(*disk, CodePage::Cp1251, &error)) << error;
    ASSERT_TRUE(reader.List("/", root, &error)) << error;
    ASSERT_EQ(root.size(), 1u);
    EXPECT_EQ(root[0].name, "\xD0\x82" "BC.TXT") << "CP1251 #80 is U+0402";
}

TEST(FatVolumeReader_Test, RejectsWhatIsNotAFatVolume)
{
    MemoryDisk blank(64);
    FatVolumeReader reader;
    std::string error;
    EXPECT_FALSE(reader.Open(blank, CodePage::Cp866, &error));
    EXPECT_FALSE(error.empty());

    // A partition table without a FAT partition
    blank.Data()[510] = 0x55;
    blank.Data()[511] = 0xAA;
    blank.Data()[446 + 4] = 0x83;  // Linux
    blank.Data()[446 + 8] = 0x01;
    error.clear();
    EXPECT_FALSE(reader.Open(blank, CodePage::Cp866, &error));
    EXPECT_NE(error.find("no FAT partition"), std::string::npos) << error;
}
