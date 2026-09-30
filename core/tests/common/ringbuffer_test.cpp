// RingBuffer (common/ringbuffer.h): FIFO eviction and in-place reset. The
// port trace recorder re-sizes its buffer in place (never replaces it) because
// the emulator thread pushes while automation threads read

#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <vector>

#include "common/ringbuffer.h"

namespace
{
    struct Item
    {
        uint64_t timestamp = 0;
    };
}  // namespace

TEST(RingBuffer_Test, EvictsOldestWhenFull)
{
    RingBuffer<Item> ring(3);
    for (uint64_t t = 1; t <= 5; t++)
        ring.push(Item{t});
    const std::vector<Item> all = ring.getAll();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].timestamp, 3u);
    EXPECT_EQ(all[2].timestamp, 5u);
    EXPECT_EQ(ring.totalEventsProduced(), 5u);
    EXPECT_EQ(ring.totalEventsEvicted(), 2u);
}

TEST(RingBuffer_Test, ResetResizesInPlaceAndRestartsCounters)
{
    RingBuffer<Item> ring(3);
    for (uint64_t t = 1; t <= 5; t++)
        ring.push(Item{t});

    ring.reset(0);  // storage released: pushes are dropped, nothing breaks
    EXPECT_EQ(ring.capacity(), 0u);
    ring.push(Item{9});
    EXPECT_EQ(ring.size(), 0u);
    EXPECT_TRUE(ring.getAll().empty());

    ring.reset(4);
    EXPECT_EQ(ring.capacity(), 4u);
    EXPECT_EQ(ring.totalEventsProduced(), 0u) << "counters restart with the reset";
    ring.push(Item{10});
    ring.push(Item{11});
    const std::vector<Item> all = ring.getAll();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].timestamp, 10u);
}
