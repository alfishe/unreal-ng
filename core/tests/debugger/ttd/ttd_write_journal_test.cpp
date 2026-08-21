/// @file ttd_write_journal_test.cpp
/// @brief Unit tests for TTDWriteJournal (ring buffer, scan, lifecycle, serialization).
///
/// Per parent TDD §9.3. Tests the write journal in isolation — no emulator,
/// no TimeTravelManager. The journal is a pure data structure with a well-
/// defined thread model (single-producer append, control-thread read under
/// pause), so we can exercise it without the full emulator harness.

#include <gtest/gtest.h>

#include <sstream>
#include <vector>

#include "debugger/ttd/ttd_write_journal.h"

using namespace ttd;

namespace
{
/// Helper: build a TTDWriteRecord with sensible defaults.
TTDWriteRecord MakeRec(uint64_t globalT, uint16_t addr, uint8_t value,
                       uint16_t m1pc = 0x0000, uint8_t physPage = 0,
                       bool isIo = false)
{
    TTDWriteRecord r{};
    r.globalT  = globalT;
    r.addr     = addr;
    r.isIo     = isIo ? 1 : 0;
    r.m1pc     = m1pc;
    r.value    = value;
    r.physPage = physPage;
    return r;
}

/// Predicate that matches any record at a given address.
auto AddrPred(uint16_t addr)
{
    return [addr](const TTDWriteRecord& r) { return r.addr == addr; };
}
} // anonymous namespace

// ===========================================================================
// Basic append / query
// ===========================================================================

TEST(TTDWriteJournal_Test, EmptyJournal_FindLast_ReturnsNullopt)
{
    TTDWriteJournal j(/*ringBytes=*/1024);
    EXPECT_TRUE(j.IsEmpty());
    EXPECT_EQ(j.Size(), 0u);

    auto r = j.FindLast(1000, AddrPred(0x1000));
    EXPECT_FALSE(r.has_value());

    EXPECT_EQ(j.OldestGlobalT(), 0u);
    EXPECT_EQ(j.NewestGlobalT(), 0u);
}

TEST(TTDWriteJournal_Test, AppendOne_FindLast_Matches)
{
    TTDWriteJournal j;
    auto rec = MakeRec(100, 0x1000, 0xAB, 0x2000, 5);
    j.Append(rec);

    ASSERT_EQ(j.Size(), 1u);
    EXPECT_FALSE(j.IsEmpty());

    auto found = j.FindLast(UINT64_MAX, AddrPred(0x1000));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->addr, 0x1000u);
    EXPECT_EQ(found->value, 0xABu);
    EXPECT_EQ(found->m1pc, 0x2000u);
    EXPECT_EQ(found->physPage, 5u);
    EXPECT_EQ(found->globalT, 100u);
}

TEST(TTDWriteJournal_Test, AppendMultiple_FindLast_ReturnsNewest)
{
    TTDWriteJournal j;
    j.Append(MakeRec(100, 0x1000, 0x01));
    j.Append(MakeRec(200, 0x2000, 0x02));  // different addr
    j.Append(MakeRec(300, 0x1000, 0x03));  // same addr as first
    j.Append(MakeRec(400, 0x3000, 0x04));  // different addr

    EXPECT_EQ(j.Size(), 4u);

    // Query: newest write to 0x1000 — should be the one at globalT=300
    auto found = j.FindLast(UINT64_MAX, AddrPred(0x1000));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->value, 0x03u);
    EXPECT_EQ(found->globalT, 300u);
}

TEST(TTDWriteJournal_Test, FindLast_BeforeConstraint)
{
    TTDWriteJournal j;
    j.Append(MakeRec(100, 0x1000, 0x01));
    j.Append(MakeRec(300, 0x1000, 0x03));

    // beforeT=250 → should match globalT=100, not 300
    auto found = j.FindLast(250, AddrPred(0x1000));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->globalT, 100u);
    EXPECT_EQ(found->value, 0x01u);

    // beforeT=50 → before both → no match
    auto none = j.FindLast(50, AddrPred(0x1000));
    EXPECT_FALSE(none.has_value());
}

TEST(TTDWriteJournal_Test, FindLast_NoMatchingPredicate)
{
    TTDWriteJournal j;
    j.Append(MakeRec(100, 0x1000, 0x01));
    j.Append(MakeRec(200, 0x2000, 0x02));

    auto none = j.FindLast(UINT64_MAX, AddrPred(0xFFFF));
    EXPECT_FALSE(none.has_value());
}

