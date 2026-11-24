/// @file ttdpiecestore_test.cpp
/// @brief The engine's piece store: each change stored once, the chain length
/// limit per piece, encode once, release along dependencies
/// (docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.3, §6).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "_helpers/testtier.h"
#include "debugger/ttd/engine/ttdpiecestore.h"
#include "debugger/ttd/engine/ttdregion.h"

using namespace ttd;

namespace
{

std::vector<uint8_t> Noise(uint32_t seed)
{
    std::vector<uint8_t> b(kTTDPieceSize);
    uint32_t x = seed * 2654435761u + 1;
    for (uint8_t& v : b)
    {
        x = x * 1664525u + 1013904223u;
        v = static_cast<uint8_t>(x >> 24);
    }
    return b;
}

std::vector<uint8_t> Pattern(uint8_t seed)
{
    std::vector<uint8_t> b(kTTDPieceSize);
    for (size_t i = 0; i < b.size(); ++i)
        b[i] = static_cast<uint8_t>(seed + (i >> 6));
    return b;
}

std::vector<uint8_t> Decoded(const TTDPieceStore& store, TTDPieceId id)
{
    std::vector<uint8_t> out(kTTDPieceSize);
    EXPECT_TRUE(store.Decode(id, out.data()));
    return out;
}

}  // namespace

TEST(TTDPieceStore_Test, UnchangedContentAddsAReference_NotAVersion)
{
    TTDPieceStore store;
    const auto a = Pattern(1);
    const TTDPieceId v0 = store.InternFirst(a.data());
    const TTDPieceId v1 = store.Intern(v0, a.data(), a.data());
    EXPECT_EQ(v1, v0);
    EXPECT_EQ(store.RefCount(v0), 2u);
    EXPECT_EQ(store.LiveVersions(), 1u);
}

TEST(TTDPieceStore_Test, AllZeroContentIsAZeroVersion)
{
    TTDPieceStore store;
    const auto a = Pattern(1);
    const std::vector<uint8_t> zero(kTTDPieceSize, 0);
    const TTDPieceId v0 = store.InternFirst(a.data());
    const TTDPieceId v1 = store.Intern(v0, a.data(), zero.data());
    EXPECT_EQ(store.EncodingOf(v1), TTDPieceStore::Encoding::Zero);
    EXPECT_EQ(store.PayloadSize(v1), 0u);
    EXPECT_EQ(Decoded(store, v1), zero);
}

TEST(TTDPieceStore_Test, SmallChangeIsADifference_DecodedThroughItsChain)
{
    TTDPieceStore store;
    auto a = Pattern(1);
    TTDPieceId id = store.InternFirst(a.data());
    std::vector<std::vector<uint8_t>> versions = {a};
    std::vector<TTDPieceId> ids = {id};
    for (int i = 1; i < 10; ++i)
    {
        auto b = versions.back();
        b[i * 37] ^= 0x5A;
        id = store.Intern(ids.back(), versions.back().data(), b.data());
        EXPECT_EQ(store.EncodingOf(id), TTDPieceStore::Encoding::Ranges) << "one byte changed: its run, as it is";
        EXPECT_EQ(store.DepthOf(id), i);
        EXPECT_EQ(store.BaseOf(id), ids.back()) << "a difference depends on the version before it";
        versions.push_back(b);
        ids.push_back(id);
    }
    for (size_t i = 0; i < ids.size(); ++i)
        EXPECT_EQ(Decoded(store, ids[i]), versions[i]) << "version " << i;
}

TEST(TTDPieceStore_Test, ChainLimitStoresFullExactlyAtK)
{
    TTDPieceStore::Params p;
    p.chainLimit = 5;
    TTDPieceStore store(p);
    auto cur = Pattern(1);
    TTDPieceId id = store.InternFirst(cur.data());
    for (int i = 1; i <= 12; ++i)
    {
        auto next = cur;
        next[i] ^= 1;
        id = store.Intern(id, cur.data(), next.data());
        cur = next;
        // depths 1..4, then Full (0) at what would be depth 5, then 1..4 again
        const uint16_t expect = static_cast<uint16_t>(i % 5);
        EXPECT_EQ(store.DepthOf(id), expect) << "change " << i;
        EXPECT_EQ(store.EncodingOf(id) == TTDPieceStore::Encoding::Full, expect == 0) << "change " << i;
        EXPECT_EQ(Decoded(store, id), cur);
    }
    EXPECT_EQ(store.GetWork().forcedFull, 2u);
}

