#include "pch.h"

#include "common/uuid.h"

/// @brief Locks UUID::isNil() semantics. An earlier revision returned the inverted
/// result (true for any non-zero byte), which EmulatorContext and the DeZog adapter
/// had to work around with default-constructed-UUID comparisons, and which
/// EmulatorManager_Test.CreateEmulator unknowingly depended on. These tests pin the
/// documented contract: isNil() == true if and only if all 16 bytes are zero.
TEST(UUID_Test, DefaultConstructedIsNil)
{
    UUID uuid;
    EXPECT_TRUE(uuid.isNil());
}

TEST(UUID_Test, GenerateIsNotNil)
{
    // A v4 UUID always has the version nibble (0x4) and the RFC 4122 variant bits
    // set, so a freshly generated UUID can never be nil.
    for (int i = 0; i < 16; ++i)
    {
        UUID uuid = UUID::Generate();
        EXPECT_FALSE(uuid.isNil());
    }
}

TEST(UUID_Test, ParsedAllZerosStringIsNil)
{
    UUID uuid("00000000-0000-0000-0000-000000000000");
    EXPECT_TRUE(uuid.isNil());
}

TEST(UUID_Test, ParsedNonZeroStringIsNotNil)
{
    UUID uuid("01234567-89ab-cdef-0123-456789abcdef");
    EXPECT_FALSE(uuid.isNil());
}

TEST(UUID_Test, ClearRestoresNil)
{
    UUID uuid = UUID::Generate();
    ASSERT_FALSE(uuid.isNil());

    uuid.clear();
    EXPECT_TRUE(uuid.isNil());
}

TEST(UUID_Test, FailedParseFallsBackToNil)
{
    // The string constructor zeroes the bytes first and parse() clears on failure,
    // so an unparsable string must yield the nil UUID, not garbage.
    UUID uuid("not-a-uuid");
    EXPECT_TRUE(uuid.isNil());
}

TEST(UUID_Test, ToStringRoundTripPreservesIdentity)
{
    const UUID original = UUID::Generate();
    const UUID roundTripped(original.toString());

    EXPECT_EQ(original, roundTripped);
    EXPECT_FALSE(roundTripped.isNil());

    const UUID nilRoundTrip(UUID().toString());
    EXPECT_TRUE(nilRoundTrip.isNil());
}
