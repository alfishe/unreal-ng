// The +3DOS file system on a disk image: write, read, replace, delete, save and load again; a TR-DOS disk is not one

#include <gtest/gtest.h>

#include "testdata.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/loaders/loader_trd.h"
#include "unrealdisk/plus3fs.h"

using namespace unrealdisk;
using unrealdisk::testing::ScratchFolder;
using unrealdisk::testing::TestDataPath;

namespace
{
std::unique_ptr<DiskImage> BlankPlus3()
{
    std::unique_ptr<DiskImage> disk;
    std::string format;
    EXPECT_TRUE(FloppyFormats::Load({}, TestDataPath("loaders/dsk/plus3-blank.dsk"), disk, format).Ok());
    return disk;
}

std::vector<uint8_t> Bytes(size_t n, uint8_t seed)
{
    std::vector<uint8_t> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = static_cast<uint8_t>(seed + i * 7);
    return out;
}
}  // namespace

TEST(Plus3Fs_Test, FilesGoOntoABlankDiskAndBack)
{
    auto disk = BlankPlus3();
    ASSERT_NE(disk, nullptr);
    Plus3FileSystem fs(*disk);
    ASSERT_TRUE(fs.IsPlus3());
    std::vector<Plus3File> files;
    std::string error;
    ASSERT_TRUE(fs.List(files, error)) << error;
    EXPECT_TRUE(files.empty());

    // a source with a +3DOS header, and a file of more than one directory entry (16K each) without one
    const std::vector<uint8_t> source = Bytes(3000, 1);
    Plus3FileSystem::WriteOptions code;
    code.param1 = 0x8000;
    ASSERT_TRUE(fs.Write("source.asm", source, code, error)) << error;
    Plus3FileSystem::WriteOptions raw;
    raw.header = false;
    const std::vector<uint8_t> big = Bytes(20000, 9);
    ASSERT_TRUE(fs.Write("BIG.BIN", big, raw, error)) << error;

    ASSERT_TRUE(fs.List(files, error)) << error;
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].FullName(), "SOURCE.ASM");
    EXPECT_TRUE(files[0].header);
    EXPECT_EQ(files[0].length, 3000u);
    EXPECT_EQ(files[0].basicType, 3);
    EXPECT_EQ(files[0].param1, 0x8000);
    EXPECT_EQ(files[1].FullName(), "BIG.BIN");
    EXPECT_FALSE(files[1].header);
    EXPECT_EQ(files[1].size, 20096u);   // whole records

    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read(files[0], data, error)) << error;
    EXPECT_EQ(data, source);
    ASSERT_TRUE(fs.Read(files[1], data, error)) << error;
    ASSERT_GE(data.size(), big.size());
    EXPECT_TRUE(std::equal(big.begin(), big.end(), data.begin()));
}

TEST(Plus3Fs_Test, AFileWrittenAgainReplacesTheOldOneAndDeleteFreesIt)
{
    auto disk = BlankPlus3();
    ASSERT_NE(disk, nullptr);
    Plus3FileSystem fs(*disk);
    std::string error;
    ASSERT_TRUE(fs.Write("MAIN.ASM", Bytes(5000, 1), {}, error)) << error;
    ASSERT_TRUE(fs.Write("MAIN.ASM", Bytes(700, 2), {}, error)) << error;
    std::vector<Plus3File> files;
    ASSERT_TRUE(fs.List(files, error)) << error;
    ASSERT_EQ(files.size(), 1u);
    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read(files[0], data, error)) << error;
    EXPECT_EQ(data, Bytes(700, 2));

    ASSERT_TRUE(fs.Delete("main.asm", error)) << error;
    ASSERT_TRUE(fs.List(files, error)) << error;
    EXPECT_TRUE(files.empty());
    EXPECT_FALSE(fs.Delete("MAIN.ASM", error));
    EXPECT_NE(error.find("no file"), std::string::npos) << error;

    // the whole disk's free space (173 blocks of 1K), then one byte too many
    ASSERT_TRUE(fs.Write("FULL.BIN", Bytes(173 * 1024 - 128, 3), {}, error)) << error;
    EXPECT_FALSE(fs.Write("MORE.BIN", Bytes(10, 4), {}, error));
    EXPECT_NE(error.find("free blocks"), std::string::npos) << error;
}

TEST(Plus3Fs_Test, TheDiskSavedAndLoadedKeepsItsFiles)
{
    auto disk = BlankPlus3();
    ASSERT_NE(disk, nullptr);
    std::string error;
    ASSERT_TRUE(Plus3FileSystem(*disk).Write("KEEP.TXT", Bytes(1500, 5), {}, error)) << error;
    const std::string path = (ScratchFolder("plus3fs") / "saved.dsk").string();
    const FloppySaveResult saved = FloppyFormats::Save({}, *disk, path, false);
    ASSERT_TRUE(saved.saved && !saved.retargeted) << saved.reason;

    std::unique_ptr<DiskImage> again;
    std::string format;
    ASSERT_TRUE(FloppyFormats::Load({}, path, again, format).Ok());
    Plus3FileSystem fs(*again);
    Plus3File file;
    ASSERT_TRUE(fs.Find("keep.txt", file, error)) << error;
    std::vector<uint8_t> data;
    ASSERT_TRUE(fs.Read(file, data, error)) << error;
    EXPECT_EQ(data, Bytes(1500, 5));
}

TEST(Plus3Fs_Test, ATrdosDiskIsNotPlus3)
{
    auto disk = std::make_unique<DiskImage>(80, 2);
    LoaderTRD trd({}, "");
    ASSERT_TRUE(trd.format(disk.get()));
    std::vector<Plus3File> files;
    std::string error;
    EXPECT_FALSE(Plus3FileSystem(*disk).List(files, error));
    EXPECT_NE(error.find("+3DOS"), std::string::npos) << error;
}
