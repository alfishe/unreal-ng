/// @file ttd_dirty_tracker_test.cpp
/// @brief Tests for TTDDirtyTracker (P1 Item 2).
///
/// Verifies:
///   - MarkDirty sets the per-frame bit (IsDirty)
///   - MarkDirty sets the session bit (WasEverDirty)
///   - CollectAndClear returns dirty pages in ascending order and clears _dirty
///   - CollectAndClear does NOT clear _everDirty
///   - ResetSession clears both bitmaps
///   - ClearFrameDirty clears only the per-frame bitmap
///   - Edge cases: page 0, page MAX_RAM_PAGES-1, out-of-range handled by caller
///   - Idempotency: marking the same page dirty multiple times is a no-op
///   - Empty CollectAndClear on a fresh tracker
///
/// Per parent TDD §6.2 and §6.3.

#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

#include "debugger/ttd/ttd_dirty_tracker.h"

using namespace ttd;

namespace {

/// Smallest meaningful model size for tests is the 128K Spectrum (8 pages).
/// The tracker is sized for MAX_RAM_PAGES (256), but tests focus on the low
/// page indices typical of real software.
constexpr uint16_t kPentagon128RamPages = 8;
constexpr uint16_t kTSEvoMaxRamPages = MAX_RAM_PAGES;

} // namespace

// ---------------------------------------------------------------------------
// MarkDirty / IsDirty / WasEverDirty basics
// ---------------------------------------------------------------------------

TEST(TTDDirtyTracker_Test, FreshTracker_HasNoDirtyPages)
{
    TTDDirtyTracker t;
    EXPECT_EQ(t.DirtyCount(), 0u);
    EXPECT_EQ(t.EverDirtyCount(), 0u);
    for (uint16_t p = 0; p < kPentagon128RamPages; ++p)
    {
        EXPECT_FALSE(t.IsDirty(p)) << "page " << p;
        EXPECT_FALSE(t.WasEverDirty(p)) << "page " << p;
    }
}

TEST(TTDDirtyTracker_Test, MarkDirty_SetsPerFrameBit)
{
    TTDDirtyTracker t;
    t.MarkDirty(3);
    EXPECT_TRUE(t.IsDirty(3));
    EXPECT_EQ(t.DirtyCount(), 1u);
}

TEST(TTDDirtyTracker_Test, MarkDirty_SetsSessionBit)
{
    TTDDirtyTracker t;
    t.MarkDirty(3);
    EXPECT_TRUE(t.WasEverDirty(3));
    EXPECT_EQ(t.EverDirtyCount(), 1u);
}

TEST(TTDDirtyTracker_Test, MarkDirty_Page0)
{
    TTDDirtyTracker t;
    t.MarkDirty(0);
    EXPECT_TRUE(t.IsDirty(0));
    EXPECT_TRUE(t.WasEverDirty(0));
}

TEST(TTDDirtyTracker_Test, MarkDirty_LastSupportedPage)
{
    TTDDirtyTracker t;
    const uint16_t last = static_cast<uint16_t>(MAX_RAM_PAGES - 1);
    t.MarkDirty(last);
    EXPECT_TRUE(t.IsDirty(last));
    EXPECT_TRUE(t.WasEverDirty(last));
}

TEST(TTDDirtyTracker_Test, MarkDirty_Idempotent)
{
    TTDDirtyTracker t;
    t.MarkDirty(5);
    t.MarkDirty(5);
    t.MarkDirty(5);
    EXPECT_EQ(t.DirtyCount(), 1u);
    EXPECT_EQ(t.EverDirtyCount(), 1u);
}

TEST(TTDDirtyTracker_Test, MarkDirty_MultiplePages)
{
    TTDDirtyTracker t;
    const uint16_t pages[] = {0, 1, 5, 7, 63, 64, 65, 127, 128, 200};
    for (uint16_t p : pages)
        t.MarkDirty(p);
    EXPECT_EQ(t.DirtyCount(), sizeof(pages) / sizeof(pages[0]));
    for (uint16_t p : pages)
    {
        EXPECT_TRUE(t.IsDirty(p)) << "page " << p;
        EXPECT_TRUE(t.WasEverDirty(p)) << "page " << p;
    }
}

