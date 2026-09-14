#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "common/signaturecache.h"

using namespace std::chrono_literals;

/// @brief SignatureCache - block digests are computed once per distinct content and served from the cache
///        afterwards, keyed by the exact bytes. Entries carry creation and last-use timestamps and are evicted
///        synchronously by idle time and by a byte budget (least recently used first).
///
/// Every test runs on a manual clock, so timing is exact and nothing sleeps.
class SignatureCache_Test : public ::testing::Test
{
protected:
    using Clock = SignatureCache::Clock;

    Clock::time_point _now = Clock::time_point() + 1h;  // Non-zero epoch so "never" and "now" differ
    SignatureCache::Policy _defaultPolicy;

    void SetUp() override
    {
        _defaultPolicy = SignatureCache::GetPolicy();
        SignatureCache::SetClockForTesting([this]() { return _now; });
        SignatureCache::SetPolicy(SignatureCache::Policy{});
        SignatureCache::Clear();
    }

    void TearDown() override
    {
        SignatureCache::Clear();
        SignatureCache::SetPolicy(_defaultPolicy);
        SignatureCache::SetClockForTesting({});
    }

    void Advance(Clock::duration d) { _now += d; }

    /// Distinct 16 KB block per seed
    static std::vector<uint8_t> Block(uint8_t seed, size_t size = 0x4000)
    {
        std::vector<uint8_t> block(size);
        for (size_t i = 0; i < block.size(); i++)
            block[i] = static_cast<uint8_t>(i * 7 + seed * 31 + 3);
        return block;
    }

    static std::string Digest(const std::vector<uint8_t>& block)
    {
        return SignatureCache::Sha256Hex(block.data(), block.size());
    }

    static bool Cached(const std::vector<uint8_t>& block)
    {
        return SignatureCache::Inspect(block.data(), block.size()).found;
    }
};

/// region <Digest and cache access>

/// @brief Sha256Hex() digests on a miss, stores the result, and serves the same digest for a copy of the bytes
TEST_F(SignatureCache_Test, SameContentIsDigestedOnce)
{
    const auto block = Block(1);

    ASSERT_EQ(SignatureCache::Size(), 0u);
    EXPECT_TRUE(SignatureCache::Lookup(block.data(), block.size()).empty()) << "cold cache must miss";

    const std::string first = Digest(block);
    ASSERT_EQ(first.size(), 64u) << "hex SHA-256";
    EXPECT_EQ(SignatureCache::Size(), 1u);
    EXPECT_EQ(SignatureCache::Bytes(), block.size());
    EXPECT_EQ(SignatureCache::Lookup(block.data(), block.size()), first);

    // A different buffer with identical content resolves to the same entry, no new one
    const std::vector<uint8_t> copy(block);
    EXPECT_EQ(Digest(copy), first);
    EXPECT_EQ(SignatureCache::Size(), 1u);
    EXPECT_EQ(SignatureCache::Bytes(), block.size());
}

/// @brief The digest is the real SHA-256, not just a stable token: known vectors, cold and cached
TEST_F(SignatureCache_Test, DigestIsSha256)
{
    const uint8_t abc[] = {'a', 'b', 'c'};
    const char* expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    EXPECT_EQ(SignatureCache::Sha256Hex(abc, sizeof(abc)), expected) << "computed";
    EXPECT_EQ(SignatureCache::Sha256Hex(abc, sizeof(abc)), expected) << "served from cache";
}

/// @brief The key is the whole block: one changed byte, or a different length, is a different block
TEST_F(SignatureCache_Test, OneByteOrLengthDifferenceIsADifferentBlock)
{
    auto block = Block(2);
    const std::string original = Digest(block);

    block[0x1234] ^= 0x01;
    const std::string changed = Digest(block);
    EXPECT_NE(changed, original);
    EXPECT_EQ(SignatureCache::Size(), 2u);

    const std::string prefix = SignatureCache::Sha256Hex(block.data(), block.size() / 2);
    EXPECT_NE(prefix, changed);
    EXPECT_EQ(SignatureCache::Size(), 3u);
    EXPECT_EQ(SignatureCache::Bytes(), block.size() * 2 + block.size() / 2);
}

