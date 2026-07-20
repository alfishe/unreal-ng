/// @file ttd_page_store_test.cpp
/// @brief Unit tests for the TTD COW page store (TDD §6.3, §6.5).
///
/// Coverage:
///   - Basic Intern / GetPage / AddRef / Release semantics
///   - COW sharing: multiple checkpoints sharing one slot
///   - Free-list reuse after Release
///   - Memory usage tracking (capacity vs used vs free)
///   - Reset (session invalidation)
///   - Data integrity and isolation
///   - Stress / fragmentation patterns

#include <gtest/gtest.h>

#include "debugger/ttd/ttd_page_store.h"

#include <algorithm>
#include <cstring>
#include <vector>

using namespace ttd;

namespace
{
/// Fill a 16 KB buffer with a repeating byte pattern.
void FillPage(std::vector<uint8_t>& page, uint8_t pattern)
{
    page.resize(TTDPageStore::kPageSize);
    std::fill(page.begin(), page.end(), pattern);
}

/// Fill a 16 KB buffer with an incremental pattern (page[i] = seed + i).
void FillPageIncremental(std::vector<uint8_t>& page, uint8_t seed)
{
    page.resize(TTDPageStore::kPageSize);
    for (size_t i = 0; i < page.size(); ++i)
        page[i] = static_cast<uint8_t>(seed + i);
}

/// Compare a stored page against an expected buffer.
bool PageEquals(const uint8_t* stored, const std::vector<uint8_t>& expected)
{
    return std::memcmp(stored, expected.data(), TTDPageStore::kPageSize) == 0;
}
} // anonymous namespace

// ===========================================================================
// Fresh store state
// ===========================================================================

TEST(TTDPageStore_Test, FreshStore_HasZeroCapacity)
{
    TTDPageStore store;
    EXPECT_EQ(store.GetCapacity(), 0u);
    EXPECT_EQ(store.GetUsedSlots(), 0u);
    EXPECT_EQ(store.GetFreeSlotCount(), 0u);
    EXPECT_EQ(store.GetCapacityBytes(), 0u);
    EXPECT_EQ(store.GetUsedBytes(), 0u);
}

// ===========================================================================
// Basic Intern
// ===========================================================================

TEST(TTDPageStore_Test, Intern_ReturnsIndexZero_ForFirstPage)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0xAB);

    uint32_t idx = store.Intern(page.data());
    EXPECT_EQ(idx, 0u);
}

TEST(TTDPageStore_Test, Intern_SetsRefcountToOne)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x42);

    uint32_t idx = store.Intern(page.data());
    EXPECT_EQ(store.GetRefCount(idx), 1u);
}

TEST(TTDPageStore_Test, Intern_StoresExactPageData)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPageIncremental(page, 0x10);

    uint32_t idx = store.Intern(page.data());
    const uint8_t* stored = store.GetPage(idx);
    EXPECT_TRUE(PageEquals(stored, page));
}

TEST(TTDPageStore_Test, Intern_IncrementsCapacity)
{
    TTDPageStore store;

    std::vector<uint8_t> p1, p2, p3;
    FillPage(p1, 0x01);
    FillPage(p2, 0x02);
    FillPage(p3, 0x03);

    store.Intern(p1.data());
    EXPECT_EQ(store.GetCapacity(), 1u);
    EXPECT_EQ(store.GetUsedSlots(), 1u);

    store.Intern(p2.data());
    EXPECT_EQ(store.GetCapacity(), 2u);
    EXPECT_EQ(store.GetUsedSlots(), 2u);

    store.Intern(p3.data());
    EXPECT_EQ(store.GetCapacity(), 3u);
    EXPECT_EQ(store.GetUsedSlots(), 3u);
}

TEST(TTDPageStore_Test, Intern_CapacityBytes_EqualsCapacityTimesPageSize)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x77);

    store.Intern(page.data());
    store.Intern(page.data());
    store.Intern(page.data());

    EXPECT_EQ(store.GetCapacityBytes(), 3u * TTDPageStore::kPageSize);
    EXPECT_EQ(store.GetUsedBytes(), 3u * TTDPageStore::kPageSize);
}

// ===========================================================================
// Data isolation
// ===========================================================================

