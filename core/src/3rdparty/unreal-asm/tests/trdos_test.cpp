// TR-DOS containers: hobeta read / write (the real TASM files are rewritten byte for byte) and TR-DOS images.

#include <gtest/gtest.h>

#include "testdata.h"
#include "unrealasm/containers.h"

using namespace unrealasm;
using namespace unrealasm::containers;
using unrealasm::testing::ReadTestData;

TEST(Trdos_Test, HobetaReadAndWriteBack)
{
    const auto bytes = ReadTestData("tasm3/000LOAD.$A");
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
    auto bytes = ReadTestData("tasm3/000LOAD.$A");
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
