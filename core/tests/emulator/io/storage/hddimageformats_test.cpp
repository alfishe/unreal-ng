// Hard-disk and CD image formats (IDE design §12.3, layer 3): probing, the
// data layout of HDF (normal and 8-bit halved), fixed VHD, HDI, ISO

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/hddimageformats.h"

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    /// `sectors` sectors where sector n is filled with (n + 1)
    std::string Payload(size_t sectors, size_t bytesPerSector = 512)
    {
        std::string data;
        for (size_t n = 0; n < sectors; n++)
            data.append(bytesPerSector, static_cast<char>(n + 1));
        return data;
    }

    void PutLe32(std::string& s, size_t at, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            s[at + i] = static_cast<char>(v >> (8 * i));
    }

    void PutBe(std::string& s, size_t at, uint64_t v, int bytes)
    {
        for (int i = 0; i < bytes; i++)
            s[at + i] = static_cast<char>(v >> (8 * (bytes - 1 - i)));
    }

    std::string HdfHeader(uint16_t dataOffset, bool halved, uint16_t c, uint16_t h, uint16_t s)
    {
        std::string header(dataOffset, '\0');
        std::memcpy(header.data(), "RS-IDE\x1A", 7);
        header[7] = 0x11;
        header[8] = halved ? 1 : 0;
        header[9] = static_cast<char>(dataOffset & 0xFF);
        header[10] = static_cast<char>(dataOffset >> 8);
        const size_t identify = 0x16;  // IDENTIFY words 1, 3, 6
        header[identify + 2] = static_cast<char>(c & 0xFF);
        header[identify + 3] = static_cast<char>(c >> 8);
        header[identify + 6] = static_cast<char>(h);
        header[identify + 12] = static_cast<char>(s);
        return header;
    }

    std::vector<uint8_t> Read(IBlockDevice& device, uint64_t lba)
    {
        std::vector<uint8_t> sector(512);
        EXPECT_TRUE(device.ReadSector(lba, sector.data()));
        return sector;
    }
}  // namespace

TEST(HddImageFormats_Test, RawAndIso)
{
    ScratchFolder folder("hddformats-raw");
    const std::string raw = Utf8(folder.File("disk.img", Payload(8)));
    EXPECT_EQ(HddImageFormats::Probe(raw), "raw");

    std::string iso = Payload(80);  // 20 CD blocks
    std::memcpy(iso.data() + 0x8000, "\x01" "CD001", 6);
    const std::string isoPath = Utf8(folder.File("renamed.bin", iso));
    EXPECT_EQ(HddImageFormats::Probe(isoPath), "iso") << "the content decides, not the name";
    auto image = HddImageFormats::Open(isoPath, "iso", RawImage::Access::ReadWrite);
    ASSERT_NE(image, nullptr);
    EXPECT_FALSE(image->IsWritable()) << "a CD image is always read-only";
    EXPECT_EQ(image->SectorCount(), 80u);
}

TEST(HddImageFormats_Test, HdfDataAfterTheHeaderWithItsGeometry)
{
    ScratchFolder folder("hddformats-hdf");
    const std::string path = Utf8(folder.File("disk.hdf", HdfHeader(0x216, false, 20, 4, 16) + Payload(4)));
    ASSERT_EQ(HddImageFormats::Probe(path), "hdf");
    auto image = HddImageFormats::Open(path, "hdf", RawImage::Access::ReadWrite);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->SectorCount(), 4u);
    EXPECT_EQ(Read(*image, 0)[0], 1);
    EXPECT_EQ(Read(*image, 3)[511], 4);
    const auto geometry = image->NativeGeometry();
    ASSERT_TRUE(geometry.has_value());
    EXPECT_EQ(geometry->cylinders, 20u);
    EXPECT_EQ(geometry->heads, 4u);
    EXPECT_EQ(geometry->sectors, 16u);

    std::vector<uint8_t> sector(512, 0xEE);
    ASSERT_TRUE(image->WriteSector(1, sector.data()));
    image.reset();
    std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
    std::string file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(static_cast<uint8_t>(file[0x216 + 512]), 0xEE) << "sector 1 lands after the header";
    EXPECT_EQ(file.substr(0, 7), "RS-IDE\x1A") << "the header is untouched";
}