TEST(TTDPieceStore_Test, EncodeOnce_FullCompressedOnlyForALargeDifference)
{
    TTDPieceStore store;
    const auto a = Pattern(1);
    const TTDPieceId v0 = store.InternFirst(a.data());

    auto small = a;
    small[100] ^= 0xFF;
    store.ResetWork();
    store.Intern(v0, a.data(), small.data());
    EXPECT_EQ(store.GetWork().compressCalls, 0u) << "a few bytes changed: stored as runs, nothing compressed";

    auto medium = a;
    for (int i = 0; i < 200; ++i)
        medium[i * 17] ^= 0x5A;   // 200 scattered bytes: runs would cost 800
    store.ResetWork();
    store.Intern(v0, a.data(), medium.data());
    EXPECT_EQ(store.GetWork().compressCalls, 1u) << "a larger difference: one compression";

    const auto noise = Noise(7);
    store.ResetWork();
    const TTDPieceId big = store.Intern(v0, a.data(), noise.data());
    EXPECT_EQ(store.GetWork().compressCalls, 2u) << "a large difference: the full piece is tried too";
    EXPECT_EQ(Decoded(store, big), noise);
}

TEST(TTDPieceStore_Test, FullKeptWhenSmallerThanTheDifference)
{
    // Noise replaced by a simple pattern: the difference is noise, the new content compresses well
    TTDPieceStore store;
    const auto noise = Noise(3);
    const auto pattern = Pattern(9);
    const TTDPieceId v0 = store.InternFirst(noise.data());
    const TTDPieceId v1 = store.Intern(v0, noise.data(), pattern.data());
    EXPECT_EQ(store.EncodingOf(v1), TTDPieceStore::Encoding::Full);
    EXPECT_EQ(store.DepthOf(v1), 0u);
    EXPECT_EQ(store.RefCount(v0), 1u) << "a full version holds no reference to the old one";
    EXPECT_EQ(Decoded(store, v1), pattern);
}

TEST(TTDPieceStore_Test, ReleaseFreesAlongTheChain)
{
    TTDPieceStore store;
    auto a = Pattern(1);
    const TTDPieceId v0 = store.InternFirst(a.data());
    auto b = a;
    b[5] ^= 1;
    const TTDPieceId v1 = store.Intern(v0, a.data(), b.data());
    ASSERT_EQ(store.RefCount(v0), 2u) << "held by its owner and by the difference built on it";

    store.Release(v0);   // the owner lets go: the base survives for v1
    EXPECT_EQ(store.LiveVersions(), 2u);
    EXPECT_EQ(Decoded(store, v1), b);

    store.Release(v1);   // the last reference: v1 goes, and the base with it
    EXPECT_EQ(store.LiveVersions(), 0u);
    EXPECT_EQ(store.PayloadBytes(), 0u);
}

TEST(TTDPieceStore_Test, PayloadsLiveInTheArenaAtTheirExactSize)
{
    TTDPieceStore store;
    size_t payload = 0;
    std::vector<TTDPieceId> ids;
    for (uint8_t i = 0; i < 200; ++i)
    {
        const auto a = Pattern(i);
        ids.push_back(store.InternFirst(a.data()));
        payload += store.PayloadSize(ids.back());
    }
    EXPECT_EQ(store.PayloadBytes(), payload) << "exact sizes, no per-payload slack";
    EXPECT_LE(store.ArenaBytes(), TTDArena::kChunkBytes + 4096) << "200 small payloads share one chunk";
    for (const TTDPieceId id : ids)
        store.Release(id);
    EXPECT_EQ(store.PayloadBytes(), 0u);
}

