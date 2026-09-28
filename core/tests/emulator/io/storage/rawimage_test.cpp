// RawImage: a plain image file as 512-byte sectors (IDE design §12.3 "Raw")

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/io/storage/rawimage.h"

namespace
{
    /// Sector s is filled with (s + 1); `tail` extra bytes of #77 after the whole sectors
    std::string MakeImage(const char* name, size_t sectors, size_t tail = 0)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        for (size_t s = 0; s < sectors; s++)
        {
            const std::vector<char> sector(IBlockDevice::kSectorSize, static_cast<char>(s + 1));
            out.write(sector.data(), static_cast<std::streamsize>(sector.size()));
        }
        const std::vector<char> extra(tail, 0x77);
        out.write(extra.data(), static_cast<std::streamsize>(extra.size()));
        return path;
    }

    std::vector<uint8_t> ReadFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
    }
}  // namespace

TEST(RawImage_Test, ReadsSectorsAndPadsThePartialLastOne)
{
    const std::string path = MakeImage("raw-read.img", 3, 100);
    auto image = RawImage::Open(path, RawImage::Access::ReadOnly);
    ASSERT_NE(image, nullptr);

    EXPECT_EQ(image->SectorCount(), 4u) << "3 whole sectors + a partial one";
    EXPECT_EQ(image->SizeBytes(), 3u * 512u + 100u);
    EXPECT_FALSE(image->IsWritable());
    EXPECT_EQ(image->Describe(), path);

    uint8_t sector[512];
    ASSERT_TRUE(image->ReadSector(1, sector));
    EXPECT_EQ(sector[0], 2);
    EXPECT_EQ(sector[511], 2);

    ASSERT_TRUE(image->ReadSector(3, sector));
    EXPECT_EQ(sector[99], 0x77);
    EXPECT_EQ(sector[100], 0x00) << "the missing tail reads as zeros";
    EXPECT_FALSE(image->ReadSector(4, sector)) << "past the end";

    image.reset();
    std::remove(path.c_str());
}

TEST(RawImage_Test, ReadOnlyRejectsWritesAndLeavesTheFile)
{
    const std::string path = MakeImage("raw-ro.img", 2);
    const auto before = ReadFile(path);
    {
        auto image = RawImage::Open(path, RawImage::Access::ReadOnly);
        ASSERT_NE(image, nullptr);
        const std::vector<uint8_t> data(512, 0xAA);
        EXPECT_FALSE(image->WriteSector(0, data.data()));
    }
    EXPECT_EQ(ReadFile(path), before);
    std::remove(path.c_str());
}

TEST(RawImage_Test, WriteThroughLandsAtLbaTimes512)
{
    const std::string path = MakeImage("raw-rw.img", 4);
    {
        auto image = RawImage::Open(path, RawImage::Access::ReadWrite);
        ASSERT_NE(image, nullptr);
        EXPECT_TRUE(image->IsWritable());
        std::vector<uint8_t> data(512);
        for (size_t i = 0; i < data.size(); i++)
            data[i] = static_cast<uint8_t>(i);
        ASSERT_TRUE(image->WriteSector(2, data.data()));
        EXPECT_FALSE(image->WriteSector(4, data.data())) << "past the end";

        uint8_t back[512];
        ASSERT_TRUE(image->ReadSector(2, back));
        EXPECT_EQ(back[300], static_cast<uint8_t>(300));
    }

    const auto file = ReadFile(path);
    ASSERT_EQ(file.size(), 4u * 512u);
    EXPECT_EQ(file[2 * 512 + 0], 0);
    EXPECT_EQ(file[2 * 512 + 255], 255);
    EXPECT_EQ(file[1 * 512 + 511], 2) << "the neighbor sector is untouched";
    EXPECT_EQ(file[3 * 512], 4);

    auto reopened = RawImage::Open(path, RawImage::Access::ReadOnly);
    ASSERT_NE(reopened, nullptr);
    uint8_t back[512];
    ASSERT_TRUE(reopened->ReadSector(2, back));
    EXPECT_EQ(back[1], 1) << "survives close and reopen";
    reopened.reset();
    std::remove(path.c_str());
}

TEST(RawImage_Test, MissingFileOrFolderIsRefusedWithAReason)
{
    std::string error;
    EXPECT_EQ(RawImage::Open(TestPathHelper::GetUniqueTestScratchPath("absent.img"), RawImage::Access::ReadOnly, &error),
              nullptr);
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_EQ(RawImage::Open(TestPathHelper::GetTestScratchPath("."), RawImage::Access::ReadOnly, &error), nullptr)
        << "a folder is not an image";
    EXPECT_FALSE(error.empty());
}

TEST(RawImage_Test, ContentIdFollowsPathAndSize)
{
    const std::string a = MakeImage("raw-id-a.img", 2);
    const std::string b = MakeImage("raw-id-b.img", 2);
    auto imageA = RawImage::Open(a, RawImage::Access::ReadOnly);
    auto imageA2 = RawImage::Open(a, RawImage::Access::ReadOnly);
    auto imageB = RawImage::Open(b, RawImage::Access::ReadOnly);
    ASSERT_TRUE(imageA && imageA2 && imageB);
    EXPECT_EQ(imageA->ContentId(), imageA2->ContentId());
    EXPECT_NE(imageA->ContentId(), imageB->ContentId());
    imageA.reset();
    imageA2.reset();
    imageB.reset();
    std::remove(a.c_str());
    std::remove(b.c_str());
}
