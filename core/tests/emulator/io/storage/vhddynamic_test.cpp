// Dynamic VHD (multi-source phases/c10-sparse-memory.md §4): our writer stores only the blocks holding data, a
// hand-built image with a partial sector bitmap reads as the specification says, writes in place allocate blocks
// at the end and move the footer, and HddImageFormats probes and opens it.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "common/filehelper.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/io/storage/sparsememorydisk.h"
#include "emulator/io/storage/vhdimage.h"
#include "emulator/media/blockformats.h"

namespace
{
    constexpr uint64_t kPerBlock = vhd::kBlockBytes / 512;

    std::vector<uint8_t> Sector(uint8_t fill)
    {
        return std::vector<uint8_t>(512, fill);
    }

    uint32_t Get32(const uint8_t* p)
    {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
    }

    std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<uint8_t> bytes(static_cast<size_t>(std::filesystem::file_size(path)));
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return bytes;
    }

    /// One's complement of the byte sum with the checksum field counted as zero equals the field
    bool ChecksumOk(const uint8_t* data, size_t size, size_t field)
    {
        uint32_t sum = 0;
        for (size_t i = 0; i < size; i++)
            sum += (i >= field && i < field + 4) ? 0 : data[i];
        return ~sum == Get32(data + field);
    }
}  // namespace

TEST(VhdDynamic_Test, WriterStoresOnlyBlocksWithData)
{
    ScratchFolder folder("vhd-dynamic");
    SparseMemoryDisk disk(64 * kPerBlock);  // 128 MiB, 64 blocks
    ASSERT_TRUE(disk.WriteSector(5, Sector(0x11).data()));
    ASSERT_TRUE(disk.WriteSector(10 * kPerBlock + 7, Sector(0x22).data()));
    ASSERT_TRUE(disk.WriteSector(disk.SectorCount() - 1, Sector(0x33).data()));

    const auto path = folder.Path() / "disk.vhd";
    std::string error;
    ASSERT_TRUE(vhd::WriteDynamic(disk, FileHelper::FromFsPath(path), &error)) << error;
    EXPECT_EQ(std::filesystem::file_size(path), 512u + 1024 + 512 + 3 * (512 + vhd::kBlockBytes) + 512) << "3 blocks of 64";

    // The structures: both footers equal, checksums valid
    const std::vector<uint8_t> bytes = ReadFile(path);
    ASSERT_TRUE(std::equal(bytes.begin(), bytes.begin() + 512, bytes.end() - 512));
    EXPECT_EQ(vhd::FooterType(bytes.data()), vhd::kDynamic);
    EXPECT_TRUE(ChecksumOk(bytes.data(), 512, 64));
    EXPECT_EQ(std::memcmp(bytes.data() + 512, "cxsparse", 8), 0);
    EXPECT_TRUE(ChecksumOk(bytes.data() + 512, 1024, 36));
    EXPECT_EQ(Get32(bytes.data() + 512 + 28), 64u) << "BAT entries";

    EXPECT_EQ(HddImageFormats::Probe(FileHelper::FromFsPath(path)), "vhd");
    auto image = HddImageFormats::OpenBlock(FileHelper::FromFsPath(path), "vhd", RawImage::Access::ReadOnly, &error);
    ASSERT_NE(image, nullptr) << error;
    ASSERT_EQ(image->SectorCount(), disk.SectorCount());
    EXPECT_FALSE(image->IsWritable());
    std::vector<uint8_t> a(512), b(512);
    for (uint64_t lba : std::vector<uint64_t>{0, 5, 6, kPerBlock, 10 * kPerBlock + 7, 30 * kPerBlock, disk.SectorCount() - 1})
    {
        ASSERT_TRUE(disk.ReadSector(lba, a.data()));
        ASSERT_TRUE(image->ReadSector(lba, b.data()));
        EXPECT_EQ(a, b) << lba;
    }
    EXPECT_EQ(image->ZeroRun(kPerBlock), 9 * kPerBlock) << "blocks 1-9 are not allocated";
    EXPECT_EQ(image->ZeroRun(5), 0u);

    // Through BlockFormats: a .vhd with vhd: dynamic, and the fixed default
    BlockWriteOptions dynamic;
    dynamic.vhd = "dynamic";
    ASSERT_TRUE(BlockFormats::Write(disk, FileHelper::FromFsPath(folder.Path() / "via.vhd"), dynamic, {}).Ok());
    EXPECT_EQ(std::filesystem::file_size(folder.Path() / "via.vhd"), std::filesystem::file_size(path));
    ASSERT_TRUE(BlockFormats::Write(disk, FileHelper::FromFsPath(folder.Path() / "fixed.vhd"), {}, {}).Ok());
    EXPECT_EQ(std::filesystem::file_size(folder.Path() / "fixed.vhd"), disk.SectorCount() * 512 + 512);
    BlockWriteOptions wrong;
    wrong.vhd = "dynamic";
    EXPECT_FALSE(BlockFormats::Write(disk, FileHelper::FromFsPath(folder.Path() / "x.img"), wrong, {}).Ok()) << "vhd applies to .vhd only";
}