// ===========================================================================
// Ring wrap behavior
// ===========================================================================

TEST(TTDWriteJournal_Test, RingWrap_OldestRecordsOverwritten)
{
    // Use a tiny ring: 1024 bytes = 85 records, rounds up to 128 capacity.
    TTDWriteJournal j(1024);

    // Fill past capacity — older records should be dropped.
    for (int i = 0; i < 200; ++i)
        j.Append(MakeRec(static_cast<uint64_t>(i * 10), 0x8000, static_cast<uint8_t>(i)));

    // Size should be capped at capacity.
    EXPECT_LE(j.Size(), j.Capacity());

    // Oldest globalT should be from the most recently-surviving record,
    // not from the first append.
    EXPECT_GT(j.OldestGlobalT(), 0u);
    EXPECT_GE(j.NewestGlobalT(), j.OldestGlobalT());

    // FindLast should still work — the newest record at 0x8000
    auto found = j.FindLast(UINT64_MAX, AddrPred(0x8000));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->globalT, 1990u);  // last appended (i=199 → globalT=1990)
}

TEST(TTDWriteJournal_Test, RingWrap_OldestGlobalTAdvances)
{
    // Very small ring for deterministic wrap testing.
    TTDWriteJournal j(12 * 64);  // 768 bytes = 64 records minimum capacity

    // Append exactly capacity records.
    const size_t cap = j.Capacity();
    for (size_t i = 0; i < cap; ++i)
        j.Append(MakeRec(static_cast<uint64_t>(i), 0x1000, static_cast<uint8_t>(i)));

    EXPECT_EQ(j.Size(), cap);
    EXPECT_EQ(j.OldestGlobalT(), 0u);  // first record

    // Append one more → wraps, oldest advances.
    j.Append(MakeRec(static_cast<uint64_t>(cap), 0x1000, 0xFF));
    EXPECT_EQ(j.Size(), cap);
    EXPECT_EQ(j.OldestGlobalT(), 1u);  // first record dropped
}

// ===========================================================================
// DropAfter
// ===========================================================================

TEST(TTDWriteJournal_Test, DropAfter_RemovesRecordsAfterThreshold)
{
    TTDWriteJournal j;
    j.Append(MakeRec(100, 0x1000, 0x01));
    j.Append(MakeRec(200, 0x1000, 0x02));
    j.Append(MakeRec(300, 0x1000, 0x03));
    j.Append(MakeRec(400, 0x1000, 0x04));

    // Drop records with globalT > 250
    j.DropAfter(250);

    EXPECT_EQ(j.Size(), 2u);

    auto found = j.FindLast(UINT64_MAX, AddrPred(0x1000));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->globalT, 200u);
    EXPECT_EQ(found->value, 0x02u);
}

TEST(TTDWriteJournal_Test, DropAfter_KeepsRecordAtExactThreshold)
{
    TTDWriteJournal j;
    j.Append(MakeRec(100, 0x1000, 0x01));
    j.Append(MakeRec(200, 0x1000, 0x02));
    j.Append(MakeRec(300, 0x1000, 0x03));

    // Drop > 200 → record at 200 is kept
    j.DropAfter(200);

    EXPECT_EQ(j.Size(), 2u);
    auto found = j.FindLast(UINT64_MAX, AddrPred(0x1000));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->globalT, 200u);
}

TEST(TTDWriteJournal_Test, DropAfter_AllRecords_EmptyJournal)
{
    TTDWriteJournal j;
    j.Append(MakeRec(100, 0x1000, 0x01));
    j.Append(MakeRec(200, 0x1000, 0x02));

    j.DropAfter(0);  // Drop everything

    EXPECT_TRUE(j.IsEmpty());
}

TEST(TTDWriteJournal_Test, DropAfter_DropBeyondNewest_NoOp)
{
    TTDWriteJournal j;
    j.Append(MakeRec(100, 0x1000, 0x01));
    j.Append(MakeRec(200, 0x1000, 0x02));

    j.DropAfter(10000);  // Beyond all records — no-op

    EXPECT_EQ(j.Size(), 2u);
}

// ===========================================================================
// Clear
// ===========================================================================

