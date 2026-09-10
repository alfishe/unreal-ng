/// @file ttd_page_store_test.cpp
/// @brief Unit tests for the TTD codec page store (TTDCodecPageStore).
///
/// The codec page store superseded the original COW TTDPageStore in v2.
/// Coverage here is intentionally minimal — InternFull / InternXor / GetPage /
/// AddRef / Release semantics, capacity tracking, Reset. The full integration
/// coverage (sharing, free-list reuse under real capture patterns, compression
/// ratio) is exercised by the manager + seek + divergence tests.
///
/// v2 codec layout (see ttd_codec_page_store.h):
///   - kPageSize = 4096 (4 KB sub-pages; was 16 KB in v1)
///   - Each slot is one of: Full, XorPrev, Zero
///   - GetPage reconstructs bytes via XOR-prev chain walk + CRC verification

#include <gtest/gtest.h>

#include "debugger/ttd/ttd_codec_page_store.h"

#include <algorithm>
#include <cstring>
#include <vector>

using namespace ttd;

namespace
{
/// Fill a 4 KB buffer with a repeating byte pattern.
void FillPage(std::vector<uint8_t>& page, uint8_t pattern)
{
    page.resize(TTDCodecPageStore::kPageSize);
    std::fill(page.begin(), page.end(), pattern);
}

/// Fill a 4 KB buffer with an incremental pattern (page[i] = seed + i).
void FillPageIncremental(std::vector<uint8_t>& page, uint8_t seed)
{
    page.resize(TTDCodecPageStore::kPageSize);
    for (size_t i = 0; i < page.size(); ++i)
        page[i] = static_cast<uint8_t>(seed + i);
}
} // anonymous namespace

// ===========================================================================
// Constants and fresh-store state
// ===========================================================================

TEST(TTDCodecPageStore_Test, PageSize_Is4096Bytes_v2)
{
    // v2 codec: 4 KB sub-pages (was 16 KB in v1). See ttd_codec_page_store.h
    // header comment for the empirical justification (92.9% of dirty 16K pages
    // have only one dirty 4K sub-page, so 4K granularity saves 4x).
    EXPECT_EQ(TTDCodecPageStore::kPageSize, 4096u);
}

TEST(TTDCodecPageStore_Test, FreshStore_HasZeroCapacity)
{
    TTDCodecPageStore store;
    EXPECT_EQ(store.GetCapacity(), 0u);
    EXPECT_EQ(store.GetUsedSlots(), 0u);
    EXPECT_EQ(store.GetCapacityBytes(), 0u);
}

// ===========================================================================
// InternFull + GetPage round-trip
// ===========================================================================

TEST(TTDCodecPageStore_Test, InternFull_ReturnsDistinctSlots)
{
    TTDCodecPageStore store;
    std::vector<uint8_t> a, b;
    FillPageIncremental(a, 0x10);
    FillPageIncremental(b, 0x80);

    uint32_t idxA = store.InternFull(a.data());
    uint32_t idxB = store.InternFull(b.data());

    EXPECT_NE(idxA, idxB);
    EXPECT_EQ(store.GetUsedSlots(), 2u);
}

TEST(TTDCodecPageStore_Test, InternFull_RoundTripsViaGetPage)
{
    TTDCodecPageStore store;
    std::vector<uint8_t> page;
    FillPageIncremental(page, 0x42);

    uint32_t idx = store.InternFull(page.data());

    std::vector<uint8_t> out(TTDCodecPageStore::kPageSize, 0xFF);
    EXPECT_TRUE(store.GetPage(idx, out.data()));
    EXPECT_EQ(std::memcmp(out.data(), page.data(), TTDCodecPageStore::kPageSize), 0)
        << "GetPage must reconstruct the original bytes";
}

TEST(TTDCodecPageStore_Test, InternFull_AllZeroPage_RoundTrips)
{
    TTDCodecPageStore store;
    std::vector<uint8_t> zeroPage(TTDCodecPageStore::kPageSize, 0);

    uint32_t idx = store.InternFull(zeroPage.data());

    std::vector<uint8_t> out(TTDCodecPageStore::kPageSize, 0xAA);
    EXPECT_TRUE(store.GetPage(idx, out.data()));
    EXPECT_EQ(std::vector<uint8_t>(out.begin(), out.end()), zeroPage);
}

// ===========================================================================
// InternXor — delta encoding against a previous slot
// ===========================================================================