/// Ranges: a few changed bytes stored as (offset, length, XOR bytes) runs,
/// without compression; decoded exactly at the piece's edges, across long
/// runs, and in a chain that mixes runs and compressed differences
TEST(TTDPieceStore_Test, RangesStoreFewChangesExactlyAndCheaply)
{
    TTDPieceStore store;
    auto a = Pattern(3);
    TTDPieceId id = store.InternFirst(a.data());
    std::vector<std::vector<uint8_t>> versions = {a};
    std::vector<TTDPieceId> ids = {id};
    auto add = [&](std::vector<uint8_t> next) {
        id = store.Intern(ids.back(), versions.back().data(), next.data());
        versions.push_back(std::move(next));
        ids.push_back(id);
        return id;
    };

    auto b = a;
    b[0] ^= 1;
    b[kTTDPieceSize - 1] ^= 2;   // the first and the last byte
    const TTDPieceId edges = add(b);
    EXPECT_EQ(store.EncodingOf(edges), TTDPieceStore::Encoding::Ranges);
    EXPECT_EQ(store.PayloadSize(edges), 2u * (3 + 1)) << "two runs of one byte, 3 bytes of header each";

    auto c = versions.back();
    for (size_t i = 1000; i < 1300; ++i)
        c[i] ^= 0x80;   // one run of 300 bytes: split at 255
    add(c);

    add(Noise(9));   // a large difference: compressed XOR or full
    auto d = versions.back();
    d[2048] ^= 0xFF;
    d[2050] ^= 0x01;   // two changes 1 byte apart: one run of 3
    const TTDPieceId merged = add(d);
    EXPECT_EQ(store.EncodingOf(merged), TTDPieceStore::Encoding::Ranges);
    EXPECT_EQ(store.PayloadSize(merged), 3u + 3u);

    for (size_t i = 0; i < ids.size(); ++i)
        EXPECT_EQ(Decoded(store, ids[i]), versions[i]) << "version " << i;
    for (size_t i = ids.size(); i-- > 0;)
        store.Release(ids[i]);
    EXPECT_EQ(store.LiveVersions(), 0u) << "runs hold their base like any difference";
}

/// DecodeFrom moves a piece's bytes from one version of its chain to another by the differences between them, up
/// or down the chain (an XOR difference turns either way): one link for a neighbour, not the chain from its start
/// (the picture's restore of the frame before a seek's, P1.S10 seek tails). Versions of another chain, or a step
/// longer than the chain from its start, are left to Decode
TEST(TTDPieceStore_Test, DecodeFromMovesAlongTheChainEitherWay)
{
    TTDPieceStore store;
    std::vector<std::vector<uint8_t>> versions = {Noise(1)};
    std::vector<TTDPieceId> ids = {store.InternFirst(versions[0].data())};
    for (int i = 1; i < 30; ++i)
    {
        auto b = versions.back();
        for (int k = 0; k < (i % 3 == 0 ? 600 : 3); ++k)   // XOR differences and byte ranges
            b[(i * 131 + k * 7) % kTTDPieceSize] ^= static_cast<uint8_t>(i + k);
        ids.push_back(store.Intern(ids.back(), versions.back().data(), b.data()));
        versions.push_back(b);
    }
    ASSERT_GT(store.DepthOf(ids.back()), 20u);
    bool damaged = false;
    for (const auto& [from, to] : {std::pair{20, 21}, std::pair{21, 20}, std::pair{25, 18}, std::pair{3, 29}})
    {
        std::vector<uint8_t> bytes = versions[from];
        store.ResetWork();
        ASSERT_TRUE(store.DecodeFrom(ids[to], ids[from], bytes.data(), damaged)) << from << " -> " << to;
        EXPECT_EQ(bytes, versions[to]) << from << " -> " << to;
        EXPECT_EQ(store.GetWork().linksDecoded, static_cast<uint64_t>(std::abs(to - from)));
    }
    // Down a long way: from the chain's start is cheaper
    std::vector<uint8_t> bytes = versions[28];
    EXPECT_FALSE(store.DecodeFrom(ids[2], ids[28], bytes.data(), damaged));
    EXPECT_FALSE(damaged);
    EXPECT_EQ(bytes, versions[28]) << "untouched";
    // Another chain
    const auto other = Noise(99);
    const TTDPieceId foreign = store.InternFirst(other.data());
    EXPECT_FALSE(store.DecodeFrom(ids[5], foreign, bytes.data(), damaged));
    EXPECT_FALSE(damaged);
}

namespace
{
/// A chain grown the way a capture grows it: the caller keeps the content of the chain's last anchor point from the
/// first version stored after it, and hands it to Intern
struct AnchoredChain
{
    TTDPieceStore& store;
    std::vector<std::vector<uint8_t>> versions;
    std::vector<TTDPieceId> ids;
    TTDPieceId anchor = TTDPieceStore::kNone;
    std::vector<uint8_t> anchorBytes = std::vector<uint8_t>(kTTDPieceSize);

