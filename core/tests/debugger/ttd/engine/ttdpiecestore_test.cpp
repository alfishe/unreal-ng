/// @file ttdpiecestore_test.cpp
/// @brief The engine's piece store: each change stored once, the chain length
/// limit per piece, encode once, release along dependencies
/// (docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.3, §6).

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

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