/// A hand-built image laid out as other tools do: a 4 KiB BAT, a block whose bitmap marks only some sectors present
TEST(VhdDynamic_Test, ReadsPartialBitmap)
{
    ScratchFolder folder("vhd-bitmap");
    const uint64_t sectors = 2 * kPerBlock;
    const std::array<uint8_t, 512> footer = vhd::Footer(sectors * 512, 520, 16, 63, vhd::kDynamic, 512, 7);
    std::vector<uint8_t> file(512 + 1024 + 4096, 0);
    std::memcpy(file.data(), footer.data(), 512);
    uint8_t* header = file.data() + 512;
    std::memcpy(header, "cxsparse", 8);
    std::fill(header + 8, header + 16, 0xFF);
    header[16 + 6] = 0x06;  // table offset 1536
    header[24 + 1] = 0x01;  // version 1.0
    header[28 + 3] = 2;     // entries
    header[32 + 1] = 0x20;  // 2 MiB blocks
    std::fill(file.begin() + 1536, file.end(), 0xFF);
    // Block 1 after the table (sector 11): a bitmap with sectors 0 and 9 present, both written as 0xAB; sector 1 holds
    // stale bytes the bitmap says are absent (reads zeros)
    const uint32_t at = static_cast<uint32_t>(file.size() / 512);
    file[1536 + 4] = 0;
    file[1536 + 5] = 0;
    file[1536 + 6] = static_cast<uint8_t>(at >> 8);
    file[1536 + 7] = static_cast<uint8_t>(at);
    std::vector<uint8_t> block(512 + vhd::kBlockBytes, 0);
    block[0] = 0x80;
    block[1] = 0x40;
    std::fill(block.begin() + 512, block.begin() + 1024, 0xAB);
    std::fill(block.begin() + 1024, block.begin() + 1536, 0xEE);
    std::fill(block.begin() + 512 + 9 * 512, block.begin() + 512 + 10 * 512, 0xAB);
    file.insert(file.end(), block.begin(), block.end());
    file.insert(file.end(), footer.begin(), footer.end());
    const auto path = folder.Path() / "hand.vhd";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    }

    std::string error;
    auto image = VhdDynamicImage::Open(FileHelper::FromFsPath(path), VhdDynamicImage::Access::ReadOnly, &error);
    ASSERT_NE(image, nullptr) << error;
    EXPECT_EQ(image->SectorCount(), sectors);
    ASSERT_TRUE(image->NativeGeometry());
    EXPECT_EQ(image->NativeGeometry()->cylinders, 520u);
    std::vector<uint8_t> read(512);
    ASSERT_TRUE(image->ReadSector(kPerBlock, read.data()));
    EXPECT_EQ(read, Sector(0xAB));
    ASSERT_TRUE(image->ReadSector(kPerBlock + 1, read.data()));
    EXPECT_EQ(read, Sector(0)) << "absent in the bitmap";
    ASSERT_TRUE(image->ReadSector(kPerBlock + 9, read.data()));
    EXPECT_EQ(read, Sector(0xAB));
    ASSERT_TRUE(image->ReadSector(3, read.data()));
    EXPECT_EQ(read, Sector(0)) << "block 0 is not allocated";
    EXPECT_EQ(image->ZeroRun(0), kPerBlock);
    EXPECT_FALSE(image->ReadSector(sectors, read.data()));

    // A fixed VHD is not one; a differencing one is refused by OpenBlock
    EXPECT_EQ(VhdDynamicImage::Open(FileHelper::FromFsPath(folder.Path() / "missing.vhd"), VhdDynamicImage::Access::ReadOnly), nullptr);
    std::vector<uint8_t> differencing = file;
    const std::array<uint8_t, 512> child = vhd::Footer(sectors * 512, 520, 16, 63, vhd::kDifferencing, 512, 7);
    std::copy(child.begin(), child.end(), differencing.end() - 512);
    {
        std::ofstream out(folder.Path() / "child.vhd", std::ios::binary);
        out.write(reinterpret_cast<const char*>(differencing.data()), static_cast<std::streamsize>(differencing.size()));
    }
    error.clear();
    EXPECT_EQ(HddImageFormats::OpenBlock(FileHelper::FromFsPath(folder.Path() / "child.vhd"), "vhd", RawImage::Access::ReadOnly, &error), nullptr);
    EXPECT_NE(error.find("differencing"), std::string::npos) << error;
}