// ---------------------------------------------------------------------------
// CollectAndClear
// ---------------------------------------------------------------------------

TEST(TTDDirtyTracker_Test, CollectAndClear_EmptyTracker_ReturnsNothing)
{
    TTDDirtyTracker t;
    std::vector<uint16_t> out;
    t.CollectAndClear(out);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(t.DirtyCount(), 0u);
}

TEST(TTDDirtyTracker_Test, CollectAndClear_ReturnsPagesInAscendingOrder)
{
    TTDDirtyTracker t;
    // Mark in non-ascending order on purpose
    t.MarkDirty(7);
    t.MarkDirty(2);
    t.MarkDirty(5);
    t.MarkDirty(0);
    t.MarkDirty(63);
    t.MarkDirty(128);

    std::vector<uint16_t> out;
    t.CollectAndClear(out);

    // Expected ascending order, regardless of insert order
    const std::vector<uint16_t> expected = {0, 2, 5, 7, 63, 128};
    EXPECT_EQ(out, expected);
    EXPECT_TRUE(std::is_sorted(out.begin(), out.end()));
}

TEST(TTDDirtyTracker_Test, CollectAndClear_ClearsPerFrameBitmap)
{
    TTDDirtyTracker t;
    t.MarkDirty(3);
    t.MarkDirty(7);

    std::vector<uint16_t> out;
    t.CollectAndClear(out);

    EXPECT_EQ(t.DirtyCount(), 0u);
    EXPECT_FALSE(t.IsDirty(3));
    EXPECT_FALSE(t.IsDirty(7));
}

TEST(TTDDirtyTracker_Test, CollectAndClear_DoesNotClearSessionBitmap)
{
    TTDDirtyTracker t;
    t.MarkDirty(3);
    t.MarkDirty(7);

    std::vector<uint16_t> out;
    t.CollectAndClear(out);

    // _everDirty must survive CollectAndClear per TDD §6.3
    EXPECT_EQ(t.EverDirtyCount(), 2u);
    EXPECT_TRUE(t.WasEverDirty(3));
    EXPECT_TRUE(t.WasEverDirty(7));
}

TEST(TTDDirtyTracker_Test, CollectAndClear_AppendsNotOverwrites)
{
    // The contract is "append to outDirtyPages", so callers can reuse a
    // single buffer across calls without re-allocating. Verify.
    TTDDirtyTracker t;
    t.MarkDirty(2);

    std::vector<uint16_t> out;
    out.push_back(999);  // sentinel pre-existing content
    t.CollectAndClear(out);

    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0], 999);
    EXPECT_EQ(out[1], 2);
}

TEST(TTDDirtyTracker_Test, CollectAndClear_SecondCallReturnsOnlyNewlyDirty)
{
    TTDDirtyTracker t;

    t.MarkDirty(1);
    t.MarkDirty(2);
    std::vector<uint16_t> first;
    t.CollectAndClear(first);
    ASSERT_EQ(first.size(), 2u);

    // Only page 3 is newly dirty since the first CollectAndClear
    t.MarkDirty(3);
    std::vector<uint16_t> second;
    t.CollectAndClear(second);

    const std::vector<uint16_t> expected = {3};
    EXPECT_EQ(second, expected);
}

// ---------------------------------------------------------------------------
// Session lifecycle: ever-touched semantics
// ---------------------------------------------------------------------------

