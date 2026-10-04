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