TEST(TTDWriteJournal_Test, Clear_RemovesAllRecords)
{
    TTDWriteJournal j;
    j.Append(MakeRec(100, 0x1000, 0x01));
    j.Append(MakeRec(200, 0x2000, 0x02));
    ASSERT_FALSE(j.IsEmpty());

    j.Clear();

    EXPECT_TRUE(j.IsEmpty());
    EXPECT_EQ(j.Size(), 0u);
    EXPECT_EQ(j.OldestGlobalT(), 0u);
    EXPECT_EQ(j.NewestGlobalT(), 0u);
    EXPECT_EQ(j.SeqHead(), 0u);
    EXPECT_EQ(j.SeqTail(), 0u);
}

TEST(TTDWriteJournal_Test, Clear_CapacityPreserved)
{
    TTDWriteJournal j;
    const size_t cap = j.Capacity();
    ASSERT_GT(cap, 0u);

    j.Append(MakeRec(100, 0x1000, 0x01));
    j.Clear();

    EXPECT_EQ(j.Capacity(), cap);

    // Can still append after clear
    j.Append(MakeRec(200, 0x2000, 0x02));
    EXPECT_EQ(j.Size(), 1u);
    auto found = j.FindLast(UINT64_MAX, AddrPred(0x2000));
    ASSERT_TRUE(found.has_value());
}

// ===========================================================================
// IO records
// ===========================================================================

TEST(TTDWriteJournal_Test, IoRecord_StoresAndQueries)
{
    TTDWriteJournal j;
    j.Append(MakeRec(100, 0x1000, 0x01, 0x2000, 0, /*isIo=*/false));
    j.Append(MakeRec(200, 0xFE, 0x02, 0x2000, 0, /*isIo=*/true));

    // Query IO record by port address
    auto ioPred = [](const TTDWriteRecord& r) { return r.isIo; };
    auto found = j.FindLast(UINT64_MAX, ioPred);
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->isIo);
    EXPECT_EQ(found->addr, 0xFEu);
    EXPECT_EQ(found->value, 0x02u);
}

// ===========================================================================
// Serialize / Deserialize round-trip
// ===========================================================================

TEST(TTDWriteJournal_Test, SerializeDeserialize_RoundTrip)
{
    TTDWriteJournal src;
    src.Append(MakeRec(100, 0x1000, 0x01, 0x2000, 1, false));
    src.Append(MakeRec(200, 0x2000, 0x02, 0x2100, 2, false));
    src.Append(MakeRec(300, 0xFE, 0x03, 0x2200, 0, true));

    std::ostringstream oss(std::ios::binary);
    ASSERT_TRUE(src.Serialize(oss));

    std::string data = oss.str();
    ASSERT_FALSE(data.empty());

    std::istringstream iss(data, std::ios::binary);
    // Serialize writes a uint64_t count header; Deserialize expects the
    // caller to have already consumed it and pass the count explicitly.
    uint64_t count = 0;
    iss.read(reinterpret_cast<char*>(&count), sizeof(count));
    ASSERT_EQ(count, 3u);
    TTDWriteJournal dst;
    ASSERT_TRUE(dst.Deserialize(iss, count));

    EXPECT_EQ(dst.Size(), 3u);

    // Verify each record
    auto allPred = [](const TTDWriteRecord&) { return true; };

    // Newest record (globalT=300, IO)
    auto found = dst.FindLast(UINT64_MAX, allPred);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->globalT, 300u);
    EXPECT_TRUE(found->isIo);
    EXPECT_EQ(found->addr, 0xFEu);

    // Second-newest (globalT=200)
    auto found2 = dst.FindLast(250, allPred);
    ASSERT_TRUE(found2.has_value());
    EXPECT_EQ(found2->globalT, 200u);
    EXPECT_FALSE(found2->isIo);

    // Oldest (globalT=100)
    auto found3 = dst.FindLast(150, allPred);
    ASSERT_TRUE(found3.has_value());
    EXPECT_EQ(found3->globalT, 100u);
    EXPECT_EQ(found3->physPage, 1u);
}

TEST(TTDWriteJournal_Test, SerializeDeserialize_EmptyJournal)
{
    TTDWriteJournal src;

    std::ostringstream oss(std::ios::binary);
    ASSERT_TRUE(src.Serialize(oss));

    std::istringstream iss(oss.str(), std::ios::binary);
    uint64_t count = 0;
    iss.read(reinterpret_cast<char*>(&count), sizeof(count));
    ASSERT_EQ(count, 0u);
    TTDWriteJournal dst;
    ASSERT_TRUE(dst.Deserialize(iss, count));

    EXPECT_TRUE(dst.IsEmpty());
}

