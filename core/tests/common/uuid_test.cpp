#include "pch.h"

#include "common/uuid.h"

#include <set>
#include <string>
#include <thread>
#include <vector>

/// @brief Locks UUID::isNil() semantics. An earlier revision returned the inverted
/// result (true for any non-zero byte), which EmulatorContext and the DeZog adapter
/// had to work around with default-constructed-UUID comparisons, and which
/// EmulatorManager_Test.CreateEmulator unknowingly depended on. These tests pin the
/// documented contract: isNil() == true if and only if all 16 bytes are zero.
TEST(UUID_Test, DefaultConstructedIsNil)
{
    unreal::UUID uuid;
    EXPECT_TRUE(uuid.isNil());
}

TEST(UUID_Test, GenerateIsNotNil)
{
    // A v4 UUID always has the version nibble (0x4) and the RFC 4122 variant bits
    // set, so a freshly generated UUID can never be nil.
    for (int i = 0; i < 16; ++i)
    {
        unreal::UUID uuid = unreal::UUID::Generate();
        EXPECT_FALSE(uuid.isNil());
    }
}

TEST(UUID_Test, ParsedAllZerosStringIsNil)
{
    unreal::UUID uuid("00000000-0000-0000-0000-000000000000");
    EXPECT_TRUE(uuid.isNil());
}

TEST(UUID_Test, ParsedNonZeroStringIsNotNil)
{
    unreal::UUID uuid("01234567-89ab-cdef-0123-456789abcdef");
    EXPECT_FALSE(uuid.isNil());
}

TEST(UUID_Test, ClearRestoresNil)
{
    unreal::UUID uuid = unreal::UUID::Generate();
    ASSERT_FALSE(uuid.isNil());

    uuid.clear();
    EXPECT_TRUE(uuid.isNil());
}

TEST(UUID_Test, FailedParseFallsBackToNil)
{
    // The string constructor zeroes the bytes first and parse() clears on failure,
    // so an unparsable string must yield the nil UUID, not garbage.
    unreal::UUID uuid("not-a-uuid");
    EXPECT_TRUE(uuid.isNil());
}

TEST(UUID_Test, ToStringRoundTripPreservesIdentity)
{
    const unreal::UUID original = unreal::UUID::Generate();
    const unreal::UUID roundTripped(original.toString());

    EXPECT_EQ(original, roundTripped);
    EXPECT_FALSE(roundTripped.isNil());

    const unreal::UUID nilRoundTrip(unreal::UUID().toString());
    EXPECT_TRUE(nilRoundTrip.isNil());
}

/// Instances are created from several automation threads at once (one per WebAPI connection). Generate() drew from
/// one static mt19937 shared by every thread with no lock - a data race on its state: two threads could read the same
/// state and return the same id, and the manager's map would then hold one of the two instances under it
TEST(UUID_Test, ThreadsGeneratingAtOnceGetDistinctIds)
{
    constexpr int kThreads = 8;
    constexpr int kEach = 1500;
    std::vector<std::vector<std::string>> ids(kThreads);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&ids, t] {
            ids[t].reserve(kEach);
            for (int i = 0; i < kEach; ++i)
                ids[t].push_back(unreal::UUID::Generate().toString());
        });
    }
    for (std::thread& thread : threads)
        thread.join();

    std::set<std::string> all;
    for (const auto& list : ids)
        all.insert(list.begin(), list.end());
    EXPECT_EQ(all.size(), static_cast<size_t>(kThreads * kEach)) << "duplicate ids generated concurrently";
}