/// @brief Null and empty input never enter the cache, Store() ignores empty signatures, Clear() empties it
TEST_F(SignatureCache_Test, InvalidInputIsNotCachedAndClearEmpties)
{
    const auto block = Block(3);

    EXPECT_TRUE(SignatureCache::Sha256Hex(nullptr, block.size()).empty());
    EXPECT_TRUE(SignatureCache::Sha256Hex(block.data(), 0).empty());
    EXPECT_TRUE(SignatureCache::Lookup(nullptr, block.size()).empty());
    EXPECT_FALSE(SignatureCache::Inspect(nullptr, block.size()).found);
    EXPECT_EQ(SignatureCache::Size(), 0u);

    SignatureCache::Store(nullptr, 16, "not stored");
    SignatureCache::Store(block.data(), 0, "not stored");
    SignatureCache::Store(block.data(), 3, "");
    EXPECT_EQ(SignatureCache::Size(), 0u);

    Digest(block);
    ASSERT_EQ(SignatureCache::Size(), 1u);

    SignatureCache::Clear();
    EXPECT_EQ(SignatureCache::Size(), 0u);
    EXPECT_EQ(SignatureCache::Bytes(), 0u);
    EXPECT_TRUE(SignatureCache::Lookup(block.data(), block.size()).empty());
}

/// @brief Store()/Lookup() work for caller-computed digests; re-storing keeps the first signature
TEST_F(SignatureCache_Test, StoreAndLookupCallerDigest)
{
    const auto block = Block(4);

    SignatureCache::Store(block.data(), block.size(), "crc32:deadbeef");
    EXPECT_EQ(SignatureCache::Lookup(block.data(), block.size()), "crc32:deadbeef");

    SignatureCache::Store(block.data(), block.size(), "crc32:other");
    EXPECT_EQ(SignatureCache::Lookup(block.data(), block.size()), "crc32:deadbeef") << "an existing entry is not replaced";
    EXPECT_EQ(SignatureCache::Size(), 1u);
    EXPECT_EQ(SignatureCache::Bytes(), block.size());
}

/// endregion </Digest and cache access>

/// region <Timestamps>

/// @brief createdAt is set once; lastUsedAt and hits follow every served lookup; Inspect() does not count as use
TEST_F(SignatureCache_Test, TimestampsTrackCreationAndLastUse)
{
    const auto block = Block(5);
    const Clock::time_point created = _now;

    Digest(block);  // Miss: creates
    auto info = SignatureCache::Inspect(block.data(), block.size());
    ASSERT_TRUE(info.found);
    EXPECT_EQ(info.createdAt, created);
    EXPECT_EQ(info.lastUsedAt, created);
    EXPECT_EQ(info.hits, 0u);

    Advance(5s);
    Digest(block);  // Hit
    Advance(5s);
    SignatureCache::Lookup(block.data(), block.size());  // Hit

    info = SignatureCache::Inspect(block.data(), block.size());
    EXPECT_EQ(info.createdAt, created) << "creation time never moves";
    EXPECT_EQ(info.lastUsedAt, created + 10s);
    EXPECT_EQ(info.hits, 2u);

    Advance(5s);
    info = SignatureCache::Inspect(block.data(), block.size());
    EXPECT_EQ(info.lastUsedAt, created + 10s) << "Inspect() is diagnostics, not use";
    EXPECT_EQ(info.hits, 2u);

    // Re-storing an existing block refreshes last use without counting a hit
    SignatureCache::Store(block.data(), block.size(), "ignored");
    info = SignatureCache::Inspect(block.data(), block.size());
    EXPECT_EQ(info.lastUsedAt, created + 15s);
    EXPECT_EQ(info.hits, 2u);
}

