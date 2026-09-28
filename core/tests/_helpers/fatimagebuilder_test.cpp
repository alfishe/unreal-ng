// FatImageBuilder: the FAT test images are what the spec says (the real
// check is guest software reading them: the ERS SD boot test, NeoGS)

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <vector>

#include "_helpers/fatimagebuilder.h"

namespace
{
    std::vector<uint8_t> ReadAll(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        std::vector<uint8_t> bytes(static_cast<size_t>(in.tellg()));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return bytes;
    }

    uint32_t Get32(const std::vector<uint8_t>& b, size_t at)
    {
        return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
    }
}  // namespace

TEST(FatImageBuilder_Test, Fat16WithMbrHoldsTheFile)
{
    FatImageSpec spec;
    spec.sizeBytes = 4ull << 20;
    spec.files = {{"hello.bin", std::vector<uint8_t>(1000, 0x5A)}};
    ScratchFatImage image("fat16-mbr.img", spec);
    ASSERT_TRUE(image.ok()) << image.error();

    const auto bytes = ReadAll(image.path());
    ASSERT_EQ(bytes.size(), spec.sizeBytes);
    EXPECT_EQ(bytes[510], 0x55);
    EXPECT_EQ(bytes[511], 0xAA);
    EXPECT_EQ(bytes[0x1BE + 4], 0x06) << "partition type FAT16";
    EXPECT_EQ(Get32(bytes, 0x1BE + 8), 2048u) << "partition at LBA 2048";

    const size_t boot = 2048 * 512;
    EXPECT_EQ(std::memcmp(&bytes[boot + 0x36], "FAT16   ", 8), 0);
    EXPECT_EQ(bytes[boot + 510], 0x55);

    // The file is in the root directory as HELLO   BIN, its data in the data area
    const auto& layout = image.layout();
    const size_t fatStart = boot + 512;  // one reserved sector
    const size_t root = fatStart + 2 * static_cast<size_t>(layout.fatSectors) * 512;
    bool found = false;
    for (size_t entry = root; entry < root + 512 * 32; entry += 32)
    {
        if (std::memcmp(&bytes[entry], "HELLO   BIN", 11) == 0)
        {
            found = true;
            EXPECT_EQ(Get32(bytes, entry + 28), 1000u) << "file size";
            const uint16_t cluster = static_cast<uint16_t>(bytes[entry + 26] | (bytes[entry + 27] << 8));
            const size_t data = root + 512 * 32 + static_cast<size_t>(cluster - 2) * layout.sectorsPerCluster * 512;
            EXPECT_EQ(bytes[data], 0x5A);
            EXPECT_EQ(bytes[data + 999], 0x5A);
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(FatImageBuilder_Test, SameSpecGivesTheSameBytes)
{
    FatImageSpec spec;
    spec.sizeBytes = 4ull << 20;  // the smallest FAT16 volume after the 1 MiB partition gap
    spec.files = {{"A.TXT", {'a'}}, {"B.TXT", {'b', 'b'}}};
    ScratchFatImage first("fat16-a.img", spec);
    ScratchFatImage second("fat16-b.img", spec);
    ASSERT_TRUE(first.ok() && second.ok());
    EXPECT_EQ(ReadAll(first.path()), ReadAll(second.path()));
}

TEST(FatImageBuilder_Test, RefusesWhatCannotBeBuilt)
{
    FatImageSpec tooSmall;
    tooSmall.fat = 32;
    tooSmall.sizeBytes = 8ull << 20;  // FAT32 needs 65 525+ clusters
    ScratchFatImage image("fat32-small.img", tooSmall);
    EXPECT_FALSE(image.ok());
    EXPECT_FALSE(image.error().empty());
}
