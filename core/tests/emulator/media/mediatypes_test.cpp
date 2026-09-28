// Media vocabulary: stable error codes and access-mode names for every surface

#include <gtest/gtest.h>

#include "emulator/media/mediatypes.h"

TEST(MediaTypes_Test, ErrorCodesAreStable)
{
    EXPECT_STREQ(MediaErrorCode(MediaError::UnknownSlot), "unknown-slot");
    EXPECT_STREQ(MediaErrorCode(MediaError::InUse), "in-use");
    EXPECT_STREQ(MediaErrorCode(MediaError::Recording), "recording");
    EXPECT_STREQ(MediaErrorCode(MediaError::Dirty), "dirty");
}

TEST(MediaTypes_Test, AccessModesParseWithLegacySynonyms)
{
    AccessMode mode = AccessMode::Session;
    ASSERT_TRUE(ParseAccessMode("ReadOnly", mode));
    EXPECT_EQ(mode, AccessMode::ReadOnly);
    ASSERT_TRUE(ParseAccessMode("persist", mode)) << "[ZC] SDWrite=persist";
    EXPECT_EQ(mode, AccessMode::WriteThrough);
    ASSERT_TRUE(ParseAccessMode("off", mode)) << "[ZC] SDWrite=off";
    EXPECT_EQ(mode, AccessMode::ReadOnly);
    EXPECT_FALSE(ParseAccessMode("sometimes", mode));
    EXPECT_STREQ(AccessModeName(AccessMode::WriteThrough), "writethrough");
}