TEST(VhdDynamic_Test, WritesInPlaceAllocateBlocks)
{
    ScratchFolder folder("vhd-inplace");
    SparseMemoryDisk disk(8 * kPerBlock);
    ASSERT_TRUE(disk.WriteSector(1, Sector(0x01).data()));
    const auto path = folder.Path() / "w.vhd";
    ASSERT_TRUE(vhd::WriteDynamic(disk, FileHelper::FromFsPath(path), nullptr));
    const uint64_t before = std::filesystem::file_size(path);

    std::string error;
    {
        auto image = HddImageFormats::OpenBlock(FileHelper::FromFsPath(path), "vhd", RawImage::Access::ReadWrite, &error);
        ASSERT_NE(image, nullptr) << error;
        ASSERT_TRUE(image->IsWritable());
        auto* dynamic = dynamic_cast<VhdDynamicImage*>(image.get());
        ASSERT_NE(dynamic, nullptr);
        EXPECT_EQ(dynamic->AllocatedBlocks(), 1u);
        ASSERT_TRUE(image->WriteSector(2, Sector(0x02).data()));                 // an allocated block
        ASSERT_TRUE(image->WriteSector(5 * kPerBlock + 3, Sector(0).data()));    // zeros into a free block: nothing
        EXPECT_EQ(dynamic->AllocatedBlocks(), 1u);
        ASSERT_TRUE(image->WriteSector(5 * kPerBlock + 3, Sector(0x53).data())); // allocates block 5
        EXPECT_EQ(dynamic->AllocatedBlocks(), 2u);
        EXPECT_EQ(image->ZeroRun(5 * kPerBlock), 0u);
    }
    EXPECT_EQ(std::filesystem::file_size(path), before + 512 + vhd::kBlockBytes);

    // Reopened: the data, zeros around it, the trailing footer valid
    const std::vector<uint8_t> bytes = ReadFile(path);
    EXPECT_EQ(vhd::FooterType(bytes.data() + bytes.size() - 512), vhd::kDynamic);
    EXPECT_TRUE(ChecksumOk(bytes.data() + bytes.size() - 512, 512, 64));
    auto image = VhdDynamicImage::Open(FileHelper::FromFsPath(path), VhdDynamicImage::Access::ReadOnly, &error);
    ASSERT_NE(image, nullptr) << error;
    std::vector<uint8_t> read(512);
    const std::vector<std::pair<uint64_t, uint8_t>> expected = {{1, 0x01}, {2, 0x02}, {3, 0}, {5 * kPerBlock + 2, 0},
                                                                {5 * kPerBlock + 3, 0x53}, {5 * kPerBlock + 4, 0}, {7 * kPerBlock, 0}};
    for (const auto& [lba, fill] : expected)
    {
        ASSERT_TRUE(image->ReadSector(lba, read.data()));
        EXPECT_EQ(read, Sector(fill)) << lba;
    }
    EXPECT_FALSE(image->WriteSector(1, Sector(9).data())) << "read-only";
}
