// G+DOS: the emulator's system disk (data/boot/plusd/gdos-2a-system.mgt, written by G+DOS 2a's CONFIG) listed and
// read directly and through DiskFs; a SAM DOS entry and a damaged first track are not G+DOS

#include "unrealdisk/gdosfs.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>

#include "testdata.h"
#include "unrealdisk/diskfs.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/floppyformats.h"

using namespace unrealdisk;
using unrealdisk::testing::TestDataPath;

namespace
{
const char* kSystemDisk = "../data/boot/plusd/gdos-2a-system.mgt";

std::vector<uint8_t> Bytes(const std::string& relative)
{
    std::ifstream in(TestDataPath(relative), std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::unique_ptr<DiskImage> Parse(const std::vector<uint8_t>& bytes)
{
    std::unique_ptr<DiskImage> disk;
    std::string format;
    const FormatResult result = FloppyFormats::LoadData({}, bytes.data(), bytes.size(), "disk.mgt", disk, format);
    EXPECT_TRUE(result.Ok()) << result.message;
    return disk;
}

/// Byte `offset` of directory entry `slot` in an MGT-ordered image (cylinder-interleaved sides)
size_t EntryOffset(uint32_t slot, size_t offset)
{
    const uint32_t cylinder = slot / 20, sector = slot % 20 / 2;
    return (cylinder * 2 * 10 + sector) * 512 + (slot % 2) * 256 + offset;
}
}  // namespace

TEST(GdosFs_Test, SystemDiskLists)
{
    std::unique_ptr<DiskImage> disk = Parse(Bytes(kSystemDisk));
    ASSERT_NE(disk, nullptr);
    GdosFileSystem fs(*disk);
    std::string why;
    ASSERT_TRUE(fs.IsGdos(&why)) << why;
    std::vector<GdosFile> files;
    std::string error;
    ASSERT_TRUE(fs.List(files, error)) << error;
    ASSERT_EQ(files.size(), 5u);
    EXPECT_EQ(files[0].name, "+SYS 2a");
    EXPECT_EQ(files[0].type, 4);
    EXPECT_EQ(files[0].sectors, 14);
    EXPECT_EQ(files[0].length, 6656u);
    EXPECT_EQ(files[0].start, 0x2000);
    EXPECT_EQ(files[1].name, "CONFIG");
    EXPECT_EQ(files[1].type, 1);
    EXPECT_EQ(files[1].length, 0x3E9Fu);
    EXPECT_EQ(files[1].autostart, 9100);
    EXPECT_EQ(files[4].name, "CONFIG3_C");
    EXPECT_EQ(fs.FreeSectors(), 1600u - 40u - (14 + 32 + 4 + 14 + 1));

    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read(files[0], data, error)) << error;
    ASSERT_EQ(data.size(), 6656u);
    // The body of +SYS starts with the code after the tape header (03 001A 2000 FFFF 0000)
    EXPECT_EQ(data[0], 0x07);
    EXPECT_EQ(data[1], 0xD0);
}

TEST(GdosFs_Test, DiskFsVolume)
{
    std::unique_ptr<DiskImage> disk = Parse(Bytes(kSystemDisk));
    ASSERT_NE(disk, nullptr);
    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    EXPECT_EQ(fs.Volumes()[0].id, "gdos");
    EXPECT_EQ(fs.Volumes()[0].system, "G+DOS");
    EXPECT_FALSE(fs.Volumes()[0].writable);
    std::vector<DiskFsEntry> entries;
    std::string error;
    ASSERT_TRUE(fs.List("", "", false, entries, error)) << error;
    ASSERT_EQ(entries.size(), 5u);
    EXPECT_EQ(entries[1].name, "CONFIG");
    EXPECT_EQ(entries[1].Field("type"), 1);
    EXPECT_EQ(entries[1].start, 9100u) << "BASIC: the autostart line";
    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read("", "config3_c", data, error)) << error;
    EXPECT_EQ(data.size(), 0x003Cu);
    std::vector<uint8_t> none;
    EXPECT_FALSE(fs.Read("", "NOSUCH", none, error));
}

TEST(GdosFs_Test, NotGdos)
{
    std::vector<uint8_t> bytes = Bytes(kSystemDisk);
    ASSERT_EQ(bytes.size(), 819200u);
    // A SAM DOS type in the first entry
    std::vector<uint8_t> sam = bytes;
    sam[EntryOffset(0, 0)] = 16;
    std::unique_ptr<DiskImage> disk = Parse(sam);
    ASSERT_NE(disk, nullptr);
    std::string why;
    EXPECT_FALSE(GdosFileSystem(*disk).IsGdos(&why));
    EXPECT_NE(why.find("type 16"), std::string::npos) << why;
    // A first track inside the directory
    std::vector<uint8_t> inside = bytes;
    inside[EntryOffset(1, 13)] = 2;
    disk = Parse(inside);
    ASSERT_NE(disk, nullptr);
    EXPECT_FALSE(GdosFileSystem(*disk).IsGdos(&why));
    // A broken link: the read stops with the reason and what it has
    std::vector<uint8_t> broken = bytes;
    const size_t firstSector = (4 * 2 * 10 + 0) * 512;  // +SYS: track 4 sector 1
    broken[firstSector + 510] = 0;
    broken[firstSector + 511] = 0;
    disk = Parse(broken);
    ASSERT_NE(disk, nullptr);
    GdosFileSystem fs(*disk);
    std::vector<GdosFile> files;
    std::string error;
    ASSERT_TRUE(fs.List(files, error));
    std::vector<uint8_t> data;
    EXPECT_FALSE(fs.Read(files[0], data, error));
    EXPECT_NE(error.find("chain ends after 1"), std::string::npos) << error;
    EXPECT_EQ(data.size(), 501u);
}