TEST(TTDWriteJournal_Test, Deserialize_ReplacesExistingContents)
{
    TTDWriteJournal j;
    j.Append(MakeRec(100, 0x1000, 0x01));
    j.Append(MakeRec(200, 0x2000, 0x02));
    ASSERT_EQ(j.Size(), 2u);

    // Serialize 1 record from a fresh journal
    TTDWriteJournal src;
    src.Append(MakeRec(500, 0x5000, 0x05));

    std::ostringstream oss(std::ios::binary);
    ASSERT_TRUE(src.Serialize(oss));

    std::istringstream iss(oss.str(), std::ios::binary);
    uint64_t count = 0;
    iss.read(reinterpret_cast<char*>(&count), sizeof(count));
    ASSERT_EQ(count, 1u);
    ASSERT_TRUE(j.Deserialize(iss, count));

    EXPECT_EQ(j.Size(), 1u);
    auto found = j.FindLast(UINT64_MAX, [](const TTDWriteRecord&) { return true; });
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->addr, 0x5000u);
}

// ===========================================================================
// Async allocation
// ===========================================================================

TEST(TTDWriteJournal_Test, AsyncAlloc_IsReadyAfterWait)
{
    // Small buffer for fast test
    TTDWriteJournal j(/*ringBytes=*/1024 * 1024, /*asyncAlloc=*/true);

    // Initially might not be ready (race, but likely not ready)
    // WaitReady should block until allocation completes
    j.WaitReady();

    EXPECT_TRUE(j.IsReady());
    EXPECT_GT(j.Capacity(), 0u);
}

TEST(TTDWriteJournal_Test, AsyncAlloc_AppendAfterWait)
{
    TTDWriteJournal j(/*ringBytes=*/1024 * 1024, /*asyncAlloc=*/true);
    j.WaitReady();

    j.Append(MakeRec(100, 0x4000, 0x42));
    EXPECT_EQ(j.Size(), 1u);

    auto found = j.FindLast(UINT64_MAX, AddrPred(0x4000));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->value, 0x42u);
}

TEST(TTDWriteJournal_Test, SyncAlloc_IsReadyImmediately)
{
    TTDWriteJournal j(/*ringBytes=*/1024, /*asyncAlloc=*/false);
    EXPECT_TRUE(j.IsReady());
}

// ---------------------------------------------------------------------------
// Block-compressed serialization
// ---------------------------------------------------------------------------
//
// The journal section is stored as zstd-compressed columnar blocks. It is by
// far the largest thing in a .ttd - 89% of the file on a real demo recording -
// so the compression matters, but a lossy or reordering round trip would
// corrupt every reverse query built on it. These check the round trip is
// exact, field by field, including the cases the column layout is most likely
// to get wrong: I/O records (a bitfield packed 8 to a byte), block boundaries,
// and counts that do not divide evenly into blocks.

namespace
{

ttd::TTDWriteRecord MakeRecord(uint64_t globalT, uint16_t addr, bool isIo,
                               uint16_t m1pc, uint8_t value, uint8_t physPage)
{
    ttd::TTDWriteRecord r{};
    r.globalT = globalT & ((uint64_t{1} << 40) - 1);
    r.addr = addr;
    r.isIo = isIo ? 1 : 0;
    r.pad = 0;
    r.m1pc = m1pc;
    r.value = value;
    r.physPage = physPage;
    return r;
}

bool SameRecord(const ttd::TTDWriteRecord& a, const ttd::TTDWriteRecord& b)
{
    return a.globalT == b.globalT && a.addr == b.addr && a.isIo == b.isIo &&
           a.m1pc == b.m1pc && a.value == b.value && a.physPage == b.physPage;
}

/// Round-trip `count` synthetic records and assert every field survives.
void ExpectExactRoundTrip(size_t count)
{
    ttd::TTDWriteJournal src(64u * 1024 * 1024, /*asyncAlloc=*/false);

    std::vector<ttd::TTDWriteRecord> expected;
    expected.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        // Deliberately varied: monotonic but irregular time steps, addresses
        // that wander, every 7th record an I/O write.
        auto r = MakeRecord(1000 + i * 37 + (i % 13),
                            static_cast<uint16_t>(0x4000 + (i * 251) % 0xC000),
                            (i % 7) == 0,
                            static_cast<uint16_t>(0x8000 + (i % 997)),
                            static_cast<uint8_t>(i * 31),
                            static_cast<uint8_t>(i % 8));
        src.Append(r);
        expected.push_back(r);
    }
    ASSERT_EQ(src.Size(), count);

