// TR-DOS containers: hobeta read / write (the real TASM files are rewritten byte for byte) and TR-DOS images.

#include <gtest/gtest.h>

#include "testdata.h"
#include "unrealasm/containers.h"

using namespace unrealasm;
using namespace unrealasm::containers;
using unrealasm::testing::ReadTestData;

TEST(Trdos_Test, HobetaReadAndWriteBack)
{
    const auto bytes = ReadTestData("tasm/000LOAD.$A");
    TrdosFile file;
    std::string error;
    ASSERT_TRUE(ReadHobeta(bytes, file, error)) << error;
    EXPECT_EQ(file.TrimmedName(), "000load") << "the host file name is upper case, the catalog name is not";
    EXPECT_EQ(file.type, 'A');
    EXPECT_EQ(file.start, 40872) << "TASM 3 saves sources with this start";
    EXPECT_EQ(file.data.size(), file.length);
    EXPECT_EQ(WriteHobeta(file), bytes);
    const CatalogHints hints = file.Hints();
    EXPECT_EQ(hints.type, 'A');
    EXPECT_EQ(hints.extension, "$A");
}

TEST(Trdos_Test, HobetaChecksumMismatchIsRejected)
{
    auto bytes = ReadTestData("tasm/000LOAD.$A");
    bytes[0] ^= 1;
    TrdosFile file;
    std::string error;
    EXPECT_FALSE(ReadHobeta(bytes, file, error));
    EXPECT_NE(error.find("checksum"), std::string::npos);
}

TEST(Trdos_Test, ImageCatalogIsRead)
{
    // A 16-track image: catalog in track 0 sectors 0-7, disk info in sector 8, one file at track 1
    std::vector<uint8_t> image(16 * 16 * 256, 0);
    image[8 * 256 + 0xE7] = 0x10;
    const uint8_t deleted[16] = {1, 'O', 'L', 'D', ' ', ' ', ' ', ' ', 'C', 0, 0, 0, 0, 1, 0, 1};
    const uint8_t entry[16] = {'S', 'O', 'U', 'R', 'C', 'E', ' ', ' ', 'A', 0xA8, 0x9F, 3, 1, 2, 0, 1};
    std::copy(std::begin(deleted), std::end(deleted), image.begin());
    std::copy(std::begin(entry), std::end(entry), image.begin() + 16);
    for (size_t i = 0; i < 512; ++i)
        image[16 * 256 + i] = static_cast<uint8_t>(i);
    std::vector<TrdosFile> files;
    std::string error;
    ASSERT_TRUE(ReadTrd(image, files, error)) << error;
    ASSERT_EQ(files.size(), 1u) << "the deleted entry is skipped";
    EXPECT_EQ(files[0].TrimmedName(), "SOURCE");
    EXPECT_EQ(files[0].start, 40872);
    EXPECT_EQ(files[0].length, 259);
    ASSERT_EQ(files[0].data.size(), 259u);
    EXPECT_EQ(files[0].data[258], 2);
    EXPECT_EQ(files[0].tail.size(), 512u - 259u);

    image[8 * 256 + 0xE7] = 0;
    EXPECT_FALSE(ReadTrd(image, files, error));
}

TEST(Trdos_Test, FilesGoIntoANewImageAndBack)
{
    std::vector<uint8_t> image = NewTrd("SOURCES");
    TrdosFile a;
    a.name = "MAIN    ";
    a.type = 'H';
    a.length = 300;
    a.data.assign(300, 0x41);
    TrdosFile b = a;
    b.name = "DATA    ";
    b.type = 'C';
    b.start = 0x6000;
    b.length = 5;
    b.data = {1, 2, 3, 4, 5};
    std::string error;
    ASSERT_TRUE(PutTrdFile(image, a, error)) << error;
    ASSERT_TRUE(PutTrdFile(image, b, error)) << error;
    std::vector<TrdosFile> files;
    ASSERT_TRUE(ReadTrd(image, files, error)) << error;
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].TrimmedName(), "MAIN");
    EXPECT_EQ(files[0].data, a.data);
    EXPECT_EQ(files[1].start, 0x6000);
    EXPECT_EQ(files[1].data, b.data);
    EXPECT_EQ(image[8 * 256 + 0xE4], 2) << "the file count";
    EXPECT_EQ(image[8 * 256 + 0xE5] | (image[8 * 256 + 0xE6] << 8), 159 * 16 - 3) << "2 + 1 sectors used";

    // The same name and size: in its place. Another size: the old entry deleted, the new one after the last file
    a.data.assign(300, 0x42);
    ASSERT_TRUE(PutTrdFile(image, a, error)) << error;
    ASSERT_TRUE(ReadTrd(image, files, error));
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].data, a.data);
    a.length = 700;
    a.data.assign(700, 0x43);
    ASSERT_TRUE(PutTrdFile(image, a, error)) << error;
    ASSERT_TRUE(ReadTrd(image, files, error));
    ASSERT_EQ(files.size(), 2u) << "the deleted entry is not listed";
    EXPECT_EQ(files[1].TrimmedName(), "MAIN");
    EXPECT_EQ(files[1].data, a.data);
    EXPECT_EQ(image[8 * 256 + 0xF4], 1) << "one deleted file";
}

TEST(Trdos_Test, SclArchiveWritesAndReads)
{
    TrdosFile a;
    a.name = "ORACLE1 ";
    a.type = 'H';
    a.length = 3;
    a.data = {7, 8, 9};
    std::vector<TrdosFile> files;
    PutFile(files, a);
    a.data = {9, 8, 7};
    PutFile(files, a);   // the same name and type: replaced
    ASSERT_EQ(files.size(), 1u);
    const std::vector<uint8_t> scl = WriteScl(files);
    EXPECT_EQ(scl.size(), 9u + 14u + 256u + 4u);
    std::vector<TrdosFile> back;
    std::string error;
    ASSERT_TRUE(ReadScl(scl, back, error)) << error;
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back[0].TrimmedName(), "ORACLE1");
    EXPECT_EQ(back[0].data, a.data);
    EXPECT_EQ(back[0].tail.size(), 253u);
    EXPECT_FALSE(ReadScl(std::vector<uint8_t>(scl.begin() + 1, scl.end()), back, error));
}
