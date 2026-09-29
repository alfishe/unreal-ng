// FatImageBuilder: the FAT test images are what the spec says (the real
// check is guest software reading them: the ERS SD boot test, NeoGS)

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "_helpers/fatimagebuilder.h"
#include "_helpers/neogstestsdcard.h"

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

namespace
{
// NeoGS cards: read a root-directory file back through its FAT chain
std::vector<uint8_t> readAll(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

uint32_t le(const std::vector<uint8_t>& b, size_t at, int bytes)
{
    uint32_t v = 0;
    for (int i = bytes - 1; i >= 0; i--)
        v = (v << 8) | b[at + static_cast<size_t>(i)];
    return v;
}

/// Reads a root-directory file back by following its FAT chain
std::vector<uint8_t> readBack(const std::vector<uint8_t>& img, const std::string& name11)
{
    const size_t mbrStart = (img[510] == 0x55 && img[450] != 0) ? le(img, 454, 4) * 512ull : 0;
    const size_t vol = mbrStart;
    const uint32_t spc = img[vol + 13];
    const uint32_t reserved = le(img, vol + 14, 2);
    const uint32_t rootEntries = le(img, vol + 17, 2);
    const bool fat32 = rootEntries == 0;
    const uint32_t fatSectors = fat32 ? le(img, vol + 36, 4) : le(img, vol + 22, 2);
    const size_t fatAt = vol + reserved * 512ull;
    const size_t rootAt = vol + (reserved + 2ull * fatSectors) * 512;
    const size_t dataAt = rootAt + rootEntries * 32ull;
    const auto clusterAt = [&](uint32_t c) { return dataAt + static_cast<size_t>(c - 2) * spc * 512; };
    const auto next = [&](uint32_t c) { return fat32 ? le(img, fatAt + c * 4ull, 4) & 0x0FFFFFFF : le(img, fatAt + c * 2ull, 2); };
    const size_t dir = fat32 ? clusterAt(le(img, vol + 44, 4)) : rootAt;
    for (size_t e = dir; img[e] != 0; e += 32)
    {
        if (memcmp(&img[e], name11.data(), 11) != 0)
            continue;
        uint32_t cluster = le(img, e + 26, 2) | (le(img, e + 20, 2) << 16);
        uint32_t left = le(img, e + 28, 4);
        std::vector<uint8_t> out;
        while (left > 0)
        {
            const uint32_t take = std::min<uint32_t>(left, spc * 512);
            out.insert(out.end(), img.begin() + static_cast<std::ptrdiff_t>(clusterAt(cluster)),
                       img.begin() + static_cast<std::ptrdiff_t>(clusterAt(cluster) + take));
            left -= take;
            cluster = next(cluster);
        }
        return out;
    }
    return {};
}
} // namespace

TEST(FatImageBuilder, NeoGSCardsHoldTheirFilesAndValidLayouts)
{
    for (NeoGSTestSd card : {NeoGSTestSd::Fat16Mbr, NeoGSTestSd::Fat16NoMbr, NeoGSTestSd::Fat32Mbr})
    {
        SCOPED_TRACE(NeoGSTestSdName(card));
        const FatImageSpec spec = NeoGSTestSdSpec(card);
        auto image = MakeNeoGSTestSd(card);
        ASSERT_TRUE(image->ok()) << image->error();
        const std::vector<uint8_t> img = readAll(image->path());
        ASSERT_EQ(img.size(), spec.sizeBytes);

        const FatImageLayout& layout = image->layout();
        if (spec.fat == 16)
            EXPECT_TRUE(layout.clusters >= 4085 && layout.clusters < 65525);
        else
            EXPECT_GE(layout.clusters, 65525u);
        EXPECT_EQ(layout.volumeStartSector, spec.mbr ? 2048u : 0u);
        const size_t bs = layout.volumeStartSector * 512;
        EXPECT_EQ(img[bs + 510], 0x55);
        EXPECT_EQ(img[bs + 511], 0xAA);
        if (spec.mbr)
            EXPECT_EQ(img[450], spec.fat == 32 ? 0x0C : 0x06);

        ASSERT_EQ(spec.files.size(), 4u);
        EXPECT_EQ(readBack(img, "NEOGS   ROM"), spec.files[0].data);
        EXPECT_EQ(readBack(img, "NGS_ROM UPD"), spec.files[1].data);
        EXPECT_EQ(readBack(img, "EYEACHE MP3"), spec.files[2].data);
        EXPECT_EQ(readBack(img, "EYE22K  MP3"), spec.files[3].data);
        EXPECT_EQ(spec.files[0].data.size(), 32768u);
    }
}

TEST(FatImageBuilder, ReplacingAnImageKeepsTheNewFile)
{
    auto image = MakeNeoGSTestSd(NeoGSTestSd::Fat16Mbr);
    const std::string first = image->path();
    image = MakeNeoGSTestSd(NeoGSTestSd::Fat16Mbr); // the old one's destructor runs after the new file exists
    EXPECT_NE(image->path(), first);
    EXPECT_TRUE(std::filesystem::exists(image->path()));
    EXPECT_FALSE(std::filesystem::exists(first));
}