    std::ostringstream out(std::ios::binary);
    ASSERT_TRUE(src.Serialize(out)) << "serialize failed for " << count << " records";

    const std::string blob = out.str();
    std::istringstream in(blob, std::ios::binary);

    uint64_t storedCount = 0;
    in.read(reinterpret_cast<char*>(&storedCount), sizeof(storedCount));
    ASSERT_TRUE(in.good());
    ASSERT_EQ(storedCount, count) << "record count header is wrong";

    ttd::TTDWriteJournal dst(64u * 1024 * 1024, /*asyncAlloc=*/false);
    ASSERT_TRUE(dst.Deserialize(in, storedCount))
        << "deserialize failed for " << count << " records";
    ASSERT_EQ(dst.Size(), count);

    size_t mismatches = 0;
    size_t firstMismatch = 0;
    for (size_t i = 0; i < count; ++i)
    {
        auto got = dst.FindLast(UINT64_MAX, [&](const ttd::TTDWriteRecord&) { return true; });
        (void)got;  // FindLast walks backwards; compare through the ring below.
        break;
    }

    const auto& ring = dst.Ring();
    for (size_t i = 0; i < count; ++i)
    {
        // Records were appended oldest-first into a freshly cleared journal,
        // so ring index i is expected[i] as long as we stayed under capacity.
        if (!SameRecord(ring[i], expected[i]))
        {
            if (mismatches == 0)
                firstMismatch = i;
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0u)
        << mismatches << " of " << count << " records differ after the round trip; "
        << "first at index " << firstMismatch;
}

}  // namespace

/// Exactly one full block, plus the awkward counts around it.
TEST(TTDWriteJournal_Compression_Test, RoundTripIsExactAcrossBlockBoundaries)
{
    ExpectExactRoundTrip(1);
    ExpectExactRoundTrip(7);       // fewer records than the io bitfield's 8
    ExpectExactRoundTrip(2047);    // one short of a block
    ExpectExactRoundTrip(2048);    // exactly one block
    ExpectExactRoundTrip(2049);    // one past a block
    ExpectExactRoundTrip(5000);    // several blocks, ragged tail
}

/// An empty journal must still produce a readable section.
TEST(TTDWriteJournal_Compression_Test, EmptyJournalRoundTrips)
{
    ttd::TTDWriteJournal src(1u * 1024 * 1024, false);
    ASSERT_TRUE(src.IsEmpty());

    std::ostringstream out(std::ios::binary);
    ASSERT_TRUE(src.Serialize(out));

    std::istringstream in(out.str(), std::ios::binary);
    uint64_t storedCount = 0;
    in.read(reinterpret_cast<char*>(&storedCount), sizeof(storedCount));
    EXPECT_EQ(storedCount, 0u);

    ttd::TTDWriteJournal dst(1u * 1024 * 1024, false);
    EXPECT_TRUE(dst.Deserialize(in, storedCount));
    EXPECT_TRUE(dst.IsEmpty());
}

/// The whole point: the section has to be substantially smaller than the
/// 12-bytes-per-record it replaced.
TEST(TTDWriteJournal_Compression_Test, SectionIsMuchSmallerThanRawRecords)
{
    constexpr size_t kCount = 20000;
    ttd::TTDWriteJournal src(64u * 1024 * 1024, false);

    // Shaped like a real workload: a handful of hot addresses written over and
    // over from a small set of instructions, time advancing steadily.
    for (size_t i = 0; i < kCount; ++i)
    {
        src.Append(MakeRecord(5000 + i * 29,
                              static_cast<uint16_t>(0x5800 + (i % 64)),
                              false,
                              static_cast<uint16_t>(0x8000 + (i % 16)),
                              static_cast<uint8_t>(i % 256),
                              5));
    }

    std::ostringstream out(std::ios::binary);
    ASSERT_TRUE(src.Serialize(out));

    const size_t raw = kCount * sizeof(ttd::TTDWriteRecord);
    const size_t stored = out.str().size();
    const double ratio = double(raw) / double(stored);

    EXPECT_GT(ratio, 5.0)
        << "journal section compressed only " << ratio << "x ("
        << raw << " -> " << stored << " bytes); the columnar layout is not working";
}