TEST(TTDDirtyTracker_Test, EverDirty_PersistsAcrossMultipleFrames)
{
    // Models the steady-state capture path: page written in frame 0,
    // never written again. _everDirty must keep flagging it so the capture
    // path knows it shares the previous slot rather than carrying NEVER_TOUCHED.
    TTDDirtyTracker t;

    t.MarkDirty(4);
    std::vector<uint16_t> f0;
    t.CollectAndClear(f0);
    ASSERT_EQ(f0.size(), 1u);

    // Frame 1: page 4 is clean, page 5 is newly dirty
    t.MarkDirty(5);
    std::vector<uint16_t> f1;
    t.CollectAndClear(f1);
    ASSERT_EQ(f1.size(), 1u);
    EXPECT_EQ(f1[0], 5);

    // Page 4 is no longer dirty this frame, but WasEverDirty must still return true
    EXPECT_FALSE(t.IsDirty(4));
    EXPECT_TRUE(t.WasEverDirty(4)) << "everDirty must persist for the whole session";
    EXPECT_EQ(t.EverDirtyCount(), 2u);
}

// ---------------------------------------------------------------------------
// ResetSession / ClearFrameDirty
// ---------------------------------------------------------------------------

TEST(TTDDirtyTracker_Test, ResetSession_ClearsBothBitmaps)
{
    TTDDirtyTracker t;
    t.MarkDirty(3);
    t.MarkDirty(7);
    std::vector<uint16_t> out;
    t.CollectAndClear(out);  // clears _dirty but not _everDirty

    EXPECT_EQ(t.EverDirtyCount(), 2u);
    t.ResetSession();
    EXPECT_EQ(t.DirtyCount(), 0u);
    EXPECT_EQ(t.EverDirtyCount(), 0u);
    EXPECT_FALSE(t.WasEverDirty(3));
    EXPECT_FALSE(t.WasEverDirty(7));
}

TEST(TTDDirtyTracker_Test, ClearFrameDirty_ClearsOnlyPerFrameBitmap)
{
    TTDDirtyTracker t;
    t.MarkDirty(3);
    t.MarkDirty(7);

    t.ClearFrameDirty();

    EXPECT_EQ(t.DirtyCount(), 0u);
    EXPECT_EQ(t.EverDirtyCount(), 2u);  // _everDirty untouched
    EXPECT_FALSE(t.IsDirty(3));
    EXPECT_TRUE(t.WasEverDirty(3));
}

// ---------------------------------------------------------------------------
// Boundary cases
// ---------------------------------------------------------------------------

TEST(TTDDirtyTracker_Test, MarkDirty_BoundariesBetweenWords)
{
    // Word boundaries are at multiples of 64. Verify bits don't bleed across.
    TTDDirtyTracker t;
    t.MarkDirty(63);
    t.MarkDirty(64);
    t.MarkDirty(127);
    t.MarkDirty(128);

    EXPECT_EQ(t.DirtyCount(), 4u);
    EXPECT_TRUE(t.IsDirty(63));
    EXPECT_TRUE(t.IsDirty(64));
    EXPECT_TRUE(t.IsDirty(127));
    EXPECT_TRUE(t.IsDirty(128));

    // Adjacent pages should NOT be marked
    EXPECT_FALSE(t.IsDirty(62));
    EXPECT_FALSE(t.IsDirty(65));
    EXPECT_FALSE(t.IsDirty(126));
    EXPECT_FALSE(t.IsDirty(129));
}

TEST(TTDDirtyTracker_Test, MarkDirty_AllPages_TypicalMaxRamSize)
{
    // Sanity: marking every supported page works and counts correctly.
    TTDDirtyTracker t;
    for (uint16_t p = 0; p < kTSEvoMaxRamPages; ++p)
        t.MarkDirty(p);

    EXPECT_EQ(t.DirtyCount(), static_cast<size_t>(kTSEvoMaxRamPages));
    EXPECT_EQ(t.EverDirtyCount(), static_cast<size_t>(kTSEvoMaxRamPages));

    std::vector<uint16_t> out;
    t.CollectAndClear(out);
    EXPECT_EQ(out.size(), static_cast<size_t>(kTSEvoMaxRamPages));
    EXPECT_TRUE(std::is_sorted(out.begin(), out.end()));
    EXPECT_EQ(out.front(), 0);
    EXPECT_EQ(out.back(), static_cast<uint16_t>(kTSEvoMaxRamPages - 1));
}
