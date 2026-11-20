// G+DOS: the emulator's system disk (data/boot/plusd/gdos-2a-system.mgt, written by G+DOS 2a's CONFIG) listed and
// read directly and through DiskFs; a type above 11 and a damaged first track are not G+DOS; files written, replaced,
// deleted, renamed and protected as G+DOS does it

#include "unrealdisk/gdosfs.h"

#include <gtest/gtest.h>

#include <cstdio>
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
    EXPECT_TRUE(fs.Volumes()[0].writable);
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
    // A type G+DOS does not have (16) in the first entry
    std::vector<uint8_t> other = bytes;
    other[EntryOffset(0, 0)] = 16;
    std::unique_ptr<DiskImage> disk = Parse(other);
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

/// A file written after CONFIG3_C (map bit 64): the next free sectors in map order, the entry as G+DOS SAVE writes
/// it (type 4, the tape header at 211 and before the data), read back, and the image saved and loaded again
TEST(GdosFs_Test, WriteCodeAfterTheLastFile)
{
    std::unique_ptr<DiskImage> disk = Parse(Bytes(kSystemDisk));
    ASSERT_NE(disk, nullptr);
    GdosFileSystem fs(*disk);
    const uint32_t freeBefore = fs.FreeSectors();
    std::vector<uint8_t> body(1200);
    for (size_t i = 0; i < body.size(); i++)
        body[i] = static_cast<uint8_t>(i * 7);
    std::string error;
    ASSERT_TRUE(fs.Write("Tiles", 3, body, 0x8000, 0xFFFF, 0, error)) << error;
    EXPECT_EQ(fs.FreeSectors(), freeBefore - 3) << "9 + 1200 bytes: 3 sectors of 510";

    std::vector<GdosFile> files;
    ASSERT_TRUE(fs.List(files, error)) << error;
    ASSERT_EQ(files.size(), 6u);
    const GdosFile& f = files[5];
    EXPECT_EQ(f.slot, 5u);
    EXPECT_EQ(f.name, "Tiles");
    EXPECT_EQ(f.type, 4);
    EXPECT_EQ(f.sectors, 3);
    EXPECT_EQ(f.length, 1200u);
    EXPECT_EQ(f.start, 0x8000);
    EXPECT_EQ(f.track, 10) << "map bit 65: track 10 sector 6";
    EXPECT_EQ(f.sector, 6);
    std::vector<uint8_t> back;
    ASSERT_TRUE(fs.Read(f, back, error)) << error;
    EXPECT_EQ(back, body);

    const std::string path = ::testing::TempDir() + "gdos-write.mgt";
    const FloppySaveResult saved = FloppyFormats::Save({}, *disk, path, false);
    ASSERT_TRUE(saved.saved) << saved.reason;
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    EXPECT_EQ(bytes[EntryOffset(5, 0)], 4);
    EXPECT_EQ(bytes[EntryOffset(5, 15 + 8)], 0x0E) << "map bits 65-67";
    EXPECT_EQ(bytes[EntryOffset(5, 211)], 3) << "the tape header: CODE";
    const size_t first = (10 * 2 * 10 + 5) * 512;
    EXPECT_EQ(bytes[first], 3) << "the tape header leads the data";
    EXPECT_EQ(bytes[first + 510], 10);
    EXPECT_EQ(bytes[first + 511], 7);
    std::unique_ptr<DiskImage> again = Parse(bytes);
    ASSERT_NE(again, nullptr);
    GdosFileSystem reread(*again);
    std::vector<GdosFile> listed;
    ASSERT_TRUE(reread.List(listed, error)) << error;
    ASSERT_EQ(listed.size(), 6u);
    ASSERT_TRUE(reread.Read(listed[5], back, error)) << error;
    EXPECT_EQ(back, body);
    std::remove(path.c_str());
}