/// endregion </Timestamps>

/// region <Idle eviction>

/// @brief An entry idle for maxIdle is evicted by the next sweep; one used in between survives
TEST_F(SignatureCache_Test, IdleEntriesAreEvicted)
{
    SignatureCache::Policy policy;
    policy.maxIdle = 60s;
    policy.sweepInterval = 10s;
    SignatureCache::SetPolicy(policy);

    const auto idle = Block(10);
    const auto busy = Block(11);
    Digest(idle);
    Digest(busy);

    Advance(40s);
    Digest(busy);  // Keeps busy fresh (sweep runs here: nothing is 60 s idle yet)
    EXPECT_TRUE(Cached(idle));
    EXPECT_TRUE(Cached(busy));

    Advance(20s);  // idle: 60 s unused, busy: 20 s
    EXPECT_EQ(SignatureCache::Sweep(), 1u);
    EXPECT_FALSE(Cached(idle));
    EXPECT_TRUE(Cached(busy));
    EXPECT_EQ(SignatureCache::Bytes(), busy.size());

    // An evicted block is simply digested again, with a fresh creation time
    const Clock::time_point recreated = _now;
    EXPECT_EQ(Digest(idle).size(), 64u);
    auto info = SignatureCache::Inspect(idle.data(), idle.size());
    ASSERT_TRUE(info.found);
    EXPECT_EQ(info.createdAt, recreated);
}

/// @brief The idle sweep runs synchronously inside ordinary calls once sweepInterval elapsed - no Sweep() needed
TEST_F(SignatureCache_Test, IdleSweepRunsInsideCallsOnInterval)
{
    SignatureCache::Policy policy;
    policy.maxIdle = 30s;
    policy.sweepInterval = 60s;
    SignatureCache::SetPolicy(policy);

    const auto stale = Block(12);
    const auto other = Block(13);
    Digest(stale);

    Advance(45s);  // stale is past maxIdle, but the sweep interval has not elapsed
    Digest(other);
    EXPECT_TRUE(Cached(stale)) << "no sweep before sweepInterval";

    Advance(20s);  // 65 s since the last sweep
    SignatureCache::Lookup(other.data(), other.size());
    EXPECT_FALSE(Cached(stale)) << "the lookup ran the due sweep";
    EXPECT_TRUE(Cached(other));
}

/// @brief Clear() and SetClockForTesting() restart the sweep interval
TEST_F(SignatureCache_Test, ClearRestartsSweepInterval)
{
    SignatureCache::Policy policy;
    policy.maxIdle = 1s;
    policy.sweepInterval = 60s;
    SignatureCache::SetPolicy(policy);

    Advance(120s);
    SignatureCache::Clear();

    const auto block = Block(14);
    Digest(block);
    Advance(30s);  // Idle past maxIdle, but only 30 s since Clear()
    SignatureCache::Lookup(Block(15).data(), 0x4000);
    EXPECT_TRUE(Cached(block));
}

/// endregion </Idle eviction>

/// region <Byte budget>

/// @brief Over maxBytes the least recently used entries go first, immediately on the insert that overflows
TEST_F(SignatureCache_Test, ByteBudgetEvictsLeastRecentlyUsed)
{
    SignatureCache::Policy policy;
    policy.maxBytes = 3 * 0x4000;  // Room for three pages
    policy.maxIdle = 24h;          // Idle eviction out of the way
    policy.sweepInterval = 24h;
    SignatureCache::SetPolicy(policy);

    const auto a = Block(20);
    const auto b = Block(21);
    const auto c = Block(22);
    const auto d = Block(23);

    Digest(a);
    Advance(1s);
    Digest(b);
    Advance(1s);
    Digest(c);
    Advance(1s);
    Digest(a);  // a becomes most recently used; b is now the oldest
    EXPECT_EQ(SignatureCache::Size(), 3u);

    Advance(1s);
    Digest(d);  // Overflows: b must go, nothing else
    EXPECT_EQ(SignatureCache::Size(), 3u);
    EXPECT_EQ(SignatureCache::Bytes(), 3u * 0x4000);
    EXPECT_TRUE(Cached(a));
    EXPECT_FALSE(Cached(b));
    EXPECT_TRUE(Cached(c));
    EXPECT_TRUE(Cached(d));
}