    AnchoredChain(TTDPieceStore& s, std::vector<uint8_t> first) : store(s)
    {
        ids.push_back(store.InternFirst(first.data()));
        versions.push_back(std::move(first));
    }
    TTDPieceId Add(const std::vector<uint8_t>& next)
    {
        if (store.IsAnchorPoint(ids.back()) && anchor != ids.back())
        {
            anchor = ids.back();
            anchorBytes = versions.back();
        }
        ids.push_back(store.Intern(ids.back(), versions.back().data(), next.data(), anchor, anchorBytes.data()));
        versions.push_back(next);
        return ids.back();
    }
};
}  // namespace

/// Every S-th version of a chain is a difference from the version S before it (its anchor), the others from the
/// previous one: a decode of the chain's last versions applies (K - 1) / S + S - 1 differences, not K - 1 (a seek
/// decodes every piece of memory through its chain; P1.S10 seek format). The K-th version is still stored whole
TEST(TTDPieceStore_Test, AnchorPointsSkipTheVersionsBetween)
{
    TTDPieceStore::Params p;
    p.chainLimit = 50;
    p.skipStride = 7;
    TTDPieceStore store(p);
    AnchoredChain chain(store, Noise(3));
    for (int i = 1; i <= 60; ++i)
    {
        auto next = chain.versions.back();
        for (int k = 0; k < (i % 4 == 0 ? 500 : 2); ++k)   // XOR differences and byte ranges
            next[(i * 131 + k * 7) % kTTDPieceSize] ^= static_cast<uint8_t>(1 + i + k);
        const TTDPieceId id = chain.Add(next);
        const int run = i % 50;   // the 50th version starts a chain again
        ASSERT_EQ(store.RunOf(id), run) << "version " << i;
        EXPECT_EQ(store.DepthOf(id), run / 7 + run % 7) << "version " << i;
        if (run == 0)
            EXPECT_EQ(store.EncodingOf(id), TTDPieceStore::Encoding::Full);
        else
            EXPECT_EQ(store.BaseOf(id), run % 7 == 0 ? chain.ids[i - 7] : chain.ids[i - 1]) << "version " << i;
    }
    EXPECT_EQ(store.GetWork().forcedFull, 1u);
    for (size_t i = 0; i < chain.ids.size(); ++i)
        EXPECT_EQ(Decoded(store, chain.ids[i]), chain.versions[i]) << "version " << i;
    store.ResetWork();
    Decoded(store, chain.ids[48]);
    EXPECT_EQ(store.GetWork().linksDecoded, 12u) << "six anchors and six versions after the last, not 48";

    // Along the chain: an anchor point and its anchor are neighbours; the version before an anchor point is not on
    // its way (left to Decode)
    bool damaged = false;
    std::vector<uint8_t> bytes = chain.versions[7];
    ASSERT_TRUE(store.DecodeFrom(chain.ids[14], chain.ids[7], bytes.data(), damaged));
    EXPECT_EQ(bytes, chain.versions[14]);
    ASSERT_TRUE(store.DecodeFrom(chain.ids[7], chain.ids[14], bytes.data(), damaged));
    EXPECT_EQ(bytes, chain.versions[7]);
    bytes = chain.versions[6];
    EXPECT_FALSE(store.DecodeFrom(chain.ids[7], chain.ids[6], bytes.data(), damaged));
    EXPECT_FALSE(damaged);
    EXPECT_EQ(bytes, chain.versions[6]) << "untouched";

    for (size_t i = chain.ids.size(); i-- > 0;)
        store.Release(chain.ids[i]);
    EXPECT_EQ(store.LiveVersions(), 0u) << "an anchor point holds its anchor like any difference its base";
}