TEST(HddImageFormats_Test, HalvedHdfStoresLowBytesOnly)
{
    ScratchFolder folder("hddformats-halved");
    std::string data(256 * 2, '\0');
    for (size_t i = 0; i < data.size(); i++)
        data[i] = static_cast<char>(0xA0 + (i / 256));
    const std::string path = Utf8(folder.File("8bit.hdf", HdfHeader(0x80, true, 1, 1, 2) + data));
    auto image = HddImageFormats::Open(path, "hdf", RawImage::Access::ReadWrite);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->SectorCount(), 2u);
    const std::vector<uint8_t> sector = Read(*image, 1);
    EXPECT_EQ(sector[0], 0xA1);
    EXPECT_EQ(sector[1], 0x00) << "the high byte of each word reads 0";
    EXPECT_EQ(sector[510], 0xA1);

    std::vector<uint8_t> word(512);
    for (size_t i = 0; i < 512; i += 2)
    {
        word[i] = 0x5A;
        word[i + 1] = 0xFF;  // dropped: nowhere to store it
    }
    ASSERT_TRUE(image->WriteSector(0, word.data()));
    EXPECT_EQ(Read(*image, 0)[0], 0x5A);
    EXPECT_EQ(Read(*image, 0)[1], 0x00);
}

TEST(HddImageFormats_Test, FixedVhdKeepsItsFooter)
{
    ScratchFolder folder("hddformats-vhd");
    std::string footer(512, '\0');
    std::memcpy(footer.data(), "conectix", 8);
    PutBe(footer, 0x30, 4 * 512, 8);  // current size
    PutBe(footer, 0x38, 2, 2);        // cylinders
    footer[0x3A] = 1;                 // heads
    footer[0x3B] = 2;                 // sectors
    PutBe(footer, 0x3C, 2, 4);        // fixed
    const std::string path = Utf8(folder.File("disk.vhd", Payload(4) + footer));
    ASSERT_EQ(HddImageFormats::Probe(path), "vhd");
    auto image = HddImageFormats::Open(path, "vhd", RawImage::Access::ReadWrite);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->SectorCount(), 4u) << "the footer is not a sector";
    EXPECT_EQ(image->NativeGeometry()->cylinders, 2u);
    std::vector<uint8_t> sector(512, 0x77);
    EXPECT_TRUE(image->WriteSector(3, sector.data()));
    EXPECT_FALSE(image->WriteSector(4, sector.data())) << "past the disk: the footer stays";
    image.reset();
    std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
    std::string file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(file.substr(4 * 512, 8), "conectix");

    // A dynamic VHD is refused with a reason
    PutBe(footer, 0x3C, 3, 4);
    const std::string dynamic = Utf8(folder.File("dynamic.vhd", Payload(4) + footer));
    std::string error;
    EXPECT_EQ(HddImageFormats::Open(dynamic, "vhd", RawImage::Access::ReadOnly, &error), nullptr);
    EXPECT_NE(error.find("fixed"), std::string::npos) << error;
}

TEST(HddImageFormats_Test, HdiHeaderGivesOffsetSizeAndGeometry)
{
    ScratchFolder folder("hddformats-hdi");
    std::string header(0x100, '\0');
    PutLe32(header, 8, 0x100);    // header size = data offset
    PutLe32(header, 12, 3 * 512); // data size
    PutLe32(header, 16, 512);     // sector size
    PutLe32(header, 20, 3);       // sectors per track
    PutLe32(header, 24, 1);       // heads
    PutLe32(header, 28, 1);       // cylinders
    const std::string path = Utf8(folder.File("pc98.hdi", header + Payload(3)));
    ASSERT_EQ(HddImageFormats::Probe(path), "hdi");
    auto image = HddImageFormats::Open(path, "hdi", RawImage::Access::ReadOnly);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->SectorCount(), 3u);
    EXPECT_EQ(Read(*image, 2)[0], 3);
    EXPECT_EQ(image->NativeGeometry()->sectors, 3u);
}