/// Through DiskFs: a program (type 1, PROG, the autostart line, param its length), replaced by a longer one in the
/// same entry, renamed, hidden and protected (then not deletable), deleted; bad names, a full disk refused
TEST(GdosFs_Test, DiskFsPutReplaceRenameProtectDelete)
{
    std::unique_ptr<DiskImage> disk = Parse(Bytes(kSystemDisk));
    ASSERT_NE(disk, nullptr);
    DiskFs fs(*disk);
    ASSERT_EQ(fs.Volumes().size(), 1u);
    EXPECT_TRUE(fs.Volumes()[0].writable);
    EXPECT_EQ(fs.Volumes()[0].attributes, "HP");
    const int64_t freeBefore = fs.Volumes()[0].freeBytes;
    std::string error;
    DiskFsWriteOptions program;
    program.basicType = 0;
    program.start = 10;
    ASSERT_TRUE(fs.Write("", "run", std::vector<uint8_t>(600, 0x0D), program, error)) << error;
    ASSERT_TRUE(fs.Write("", "run", std::vector<uint8_t>(1500, 0x0D), program, error)) << error;

    std::vector<DiskFsEntry> entries;
    ASSERT_TRUE(fs.List("", "", false, entries, error)) << error;
    ASSERT_EQ(entries.size(), 6u) << "the older run replaced, not added";
    EXPECT_EQ(entries[5].path, "run");
    EXPECT_EQ(entries[5].size, 1500u);
    EXPECT_EQ(entries[5].start, 10u) << "a program's start is its autostart line";
    EXPECT_EQ(entries[5].Field("type"), 1);
    EXPECT_EQ(entries[5].Field("param"), 1500);
    EXPECT_EQ(entries[5].Field("sectors"), 3) << "the first one's 2 sectors freed";
    GdosFileSystem gdos(*disk);
    EXPECT_EQ(static_cast<int64_t>(gdos.FreeSectors()) * GdosFileSystem::kDataPerSector, freeBefore - 3 * 510);
    std::vector<GdosFile> files;
    ASSERT_TRUE(gdos.List(files, error)) << error;
    EXPECT_EQ(files[5].start, 23755) << "PROG of a 48K";

    EXPECT_FALSE(fs.Rename("", "run", "CONFIG", error)) << "CONFIG is there";
    ASSERT_TRUE(fs.Rename("", "run", "Start", error)) << error;
    ASSERT_TRUE(fs.SetAttributes("", "Start", "HP", error)) << error;
    entries.clear();
    ASSERT_TRUE(fs.List("", "", false, entries, error)) << error;
    EXPECT_EQ(entries[5].path, "Start");
    EXPECT_EQ(entries[5].attributes, "HP");
    EXPECT_FALSE(fs.Delete("", "Start", error));
    EXPECT_NE(error.find("protected"), std::string::npos) << error;
    EXPECT_FALSE(fs.Write("", "Start", {1, 2, 3}, {}, error)) << "a protected file is not replaced";
    ASSERT_TRUE(fs.SetAttributes("", "Start", "-H-P", error)) << error;
    ASSERT_TRUE(fs.Delete("", "Start", error)) << error;
    entries.clear();
    ASSERT_TRUE(fs.List("", "", false, entries, error)) << error;
    EXPECT_EQ(entries.size(), 5u);
    EXPECT_EQ(static_cast<int64_t>(gdos.FreeSectors()) * GdosFileSystem::kDataPerSector, freeBefore);

    EXPECT_FALSE(fs.Write("", "elevenchars", {1}, {}, error));
    EXPECT_FALSE(fs.Write("", " lead", {1}, {}, error));
    EXPECT_FALSE(fs.Write("", "big", std::vector<uint8_t>(70000), {}, error));
    // Fill the disk with 64 KB files: the one that does not fit is refused and changes nothing
    int written = 0;
    while (fs.Write("", "f" + std::to_string(written), std::vector<uint8_t>(65000), {}, error))
        written++;
    EXPECT_NE(error.find("free"), std::string::npos) << error;
    EXPECT_EQ(written, 11) << "1495 sectors free, 128 a file";
}

/// A blank MGT (all zeros) is an empty G+DOS disk: the first file goes to track 4 sector 1, the first entry
TEST(GdosFs_Test, WriteOnABlankDisk)
{
    std::unique_ptr<DiskImage> disk = Parse(std::vector<uint8_t>(819200, 0));
    ASSERT_NE(disk, nullptr);
    GdosFileSystem fs(*disk);
    ASSERT_TRUE(fs.IsGdos());
    std::string error;
    ASSERT_TRUE(fs.Write("A", 3, {1, 2, 3}, 0x6000, 0xFFFF, 0, error)) << error;
    std::vector<GdosFile> files;
    ASSERT_TRUE(fs.List(files, error)) << error;
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].slot, 0u);
    EXPECT_EQ(files[0].track, 4);
    EXPECT_EQ(files[0].sector, 1);
    EXPECT_EQ(fs.FreeSectors(), 1560u - 1);
}
