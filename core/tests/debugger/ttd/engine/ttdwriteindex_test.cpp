/// @file ttdwriteindex_test.cpp
/// @brief The engine's write journal (D40, J6): records in compressed blocks,
/// found in a time range like a brute-force scan finds them.

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "debugger/ttd/engine/ttdwriteindex.h"

using namespace ttd;

namespace
{
std::vector<TTDWriteRecord> MakeRecords(size_t count)
{
    std::mt19937 rng(7);
    std::vector<TTDWriteRecord> out;
    uint64_t t = 1000;
    for (size_t i = 0; i < count; ++i)
    {
        t += 1 + rng() % 40;
        TTDWriteRecord r{};
        r.globalT = t;
        // Like a program's writes: a few addresses, a few writing instructions,
        // counting values, one page mostly
        r.addr = static_cast<uint16_t>(0x4000 + (i % 64));
        r.m1pc = static_cast<uint16_t>(0x8000 + (i % 4) * 3);
        r.value = static_cast<uint8_t>(i / 64);
        r.physPage = static_cast<uint8_t>(rng() % 16 == 0 ? 2 : 0);
        out.push_back(r);
    }
    return out;
}
}  // namespace

TEST(TTDWriteIndex_Test, FindsTheNewestWriteInARangeAcrossBlocks)
{
    const std::vector<TTDWriteRecord> records = MakeRecords(3 * kWriteBlockRecords + 500);
    TTDWriteIndex index;
    for (const TTDWriteRecord& r : records)
        index.Append(r);
    ASSERT_EQ(index.Size(), records.size());

    std::mt19937 rng(11);
    const uint64_t first = records.front().globalT, last = records.back().globalT;
    for (int q = 0; q < 300; ++q)
    {
        const uint64_t a = first - 5 + rng() % (last - first + 10);
        const uint64_t b = a + rng() % 20000;
        const uint16_t addr = static_cast<uint16_t>(0x4000 + rng() % 64);
        auto pred = [addr](const TTDWriteRecord& r) { return r.addr == addr; };
        std::optional<TTDWriteRecord> want;
        for (const TTDWriteRecord& r : records)
            if (r.globalT > a && r.globalT <= b && pred(r))
                want = r;
        const std::optional<TTDWriteRecord> got = index.FindLastInRange(a, b, pred);
        ASSERT_EQ(got.has_value(), want.has_value()) << "query " << q;
        if (want)
        {
            EXPECT_EQ(uint64_t(got->globalT), uint64_t(want->globalT));
            EXPECT_EQ(got->value, want->value);
            EXPECT_EQ(got->m1pc, want->m1pc);
            EXPECT_EQ(got->physPage, want->physPage);
        }
    }

    // Every record comes back, in order; the blocks hold far less than raw records
    size_t k = 0;
    index.ForEach([&](const TTDWriteRecord& r) {
        ASSERT_LT(k, records.size());
        EXPECT_EQ(uint64_t(r.globalT), uint64_t(records[k].globalT));
        ++k;
    });
    EXPECT_EQ(k, records.size());
    // The sealed blocks hold far less than raw records; fixed on top: the open
    // block's room and one decoded block kept for queries
    EXPECT_LT(index.HeapBytes(),
              records.size() * sizeof(TTDWriteRecord) / 3 + 2 * kWriteBlockRecords * sizeof(TTDWriteRecord));

    index.SetSegments({{first - 1, last}});
    ASSERT_EQ(index.Segments().size(), 1u);
    index.Clear();
    EXPECT_EQ(index.Size(), 0u);
    EXPECT_TRUE(index.Segments().empty());
    EXPECT_FALSE(index.FindLastInRange(0, UINT64_MAX, [](const TTDWriteRecord&) { return true; }));
}

/// A history limit releases the oldest blocks: the rest still answers, and
/// the segments no longer claim the released span
TEST(TTDWriteIndex_Test, DropBeforeReleasesWholeOldBlocks)
{
    const std::vector<TTDWriteRecord> records = MakeRecords(3 * kWriteBlockRecords + 500);
    TTDWriteIndex index;
    for (const TTDWriteRecord& r : records)
        index.Append(r);
    index.SetSegments({{0, records.back().globalT}});
    const uint64_t cut = records[kWriteBlockRecords + 10].globalT;   // inside the second block
    index.DropBefore(cut);
    EXPECT_EQ(index.Size(), records.size() - kWriteBlockRecords) << "only the first block lies wholly before";
    const uint64_t firstDropped = records[kWriteBlockRecords - 1].globalT;
    ASSERT_EQ(index.Segments().size(), 1u);
    EXPECT_EQ(index.Segments()[0].from, firstDropped) << "covered only after the newest record released";
    index.SetSegments({{0, records.back().globalT}});
    EXPECT_EQ(index.Segments()[0].from, firstDropped) << "a later SetSegments stays clipped";
    const auto pred = [](const TTDWriteRecord&) { return true; };
    const auto newest = index.FindLastInRange(firstDropped, cut, pred);
    ASSERT_TRUE(newest.has_value());
    EXPECT_GT(uint64_t(newest->globalT), firstDropped);
    index.Clear();
    index.SetSegments({{0, 10}});
    EXPECT_EQ(index.Segments()[0].from, 0u) << "a new session starts unclipped";
}