TEST(TTDPageStore_Test, Intern_CopiesData_SourceMutationDoesNotAffectStored)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0xAA);

    uint32_t idx = store.Intern(page.data());

    // Mutate the source buffer
    std::fill(page.begin(), page.end(), 0xFF);

    // Stored data must be unaffected
    const uint8_t* stored = store.GetPage(idx);
    std::vector<uint8_t> expected;
    FillPage(expected, 0xAA);
    EXPECT_TRUE(PageEquals(stored, expected));
}

TEST(TTDPageStore_Test, GetPage_ReturnsDistinctPointers_ForDistinctSlots)
{
    TTDPageStore store;
    std::vector<uint8_t> p1, p2;
    FillPage(p1, 0x11);
    FillPage(p2, 0x22);

    uint32_t idx1 = store.Intern(p1.data());
    uint32_t idx2 = store.Intern(p2.data());

    const uint8_t* ptr1 = store.GetPage(idx1);
    const uint8_t* ptr2 = store.GetPage(idx2);

    EXPECT_NE(ptr1, ptr2);
    EXPECT_EQ(*ptr1, 0x11);
    EXPECT_EQ(*ptr2, 0x22);
}

// ===========================================================================
// AddRef
// ===========================================================================

TEST(TTDPageStore_Test, AddRef_IncrementsRefcount)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x55);

    uint32_t idx = store.Intern(page.data());
    EXPECT_EQ(store.GetRefCount(idx), 1u);

    store.AddRef(idx);
    EXPECT_EQ(store.GetRefCount(idx), 2u);

    store.AddRef(idx);
    EXPECT_EQ(store.GetRefCount(idx), 3u);
}

TEST(TTDPageStore_Test, AddRef_DoesNotChangeUsedSlots)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x55);

    uint32_t idx = store.Intern(page.data());
    EXPECT_EQ(store.GetUsedSlots(), 1u);

    store.AddRef(idx);
    store.AddRef(idx);
    store.AddRef(idx);

    // AddRef on an existing slot does NOT create a new slot
    EXPECT_EQ(store.GetUsedSlots(), 1u);
    EXPECT_EQ(store.GetCapacity(), 1u);
}

// ===========================================================================
// Release
// ===========================================================================

TEST(TTDPageStore_Test, Release_ToZero_FreesSlot)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x33);

    uint32_t idx = store.Intern(page.data());
    EXPECT_EQ(store.GetUsedSlots(), 1u);
    EXPECT_EQ(store.GetFreeSlotCount(), 0u);

    store.Release(idx);

    EXPECT_EQ(store.GetRefCount(idx), 0u);
    EXPECT_EQ(store.GetUsedSlots(), 0u);
    EXPECT_EQ(store.GetFreeSlotCount(), 1u);
}

TEST(TTDPageStore_Test, Release_WithMultipleRefs_KeepsSlot)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x66);

    uint32_t idx = store.Intern(page.data());
    store.AddRef(idx);
    store.AddRef(idx);
    EXPECT_EQ(store.GetRefCount(idx), 3u);

    store.Release(idx);
    EXPECT_EQ(store.GetRefCount(idx), 2u);
    EXPECT_EQ(store.GetUsedSlots(), 1u);  // Still in use

    store.Release(idx);
    EXPECT_EQ(store.GetRefCount(idx), 1u);
    EXPECT_EQ(store.GetUsedSlots(), 1u);  // Still in use

    store.Release(idx);
    EXPECT_EQ(store.GetRefCount(idx), 0u);
    EXPECT_EQ(store.GetUsedSlots(), 0u);  // Now freed
}

// ===========================================================================
// Free-list reuse
// ===========================================================================

TEST(TTDPageStore_Test, FreeListReuse_InternAfterRelease_ReusesSlot)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x99);

    uint32_t idx1 = store.Intern(page.data());
    EXPECT_EQ(store.GetCapacity(), 1u);

    store.Release(idx1);
    EXPECT_EQ(store.GetCapacity(), 1u);  // Capacity doesn't shrink
    EXPECT_EQ(store.GetFreeSlotCount(), 1u);

    // Next Intern should reuse slot 0, not grow capacity
    FillPage(page, 0x88);
    uint32_t idx2 = store.Intern(page.data());

    EXPECT_EQ(idx2, idx1);  // Same slot reused
    EXPECT_EQ(store.GetCapacity(), 1u);  // No growth
    EXPECT_EQ(store.GetFreeSlotCount(), 0u);
}