/// @brief A block larger than the whole budget is not admitted, so it cannot flush the entries that do fit
TEST_F(SignatureCache_Test, OversizedBlockIsNotRetained)
{
    SignatureCache::Policy policy;
    policy.maxBytes = 0x4000;
    policy.maxIdle = 24h;
    policy.sweepInterval = 24h;
    SignatureCache::SetPolicy(policy);

    const auto small = Block(24);
    Digest(small);
    Advance(1s);

    const auto huge = Block(25, 0x8000);
    EXPECT_EQ(Digest(huge).size(), 64u) << "the digest is still returned";
    EXPECT_FALSE(Cached(huge));
    EXPECT_TRUE(Cached(small)) << "existing entries are untouched";
    EXPECT_EQ(SignatureCache::Bytes(), small.size());
}

/// @brief Tightening the policy applies on Sweep(): idle and budget rules both enforced
TEST_F(SignatureCache_Test, SetPolicyAppliesOnSweep)
{
    for (uint8_t i = 0; i < 6; i++)
    {
        Digest(Block(30 + i));
        Advance(1s);
    }
    ASSERT_EQ(SignatureCache::Size(), 6u);

    SignatureCache::Policy policy;
    policy.maxIdle = 24h;
    policy.maxBytes = 2 * 0x4000;
    policy.sweepInterval = 24h;
    SignatureCache::SetPolicy(policy);

    EXPECT_EQ(SignatureCache::GetPolicy().maxBytes, policy.maxBytes);
    EXPECT_EQ(SignatureCache::Sweep(), 4u);
    EXPECT_EQ(SignatureCache::Size(), 2u);
    EXPECT_TRUE(Cached(Block(34))) << "the two most recent survive";
    EXPECT_TRUE(Cached(Block(35)));
}

/// endregion </Byte budget>

/// region <Concurrency>

/// @brief Many threads digesting an overlapping set of blocks under a tight budget: every returned digest is
///        correct and the bookkeeping stays consistent (run under TSan to check the lock discipline)
TEST_F(SignatureCache_Test, ConcurrentUseStaysConsistent)
{
    SignatureCache::SetClockForTesting({});  // Real clock: the manual one is not thread-safe
    SignatureCache::Policy policy;
    policy.maxBytes = 4 * 0x400;
    policy.maxIdle = 24h;
    policy.sweepInterval = 24h;
    SignatureCache::SetPolicy(policy);

    const size_t kBlocks = 12;
    std::vector<std::vector<uint8_t>> blocks;
    std::vector<std::string> expected;
    for (size_t i = 0; i < kBlocks; i++)
    {
        blocks.push_back(Block(static_cast<uint8_t>(40 + i), 0x400));
        expected.push_back(Digest(blocks.back()));
    }

    std::atomic<int> wrong{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++)
    {
        threads.emplace_back([&, t]() {
            for (int n = 0; n < 400; n++)
            {
                const size_t i = (size_t(t) * 5 + size_t(n)) % kBlocks;
                if (SignatureCache::Sha256Hex(blocks[i].data(), blocks[i].size()) != expected[i])
                    wrong++;
            }
        });
    }
    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(wrong.load(), 0);
    EXPECT_LE(SignatureCache::Bytes(), policy.maxBytes);
    EXPECT_EQ(SignatureCache::Bytes(), SignatureCache::Size() * 0x400u);
}

/// endregion </Concurrency>