TEST(TTDCodecPageStore_Test, InternXor_ReconstructsOriginalBytes)
{
    TTDCodecPageStore store;

    // Base page: incremental pattern.
    std::vector<uint8_t> base;
    FillPageIncremental(base, 0x00);
    uint32_t baseIdx = store.InternFull(base.data());

    // Modified page: change a few bytes.
    std::vector<uint8_t> modified = base;
    modified[10] ^= 0xFF;
    modified[100] ^= 0xAA;
    modified[1000] ^= 0x55;

    uint32_t xorIdx = store.InternXor(baseIdx, modified.data());

    // Both indices must reconstruct to their original bytes.
    std::vector<uint8_t> outBase(TTDCodecPageStore::kPageSize, 0);
    std::vector<uint8_t> outMod(TTDCodecPageStore::kPageSize, 0);

    EXPECT_TRUE(store.GetPage(baseIdx, outBase.data()));
    EXPECT_TRUE(store.GetPage(xorIdx, outMod.data()));

    EXPECT_EQ(std::memcmp(outBase.data(), base.data(), TTDCodecPageStore::kPageSize), 0);
    EXPECT_EQ(std::memcmp(outMod.data(), modified.data(), TTDCodecPageStore::kPageSize), 0)
        << "InternXor must reconstruct modified bytes via XOR-prev chain";
}

TEST(TTDCodecPageStore_Test, InternXor_UnchangedPage_DedupsToPrevSlot)
{
    // v2 codec is content-aware: if XOR delta is all zeros, the codec store
    // returns prevSlot itself rather than allocating a new slot. This is the
    // key dedup that makes "dirty flag set but content unchanged" cheap.
    TTDCodecPageStore store;

    std::vector<uint8_t> page;
    FillPageIncremental(page, 0x33);
    uint32_t idx1 = store.InternFull(page.data());

    // Same content → no new slot needed.
    uint32_t idx2 = store.InternXor(idx1, page.data());
    EXPECT_EQ(idx1, idx2)
        << "InternXor with zero XOR delta must dedup to prevSlot";
    EXPECT_EQ(store.GetUsedSlots(), 1u);
}

// ===========================================================================
// Multi-hop XOR chains (delta-of-delta-of-delta)
// ===========================================================================

TEST(TTDCodecPageStore_Test, InternXor_MultiHopChain_ReconstructsCorrectly)
{
    TTDCodecPageStore store;

    std::vector<uint8_t> v0, v1, v2, v3;
    FillPageIncremental(v0, 0x00);
    v1 = v0; v1[100] ^= 0x11;
    v2 = v1; v2[200] ^= 0x22;
    v3 = v2; v3[300] ^= 0x33;

    uint32_t i0 = store.InternFull(v0.data());
    uint32_t i1 = store.InternXor(i0, v1.data());
    uint32_t i2 = store.InternXor(i1, v2.data());
    uint32_t i3 = store.InternXor(i2, v3.data());

    ASSERT_NE(i0, i1);
    ASSERT_NE(i1, i2);
    ASSERT_NE(i2, i3);

    std::vector<uint8_t> out(TTDCodecPageStore::kPageSize, 0);
    EXPECT_TRUE(store.GetPage(i3, out.data()));
    EXPECT_EQ(std::memcmp(out.data(), v3.data(), TTDCodecPageStore::kPageSize), 0)
        << "4-hop XOR chain must reconstruct the final version exactly";

    // Intermediate versions also reconstruct correctly.
    EXPECT_TRUE(store.GetPage(i1, out.data()));
    EXPECT_EQ(std::memcmp(out.data(), v1.data(), TTDCodecPageStore::kPageSize), 0);
}

// ===========================================================================
// AddRef / Release refcounting
// ===========================================================================

TEST(TTDCodecPageStore_Test, AddRef_IncrementsRefcount)
{
    TTDCodecPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x77);
    uint32_t idx = store.InternFull(page.data());

    EXPECT_EQ(store.GetRefCount(idx), 1u);
    store.AddRef(idx);
    EXPECT_EQ(store.GetRefCount(idx), 2u);
    store.AddRef(idx);
    EXPECT_EQ(store.GetRefCount(idx), 3u);
}

TEST(TTDCodecPageStore_Test, Release_DecrementsRefcount_AndFreesOnZero)
{
    TTDCodecPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x55);
    uint32_t idx = store.InternFull(page.data());

    store.AddRef(idx);
    store.AddRef(idx);
    EXPECT_EQ(store.GetRefCount(idx), 3u);

    store.Release(idx);
    EXPECT_EQ(store.GetRefCount(idx), 2u);
    store.Release(idx);
    EXPECT_EQ(store.GetRefCount(idx), 1u);
    store.Release(idx);
    // Slot is now free; refcount query on a free slot is implementation-defined
    // but used-slots count must drop.
    EXPECT_EQ(store.GetUsedSlots(), 0u);
}

// ===========================================================================
// Reset
// ===========================================================================

TEST(TTDCodecPageStore_Test, Reset_ClearsAllSlots)
{
    TTDCodecPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x99);
    store.InternFull(page.data());
    store.InternFull(page.data());
    ASSERT_GT(store.GetUsedSlots(), 0u);
    ASSERT_GT(store.GetCapacity(), 0u);

    store.Reset();

    EXPECT_EQ(store.GetUsedSlots(), 0u);
    EXPECT_EQ(store.GetCapacity(), 0u);
    EXPECT_EQ(store.GetCapacityBytes(), 0u);
}