TEST(TTDPageStore_Test, FreeListReuse_ReusedSlotHasCorrectData)
{
    TTDPageStore store;
    std::vector<uint8_t> page1, page2;
    FillPage(page1, 0xAA);
    FillPage(page2, 0xBB);

    uint32_t idx1 = store.Intern(page1.data());
    store.Release(idx1);

    uint32_t idx2 = store.Intern(page2.data());
    ASSERT_EQ(idx1, idx2);

    const uint8_t* stored = store.GetPage(idx2);
    EXPECT_TRUE(PageEquals(stored, page2));  // New data, not old
}

// ===========================================================================
// COW sharing simulation
// ===========================================================================

TEST(TTDPageStore_Test, COWSharing_TwoCheckpointsShareCleanPage)
{
    // Simulate the capture pattern from TDD §6.3:
    // Frame 1: page 0 is dirty → Intern
    // Frame 2: page 0 is clean → AddRef (share the same slot)
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x44);

    // Frame 1 capture
    uint32_t frame1Ref = store.Intern(page.data());
    EXPECT_EQ(store.GetRefCount(frame1Ref), 1u);

    // Frame 2 capture — page 0 clean, share via AddRef
    store.AddRef(frame1Ref);
    uint32_t frame2Ref = frame1Ref;  // Same storeIndex
    EXPECT_EQ(store.GetRefCount(frame2Ref), 2u);

    // Both checkpoints see the same data through the same slot
    const uint8_t* p1 = store.GetPage(frame1Ref);
    const uint8_t* p2 = store.GetPage(frame2Ref);
    EXPECT_EQ(p1, p2);  // Same pointer — that's the point of COW

    // Dropping frame 1 doesn't free the page (frame 2 still holds a ref)
    store.Release(frame1Ref);
    EXPECT_EQ(store.GetRefCount(frame2Ref), 1u);
    EXPECT_EQ(store.GetUsedSlots(), 1u);  // Still in use
}

TEST(TTDPageStore_Test, COWSharing_OnlyUniquePagesFreed_WhenCheckpointDropped)
{
    // Three frames, each dirtying a different page + one shared clean page:
    //   Frame 1: Intern pageA, Intern pageShared
    //   Frame 2: Intern pageB, AddRef pageShared
    //   Frame 3: Intern pageC, AddRef pageShared
    //
    // Dropping frame 1 frees pageA only (pageShared still ref'd by 2+3).
    TTDPageStore store;

    std::vector<uint8_t> pageA, pageB, pageC, pageShared;
    FillPage(pageA, 0x01);
    FillPage(pageB, 0x02);
    FillPage(pageC, 0x03);
    FillPage(pageShared, 0xFF);

    uint32_t shared = store.Intern(pageShared.data());  // ref=1

    uint32_t f1_a = store.Intern(pageA.data());
    store.AddRef(shared);  // ref=2

    uint32_t f2_b = store.Intern(pageB.data());
    store.AddRef(shared);  // ref=3

    uint32_t f3_c = store.Intern(pageC.data());

    EXPECT_EQ(store.GetUsedSlots(), 4u);

    // Drop frame 1
    store.Release(f1_a);   // pageA freed
    store.Release(shared); // shared: 3→2

    EXPECT_EQ(store.GetRefCount(shared), 2u);
    EXPECT_EQ(store.GetUsedSlots(), 3u);  // shared + B + C
    EXPECT_EQ(store.GetFreeSlotCount(), 1u);
}

// ===========================================================================
// Memory tracking
// ===========================================================================

TEST(TTDPageStore_Test, UsedBytes_LessThanCapacityBytes_AfterRelease)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x00);

    store.Intern(page.data());
    store.Intern(page.data());
    store.Intern(page.data());

    EXPECT_EQ(store.GetUsedBytes(), store.GetCapacityBytes());

    // Release one slot — usedBytes drops, capacityBytes stays
    store.Release(0u);

    EXPECT_EQ(store.GetUsedBytes(), 2u * TTDPageStore::kPageSize);
    EXPECT_EQ(store.GetCapacityBytes(), 3u * TTDPageStore::kPageSize);
    EXPECT_GT(store.GetCapacityBytes(), store.GetUsedBytes());
}

