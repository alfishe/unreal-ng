/// @file ttdarena_test.cpp
/// @brief The engine's arena: exact-size payloads in shared chunks, a chunk
/// returned when its last payload goes.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "debugger/ttd/engine/ttdarena.h"

using namespace ttd;

TEST(TTDArena_Test, PayloadsShareAChunk_AndComeBackIntact)
{
    TTDArena arena;
    std::vector<TTDArenaRef> refs;
    for (uint32_t i = 1; i <= 100; ++i)
    {
        std::vector<uint8_t> b(i, static_cast<uint8_t>(i));
        refs.push_back(arena.Store(b.data(), i));
    }
    EXPECT_EQ(arena.ChunkCount(), 1u);
    EXPECT_EQ(arena.LiveBytes(), 5050u);
    for (uint32_t i = 1; i <= 100; ++i)
    {
        const TTDArenaRef& r = refs[i - 1];
        ASSERT_EQ(r.size, i);
        for (uint32_t k = 0; k < i; ++k)
            ASSERT_EQ(arena.Data(r)[k], static_cast<uint8_t>(i));
    }
}

TEST(TTDArena_Test, AChunkIsReturnedWhenItsLastPayloadGoes)
{
    TTDArena arena;
    // Fill two chunks: the first is no longer the one being filled
    std::vector<uint8_t> big(TTDArena::kChunkBytes / 2 + 1, 7);
    const TTDArenaRef a = arena.Store(big.data(), static_cast<uint32_t>(big.size()));
    const TTDArenaRef b = arena.Store(big.data(), static_cast<uint32_t>(big.size()));
    ASSERT_NE(a.chunk, b.chunk);
    ASSERT_EQ(arena.ChunkCount(), 2u);
    const size_t heapWithTwo = arena.HeapBytes();

    arena.Release(a);
    EXPECT_EQ(arena.ChunkCount(), 1u) << "the emptied chunk is returned";
    EXPECT_LT(arena.HeapBytes(), heapWithTwo);
    EXPECT_EQ(arena.LiveBytes(), big.size());
}

TEST(TTDArena_Test, PayloadLargerThanAChunkGetsItsOwn)
{
    TTDArena arena;
    std::vector<uint8_t> huge(TTDArena::kChunkBytes + 10, 3);
    const TTDArenaRef r = arena.Store(huge.data(), static_cast<uint32_t>(huge.size()));
    EXPECT_EQ(r.size, huge.size());
    EXPECT_EQ(std::memcmp(arena.Data(r), huge.data(), huge.size()), 0);
    std::vector<uint8_t> small(10, 4);
    const TTDArenaRef s = arena.Store(small.data(), 10);
    EXPECT_NE(s.chunk, r.chunk) << "the oversized chunk is not filled further";
}