/// An anchor the caller got wrong is not used: a version of another chain, or one that is not S versions back,
/// leaves the new version a difference from the previous one, and it decodes to its content. A piece back at its
/// anchor's content at an anchor point takes the anchor's version again (no empty difference is stored)
TEST(TTDPieceStore_Test, AnAnchorThatIsNotTheChainsIsIgnored)
{
    TTDPieceStore::Params p;
    p.skipStride = 4;
    TTDPieceStore store(p);
    const auto other = Noise(50);
    const TTDPieceId foreign = store.InternFirst(other.data());   // an anchor point of its own chain (run 0)
    std::vector<std::vector<uint8_t>> versions = {Noise(1)};
    std::vector<TTDPieceId> ids = {store.InternFirst(versions[0].data())};
    for (int i = 1; i <= 4; ++i)
    {
        auto next = versions.back();
        next[i * 50] ^= 0x11;
        ids.push_back(store.Intern(ids.back(), versions.back().data(), next.data(), foreign, other.data()));
        versions.push_back(next);
        EXPECT_EQ(store.BaseOf(ids.back()), ids[i - 1]) << "version " << i;
        EXPECT_EQ(Decoded(store, ids.back()), versions.back()) << "version " << i;
    }
    EXPECT_EQ(store.DepthOf(ids[4]), 4u);

    // Back to the anchor's content at the anchor point
    TTDPieceStore same(p);
    AnchoredChain chain(same, Noise(2));
    for (int i = 1; i <= 3; ++i)
    {
        auto next = chain.versions.back();
        next[i] ^= 0x40;
        chain.Add(next);
    }
    const size_t live = same.LiveVersions();
    const TTDPieceId back = chain.Add(chain.versions[0]);
    EXPECT_EQ(back, chain.ids[0]);
    EXPECT_EQ(same.RefCount(back), 3u) << "its own, the chain's first difference, the version again";
    EXPECT_EQ(same.LiveVersions(), live);
    EXPECT_EQ(Decoded(same, back), chain.versions[0]);
    // The chain goes on from the anchor
    auto next = chain.versions[0];
    next[7] ^= 0x01;
    const TTDPieceId after = chain.Add(next);
    EXPECT_EQ(same.BaseOf(after), back);
    EXPECT_EQ(same.RunOf(after), 1u);
    EXPECT_EQ(Decoded(same, after), next);
}

/// Chains grown at random (a few bytes, a block, the whole piece, zeros, the anchor's content again), with the
/// anchors a capture keeps: every version decodes to its content, no chain is longer than the bound the stride
/// gives, and releasing every version newest first frees the store. The long tier grows 40 pieces through 3,000
/// versions each (UNREAL_LONG_TESTS=1); the short one 6 pieces through 300 (a few milliseconds)
TEST(TTDPieceStore_Test, RandomAnchoredChainsDecodeToTheirContent)
{
    TTDPieceStore::Params p;
    p.chainLimit = 50;
    p.skipStride = 7;
    TTDPieceStore store(p);
    const int pieces = TestTier::Pick<int>(6, 40);
    const int versions = TestTier::Pick<int>(300, 3000);
    uint32_t x = 12345;
    auto next = [&x]() {
        x = x * 1664525u + 1013904223u;
        return x >> 8;
    };
    uint32_t deepest = 0;
    for (int piece = 0; piece < pieces; ++piece)
    {
        AnchoredChain chain(store, Noise(100 + static_cast<uint32_t>(piece)));
        for (int v = 1; v < versions; ++v)
        {
            std::vector<uint8_t> bytes = chain.versions.back();
            const uint32_t kind = next() % 64;
            if (kind == 0)
                bytes.assign(kTTDPieceSize, 0);
            else if (kind == 1)
                bytes = Noise(next());
            else if (kind < 4)
                bytes = chain.anchorBytes;   // back to the anchor's content (zeros before the chain has one)
            else if (kind < 16)
                for (uint32_t k = 0, at = next() % 3000, n = 100 + next() % 900; k < n; ++k)
                    bytes[at + k] = static_cast<uint8_t>(next());
            else
                for (uint32_t k = 0, n = 1 + next() % 6; k < n; ++k)
                    bytes[next() % kTTDPieceSize] ^= static_cast<uint8_t>(1 + next() % 255);
            const TTDPieceId id = chain.Add(bytes);
            deepest = std::max<uint32_t>(deepest, store.DepthOf(id));
        }
        for (size_t i = 0; i < chain.ids.size(); i += (TestTier::Long() ? 1 : 3))
            ASSERT_EQ(Decoded(store, chain.ids[i]), chain.versions[i]) << "piece " << piece << " version " << i;
        for (size_t i = chain.ids.size(); i-- > 0;)
            store.Release(chain.ids[i]);
    }
    EXPECT_LE(deepest, 49u / 7u + 6u) << "(K - 1) / S anchors and S - 1 versions after the last";
    EXPECT_GT(deepest, 6u);
    EXPECT_EQ(store.LiveVersions(), 0u);
}
