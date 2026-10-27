// The tools' disk files (tools/common/diskfiles.h) through DiskFs: a TR-DOS disk written and read back with the rest of
// the last sector, and a +3DOS disk's files with their extensions and padded names

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "../tools/common/diskfiles.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/loaders/loader_trd.h"

using namespace unrealasm;
using namespace unrealasm::containers;
using namespace unrealasm::tools;

TEST(DiskFiles_Test, TrdosPutAndReadBack)
{
    unrealdisk::DiskImage disk(uint8_t{80}, uint8_t{2});
    unrealdisk::LoaderTRD trd({}, "");
    ASSERT_TRUE(trd.format(&disk));

    TrdosFile source;
    source.name = "PROG    ";
    source.type = 'C';
    source.start = 0x8000;
    source.data = {1, 2, 3};
    source.length = 3;
    source.tail.assign(253, 0xEE);
    TrdosFile odd = source;
    odd.name = "ODD";
    odd.type = 1;  // no letter: \x01 in DiskFs names
    std::string error;
    ASSERT_TRUE(PutDiskFile(disk, source, error)) << error;
    ASSERT_TRUE(PutDiskFile(disk, odd, error)) << error;

    std::vector<TrdosFile> files;
    ASSERT_TRUE(ReadDiskFiles(disk, files, error)) << error;
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].name, "PROG    ");
    EXPECT_EQ(files[0].type, 'C');
    EXPECT_EQ(files[0].start, 0x8000);
    EXPECT_EQ(files[0].length, 3);
    EXPECT_EQ(files[0].sectors, 1);
    EXPECT_EQ(files[0].data, source.data);
    EXPECT_EQ(files[0].tail.size(), 253u);
    EXPECT_TRUE(files[0].extension.empty());
    EXPECT_EQ(files[1].name, "ODD     ");
    EXPECT_EQ(files[1].type, 1);
}

TEST(DiskFiles_Test, Plus3FilesKeepTheirExtension)
{
    const std::filesystem::path blank = std::filesystem::path(UNREAL_ASM_REPO_TESTDATA_DIR) / "loaders/dsk/plus3-blank.dsk";
    if (!std::filesystem::exists(blank))
        GTEST_SKIP() << "outside the unreal-ng repository";
    std::ifstream in(blank, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::unique_ptr<unrealdisk::DiskImage> disk;
    std::string format;
    ASSERT_TRUE(unrealdisk::FloppyFormats::LoadData({}, bytes.data(), bytes.size(), "blank.dsk", disk, format).Ok());

    TrdosFile code;
    code.name = "GAME";
    code.extension = "bin";
    code.start = 0x6000;
    code.data.assign(300, 0x5A);
    std::string error;
    ASSERT_TRUE(PutDiskFile(*disk, code, error)) << error;

    std::vector<TrdosFile> files;
    ASSERT_TRUE(ReadDiskFiles(*disk, files, error)) << error;
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].name, "GAME    ");
    EXPECT_EQ(files[0].extension, "bin");
    EXPECT_EQ(files[0].type, 0);
    EXPECT_EQ(files[0].start, 0x6000);
    EXPECT_EQ(files[0].data, code.data);
    EXPECT_EQ(files[0].length, 300);
    EXPECT_EQ(files[0].sectors, 2);
}
