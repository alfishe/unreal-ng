// Hobeta files: read and written back (the real TASM files are rewritten byte for byte). Disk images: unreal-disk.

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
