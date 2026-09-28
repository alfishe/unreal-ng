// The run-time FAT image builder used by the SD card tests (fatimagebuilder.h)

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <vector>

#include "_helpers/fatimagebuilder.h"
#include "_helpers/neogstestsdcard.h"

namespace
{
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

        ASSERT_EQ(spec.files.size(), 3u);
        EXPECT_EQ(readBack(img, "NEOGS   ROM"), spec.files[0].data);
        EXPECT_EQ(readBack(img, "NGS_ROM UPD"), spec.files[1].data);
        EXPECT_EQ(readBack(img, "EYEACHE MP3"), spec.files[2].data);
        EXPECT_EQ(spec.files[0].data.size(), 32768u);
    }
}

TEST(FatImageBuilder, SameSpecGivesTheSameBytes)
{
    const FatImageSpec spec = NeoGSTestSdSpec(NeoGSTestSd::Fat32Mbr);
    ScratchFatImage a("fat-a.img", spec);
    ScratchFatImage b("fat-b.img", spec);
    ASSERT_TRUE(a.ok() && b.ok());
    EXPECT_EQ(readAll(a.path()), readAll(b.path()));
}

TEST(FatImageBuilder, RefusesWhatCannotBeBuilt)
{
    FatImageSpec spec;
    spec.fat = 32;
    spec.sizeBytes = 8ull << 20; // too few clusters for FAT32
    ScratchFatImage tooSmall("fat-small.img", spec);
    EXPECT_FALSE(tooSmall.ok());
    EXPECT_NE(tooSmall.error().find("FAT32"), std::string::npos) << tooSmall.error();

    spec.fat = 16;
    spec.files = {{"TOOLONGNAME.ROM", {1}}};
    ScratchFatImage badName("fat-name.img", spec);
    EXPECT_FALSE(badName.ok());
}
