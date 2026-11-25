/// @file ttdcompression_test.cpp
/// @brief ttd::codec::Compress allocates its result at exactly the compressed
/// size (TTD v2 POC 011 experiment E5: a result shrunk from the worst-case
/// buffer kept ~4 KB of heap per stored page).

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "debugger/ttd/ttdcompression.h"

using namespace ttd;

namespace
{
std::vector<uint8_t> Compressible(size_t size)
{
    std::vector<uint8_t> data(size, 0);
    for (size_t i = 0; i < size; i += 64)
        data[i] = static_cast<uint8_t>(i >> 6);
    return data;
}

std::vector<uint8_t> Incompressible(size_t size)
{
    std::vector<uint8_t> data(size);
    uint32_t x = 0x12345678u;
    for (uint8_t& b : data)
    {
        x = x * 1664525u + 1013904223u;
        b = static_cast<uint8_t>(x >> 24);
    }
    return data;
}

void ExpectExactAndRoundTrip(const std::vector<uint8_t>& raw)
{
    const std::vector<uint8_t> packed = codec::Compress(raw.data(), raw.size());
    ASSERT_FALSE(packed.empty());
    EXPECT_EQ(packed.capacity(), packed.size()) << "the result holds no unused allocation";

    std::vector<uint8_t> back(raw.size());
    ASSERT_TRUE(codec::Decompress(packed, raw.size(), back.data()));
    EXPECT_EQ(back, raw);
}
}  // namespace

TEST(TTDCompression_Test, CompressiblePage_ExactSizeAndRoundTrip)
{
    const std::vector<uint8_t> raw = Compressible(4096);
    ExpectExactAndRoundTrip(raw);
    EXPECT_LT(codec::Compress(raw.data(), raw.size()).size(), 512u);
}

TEST(TTDCompression_Test, IncompressiblePage_ExactSizeAndRoundTrip)
{
    ExpectExactAndRoundTrip(Incompressible(4096));
}

TEST(TTDCompression_Test, InputAboveScratchLimit_ExactSizeAndRoundTrip)
{
    // Above kCompressScratchMax the worst-case buffer is a temporary one
    ExpectExactAndRoundTrip(Compressible(codec::kCompressScratchMax * 2));
}

TEST(TTDCompression_Test, SmallAfterLarge_StillExact)
{
    // The per-thread scratch is reused: a small input after a larger one must
    // not inherit the larger buffer's size
    ExpectExactAndRoundTrip(Incompressible(codec::kCompressScratchMax));
    ExpectExactAndRoundTrip(Compressible(100));
}

TEST(TTDCompression_Test, EmptyInput_ReturnsEmpty)
{
    EXPECT_TRUE(codec::Compress(nullptr, 0).empty());
}

TEST(TTDCompression_Test, Crc32C_KnownVectorAndContinuation)
{
    // The CRC32C check value of "123456789" (RFC 3720 B.4)
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(codec::Crc32C(check, sizeof(check)), 0xE3069283u);

    // Lengths across the 8-byte step and its tail; a split run continues through the seed
    const std::vector<uint8_t> data = Incompressible(1031);
    for (size_t size : {1u, 7u, 8u, 9u, 64u, 1031u})
        for (size_t split : {size_t(0), size / 3, size})
        {
            const uint32_t whole = codec::Crc32C(data.data(), size);
            const uint32_t head = codec::Crc32C(data.data(), split);
            EXPECT_EQ(codec::Crc32C(data.data() + split, size - split, head), whole) << size << " split " << split;
        }
}

#if defined(UNREAL_CRC32C_X86_DISPATCH)
TEST(TTDCompression_Test, Crc32C_HardwareMatchesTable)
{
    // x86-64 without SSE4.2 in the baseline: the run-time path and the table give the same CRC32C
    if (!codec::detail::Crc32CHardware())
        GTEST_SKIP() << "the CPU has no SSE4.2";
    const std::vector<uint8_t> data = Incompressible(4099);
    for (size_t size : {1u, 7u, 8u, 15u, 4096u, 4099u})
        EXPECT_EQ(codec::detail::Crc32CHardwareRun(data.data(), size, 0x12345678u),
                  codec::detail::Crc32CTableRun(data.data(), size, 0x12345678u))
            << size;
}
#endif
