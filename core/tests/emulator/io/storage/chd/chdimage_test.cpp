// A CHD as a block device (chdimage.h): sectors, the GDDD geometry, the hunk cache, and what it refuses.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>

#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/chd/chdimage.h"
#include "chdtesthelper.h"
#include "emulator/io/storage/chd/chdwriter.h"

using namespace chdtest;

TEST(ChdImage_Test, SectorsGeometryAndTheHunkCache)
{
    const std::vector<uint8_t> image = MixedImage();
    std::string error;
    auto chd = ChdImage::Open(Fixture("mixed-default.chd"), &error);
    ASSERT_NE(chd, nullptr) << error;
    EXPECT_EQ(chd->SectorCount(), 512u);
    EXPECT_FALSE(chd->IsWritable());
    ASSERT_TRUE(chd->NativeGeometry().has_value());
    EXPECT_EQ(chd->NativeGeometry()->cylinders, 8u);
    EXPECT_EQ(chd->NativeGeometry()->heads, 4u);
    EXPECT_EQ(chd->NativeGeometry()->sectors, 16u);

    // The eight sectors of hunk 12 (FLAC): one decode
    uint8_t sector[512];
    for (uint64_t lba = 96; lba < 104; lba++)
    {
        ASSERT_TRUE(chd->ReadSector(lba, sector));
        EXPECT_EQ(0, std::memcmp(sector, image.data() + lba * 512, 512)) << "sector " << lba;
    }
    EXPECT_EQ(chd->HunkReads(), 1u);
    for (uint64_t lba = 0; lba < chd->SectorCount(); lba++)
    {
        ASSERT_TRUE(chd->ReadSector(lba, sector));
        ASSERT_EQ(0, std::memcmp(sector, image.data() + lba * 512, 512)) << "sector " << lba;
    }
    EXPECT_EQ(chd->HunkReads(), kHunks) << "every hunk decoded once";
    EXPECT_FALSE(chd->ReadSector(512, sector));
    EXPECT_FALSE(chd->WriteSector(0, sector));

    auto again = ChdImage::Open(Fixture("mixed-default.chd"));
    auto other = ChdImage::Open(Fixture("mixed-lzma.chd"));
    ASSERT_NE(again, nullptr);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(chd->ContentId(), again->ContentId());
    EXPECT_NE(chd->ContentId(), other->ContentId());
}

TEST(ChdImage_Test, OnlyFiveHundredTwelveByteSectors)
{
    ScratchFolder folder("chd-bps");
    chd::WriteOptions options;
    options.metadata = {chd::HardDiskMetadata(BlockGeometry{16, 1, 1}, 256)};
    const std::vector<uint8_t> disk(4096, 0x11);
    const auto u8 = (folder.Path() / "bps256.chd").u8string();
    const std::string path(u8.begin(), u8.end());
    std::string error;
    ASSERT_TRUE(chd::WriteChd(path, disk.size(), [&disk](uint32_t, uint8_t* dst, std::string*) {
        std::memcpy(dst, disk.data(), 4096);
        return true;
    }, options, &error)) << error;
    EXPECT_EQ(ChdImage::Open(path, &error), nullptr);
    EXPECT_NE(error.find("256-byte sectors"), std::string::npos) << error;
}
