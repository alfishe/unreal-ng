// The TR-DOS file system on a disk image: write, read, replace, delete; refusals say why

#include <gtest/gtest.h>

#include "testdata.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/loaders/loader_trd.h"
#include "unrealdisk/trdosfs.h"

using namespace unrealdisk;
using unrealdisk::testing::ScratchFolder;
using unrealdisk::testing::TestDataPath;

namespace
{
std::unique_ptr<DiskImage> Blank()
{
    auto disk = std::make_unique<DiskImage>(uint8_t{80}, uint8_t{2});
    LoaderTRD trd({}, "");
    EXPECT_TRUE(trd.format(disk.get()));
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

TEST(TrdosFs_Test, FilesGoOntoABlankDiskAndBack)
{
    auto disk = Blank();
    TrdosFileSystem fs(*disk);
    ASSERT_TRUE(fs.IsTrdos());
    std::string error;
    ASSERT_TRUE(fs.Write("MAIN", 'H', Bytes(1242, 1), {}, error)) << error;
    TrdosFileSystem::WriteOptions code;
    code.start = 0x6000;
    ASSERT_TRUE(fs.Write("DATA", 'C', Bytes(5, 9), code, error)) << error;

    std::vector<TrdosFile> files;
    ASSERT_TRUE(fs.List(files, error)) << error;
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].TrimmedName(), "MAIN");
    EXPECT_EQ(files[0].sectors, 5);
    EXPECT_EQ(files[0].firstTrack, 1) << "the first file starts at track 1";
    EXPECT_EQ(files[1].start, 0x6000);
    std::vector<uint8_t> data, tail;
    ASSERT_TRUE(fs.Read(files[0], data, &tail, error)) << error;
    EXPECT_EQ(data, Bytes(1242, 1));
    EXPECT_EQ(tail.size(), 5u * 256 - 1242);

    // The same name and type again: the old entry deleted, the new one after the last file
    ASSERT_TRUE(fs.Write("MAIN", 'H', Bytes(300, 3), {}, error)) << error;
    ASSERT_TRUE(fs.List(files, error));
    ASSERT_EQ(files.size(), 2u) << "the deleted entry is not listed";
    TrdosFile main;
    ASSERT_TRUE(fs.Find("MAIN", 'H', main, error)) << error;
    ASSERT_TRUE(fs.Read(main, data, nullptr, error));
    EXPECT_EQ(data, Bytes(300, 3));

    ASSERT_TRUE(fs.Delete("DATA", 'C', error)) << error;
    EXPECT_FALSE(fs.Find("DATA", 'C', main, error));
    EXPECT_NE(error.find("no file DATA.C"), std::string::npos) << error;
}

TEST(TrdosFs_Test, AWrittenDiskSavesAndLoadsInEveryTrdosFormat)
{
    auto disk = Blank();
    std::string error;
    ASSERT_TRUE(TrdosFileSystem(*disk).Write("ORACLE1", 'H', Bytes(1242, 5), {}, error)) << error;
    const auto folder = ScratchFolder("trdosfs-save");
    for (const std::string ext : {"trd", "scl", "fdi", "udi"})
    {
        const std::string path = (folder / ("disk." + ext)).string();
        ASSERT_TRUE(FloppyFormats::Save({}, *disk, path, false).saved) << ext;
        std::unique_ptr<DiskImage> back;
        std::string format;
        ASSERT_TRUE(FloppyFormats::Load({}, path, back, format).Ok()) << ext;
        TrdosFile file;
        ASSERT_TRUE(TrdosFileSystem(*back).Find("ORACLE1", 'H', file, error)) << ext << ": " << error;
        std::vector<uint8_t> data;
        ASSERT_TRUE(TrdosFileSystem(*back).Read(file, data, nullptr, error));
        EXPECT_EQ(data, Bytes(1242, 5)) << ext;
    }
}

TEST(TrdosFs_Test, RefusalsSayWhy)
{
    auto disk = Blank();
    TrdosFileSystem fs(*disk);
    std::string error;
    EXPECT_FALSE(fs.Write("NAME", 0, Bytes(10, 0), {}, error));
    EXPECT_NE(error.find("type letter"), std::string::npos) << error;
    EXPECT_FALSE(fs.Write("TOOLONGNAME", 'C', Bytes(10, 0), {}, error));
    for (int i = 0; i < 128; ++i)
        ASSERT_TRUE(fs.Write("F" + std::to_string(i), 'C', Bytes(1, 0), {}, error)) << i << ": " << error;
    EXPECT_FALSE(fs.Write("ONEMORE", 'C', Bytes(1, 0), {}, error));
    EXPECT_NE(error.find("catalog is full"), std::string::npos) << error;

    DiskImage plain(40, 1);   // no TR-DOS on it
    EXPECT_FALSE(TrdosFileSystem(plain).IsTrdos());
    std::vector<TrdosFile> files;
    EXPECT_FALSE(TrdosFileSystem(plain).List(files, error));
}