/// A resume from the past cuts the index: whole blocks after the cut go, the
/// block the cut falls into keeps its records up to it, and the index then
/// answers like one that never had the rest; appending continues in order
TEST(TTDWriteIndex_Test, DropAfterKeepsWhatLiesUpToTheCut)
{
    const std::vector<TTDWriteRecord> records = MakeRecords(3 * kWriteBlockRecords + 500);
    const uint64_t cut = records[kWriteBlockRecords + 77].globalT;   // inside the second block
    TTDWriteIndex cutIndex, reference;
    for (const TTDWriteRecord& r : records)
    {
        cutIndex.Append(r);
        if (r.globalT <= cut)
            reference.Append(r);
    }
    cutIndex.SetSegments({{0, records.back().globalT}});
    cutIndex.DropAfter(cut);
    EXPECT_EQ(cutIndex.Size(), reference.Size());
    ASSERT_EQ(cutIndex.Segments().size(), 1u);
    EXPECT_EQ(cutIndex.Segments()[0].to, cut);
    // New records after the cut, the same on both
    for (size_t i = 0; i < 3000; ++i)
    {
        TTDWriteRecord r = records[i];
        r.globalT = cut + 1 + i;
        cutIndex.Append(r);
        reference.Append(r);
    }
    std::vector<uint64_t> a, b;
    cutIndex.ForEach([&](const TTDWriteRecord& r) { a.push_back(r.globalT); });
    reference.ForEach([&](const TTDWriteRecord& r) { b.push_back(r.globalT); });
    EXPECT_EQ(a, b);
    const auto pred = [](const TTDWriteRecord& r) { return r.addr == 0x4005; };
    const auto x = cutIndex.FindLastInRange(0, cut, pred);
    const auto y = reference.FindLastInRange(0, cut, pred);
    ASSERT_EQ(x.has_value(), y.has_value());
    if (x)
    {
        EXPECT_EQ(uint64_t(x->globalT), uint64_t(y->globalT));
    }
}

/// The timeline counts a range's memory writes from the blocks' running counts: the same as a scan, with port OUTs left
/// out, after a cut at either end, and at most one block decoded per count (P1.S10: a whole-journal pass per timeline
/// grew with the session)
TEST(TTDWriteIndex_Test, MemoryWritesUpToCountsLikeAScan)
{
    std::vector<TTDWriteRecord> records = MakeRecords(5 * kWriteBlockRecords + 300);
    for (size_t i = 0; i < records.size(); i += 7)
        records[i].isIo = 1;
    TTDWriteIndex index;
    for (const TTDWriteRecord& r : records)
        index.Append(r);
    std::vector<TTDWriteRecord> kept = records;
    auto check = [&](const char* step) {
        std::mt19937 rng(3);
        const uint64_t first = kept.front().globalT, last = kept.back().globalT;
        for (int q = 0; q < 200; ++q)
        {
            const uint64_t t = q == 0 ? 0 : q == 1 ? UINT64_MAX : first - 3 + rng() % (last - first + 6);
            uint64_t want = 0;
            for (const TTDWriteRecord& r : kept)
                want += r.globalT <= t && !r.isIo ? 1u : 0u;
            const uint64_t decodedBefore = index.BlocksDecoded();
            ASSERT_EQ(index.MemoryWritesUpTo(t), want) << step << " t " << t;
            EXPECT_LE(index.BlocksDecoded() - decodedBefore, 1u) << step;
        }
    };
    check("whole");
    const uint64_t front = records[kWriteBlockRecords + 5].globalT;
    index.DropBefore(front);
    kept.erase(kept.begin(), kept.begin() + kWriteBlockRecords);   // the first block, wholly before
    check("after DropBefore");
    const uint64_t back = records[3 * kWriteBlockRecords + 99].globalT;
    index.DropAfter(back);
    while (kept.back().globalT > back)
        kept.pop_back();
    check("after DropAfter");
    for (size_t i = 0; i < kWriteBlockRecords + 10; ++i)
    {
        TTDWriteRecord r = records[i];
        r.globalT = back + 1 + i;
        index.Append(r);
        kept.push_back(r);
    }
    check("appended again");
}

/// A recording folder's file takes its part's records: ForEachInRange visits exactly [from, to), in order
TEST(TTDWriteIndex_Test, ForEachInRangeVisitsThePartsRecords)
{
    const std::vector<TTDWriteRecord> records = MakeRecords(3 * kWriteBlockRecords + 500);
    TTDWriteIndex index;
    for (const TTDWriteRecord& r : records)
        index.Append(r);
    std::mt19937 rng(5);
    const uint64_t first = records.front().globalT, last = records.back().globalT;
    for (int q = 0; q < 100; ++q)
    {
        const uint64_t a = first - 5 + rng() % (last - first + 10);
        const uint64_t b = q % 10 == 0 ? UINT64_MAX : a + rng() % 60000;
        std::vector<uint64_t> want, got;
        for (const TTDWriteRecord& r : records)
            if (r.globalT >= a && r.globalT < b)
                want.push_back(r.globalT);
        index.ForEachInRange(a, b, [&](const TTDWriteRecord& r) { got.push_back(r.globalT); });
        ASSERT_EQ(got, want) << "query " << q;
    }
}