// ===========================================================================
// Reset
// ===========================================================================

TEST(TTDPageStore_Test, Reset_ClearsAllStorage)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0xEE);

    for (int i = 0; i < 10; ++i)
        store.Intern(page.data());

    EXPECT_GT(store.GetCapacity(), 0u);

    store.Reset();

    EXPECT_EQ(store.GetCapacity(), 0u);
    EXPECT_EQ(store.GetUsedSlots(), 0u);
    EXPECT_EQ(store.GetFreeSlotCount(), 0u);
    EXPECT_EQ(store.GetCapacityBytes(), 0u);
    EXPECT_EQ(store.GetUsedBytes(), 0u);
}

TEST(TTDPageStore_Test, InternAfterReset_StartsFromZero)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0xDD);

    store.Intern(page.data());
    store.Intern(page.data());
    store.Reset();

    // First Intern after Reset gets index 0 again
    uint32_t idx = store.Intern(page.data());
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(store.GetCapacity(), 1u);
}

// ===========================================================================
// Stress / patterns
// ===========================================================================

TEST(TTDPageStore_Test, Stress_InternManyPages_AllDataIntact)
{
    TTDPageStore store;
    constexpr int N = 100;
    std::vector<std::vector<uint8_t>> pages(N);
    std::vector<uint32_t> indices(N);

    for (int i = 0; i < N; ++i)
    {
        FillPageIncremental(pages[i], static_cast<uint8_t>(i));
        indices[i] = store.Intern(pages[i].data());
    }

    EXPECT_EQ(store.GetCapacity(), static_cast<uint32_t>(N));
    EXPECT_EQ(store.GetUsedSlots(), static_cast<uint32_t>(N));

    // Verify all pages
    for (int i = 0; i < N; ++i)
    {
        const uint8_t* stored = store.GetPage(indices[i]);
        EXPECT_TRUE(PageEquals(stored, pages[i]))
            << "Page " << i << " data mismatch";
    }
}

TEST(TTDPageStore_Test, Stress_AlternatingInternRelease_NoCapacityBloat)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x00);

    // Intern + Release in a loop — capacity should stabilize at 1
    for (int i = 0; i < 50; ++i)
    {
        uint32_t idx = store.Intern(page.data());
        store.Release(idx);
    }

    EXPECT_EQ(store.GetCapacity(), 1u);   // Never needed more than 1 slot
    EXPECT_EQ(store.GetUsedSlots(), 0u);  // All released
    EXPECT_EQ(store.GetFreeSlotCount(), 1u);
}

TEST(TTDPageStore_Test, FullCycle_InternAll_ReleaseAll_InternAgain)
{
    TTDPageStore store;
    std::vector<uint8_t> page;
    FillPage(page, 0x11);

    constexpr int N = 20;
    std::vector<uint32_t> indices(N);

    // Phase 1: Intern N pages
    for (int i = 0; i < N; ++i)
        indices[i] = store.Intern(page.data());
    EXPECT_EQ(store.GetCapacity(), static_cast<uint32_t>(N));

    // Phase 2: Release all
    for (int i = 0; i < N; ++i)
        store.Release(indices[i]);
    EXPECT_EQ(store.GetUsedSlots(), 0u);
    EXPECT_EQ(store.GetFreeSlotCount(), static_cast<uint32_t>(N));

    // Phase 3: Intern N pages again — should reuse all freed slots, no growth
    for (int i = 0; i < N; ++i)
        indices[i] = store.Intern(page.data());
    EXPECT_EQ(store.GetCapacity(), static_cast<uint32_t>(N));  // No bloat
    EXPECT_EQ(store.GetUsedSlots(), static_cast<uint32_t>(N));
}

// ===========================================================================
// Page size constant
// ===========================================================================

TEST(TTDPageStore_Test, PageSize_Is16384Bytes)
{
    EXPECT_EQ(TTDPageStore::kPageSize, 16384u);
}
