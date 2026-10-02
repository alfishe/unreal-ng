// The CHD Huffman coding (chdhuffman.h): the 8-bit hunk codec and the RLE-exported table of the v5 map. MAME's
// decoder reading our output is covered by chdwriter_test.cpp (chdman verify); here: our own round trips and edges.

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "emulator/io/storage/chd/chdhuffman.h"

using namespace chd;

namespace
{
    void ExpectRoundTrip(const std::vector<uint8_t>& data, const char* what)
    {
        std::vector<uint8_t> packed(data.size() * 2 + 64);
        uint32_t written = 0;
        ASSERT_TRUE(Huffman8Encode(data.data(), static_cast<uint32_t>(data.size()), packed.data(), static_cast<uint32_t>(packed.size()), written))
            << what;
        std::vector<uint8_t> unpacked(data.size());
        ASSERT_TRUE(Huffman8Decode(packed.data(), written, unpacked.data(), static_cast<uint32_t>(unpacked.size()))) << what;
        EXPECT_EQ(unpacked, data) << what;
    }
}  // namespace

TEST(ChdHuffman_Test, EightBitCodecRoundTrips)
{
    std::mt19937 rng(42);
    std::vector<uint8_t> uniform(4096);
    for (uint8_t& b : uniform)
        b = static_cast<uint8_t>(rng());
    ExpectRoundTrip(uniform, "uniform bytes");

    std::vector<uint8_t> skewed(4096);
    std::geometric_distribution<int> geometric(0.3);
    for (uint8_t& b : skewed)
        b = static_cast<uint8_t>(std::min(255, geometric(rng)));
    ExpectRoundTrip(skewed, "skewed bytes");

    ExpectRoundTrip(std::vector<uint8_t>(4096, 0xE5), "one symbol");
    ExpectRoundTrip(std::vector<uint8_t>{1, 2}, "two symbols, two bytes");

    // Fibonacci-like counts force the length limit (16 bits) to bite
    std::vector<uint8_t> deep;
    uint32_t a = 1;
    uint32_t b = 1;
    for (int symbol = 0; symbol < 22 && deep.size() < 60000; symbol++)
    {
        deep.insert(deep.end(), a, static_cast<uint8_t>(symbol));
        const uint32_t next = a + b;
        a = b;
        b = next;
    }
    ExpectRoundTrip(deep, "length-limited");

    // A skewed hunk gets smaller; a cut-short stream is refused
    std::vector<uint8_t> packed(4096);
    uint32_t written = 0;
    ASSERT_TRUE(Huffman8Encode(skewed.data(), 4096, packed.data(), 4096, written));
    EXPECT_LT(written, 3000u);
    std::vector<uint8_t> out(4096);
    EXPECT_FALSE(Huffman8Decode(packed.data(), written / 2, out.data(), 4096));
}

TEST(ChdHuffman_Test, RleTableRoundTripsTheMapAlphabet)
{
    HuffmanCoder encoder(16, 8);
    const uint32_t counts[16] = {500, 0, 3, 0, 40, 77, 0, 9, 1, 120, 30, 0, 0, 2, 0, 0};
    for (uint32_t symbol = 0; symbol < 16; symbol++)
    {
        for (uint32_t i = 0; i < counts[symbol]; i++)
            encoder.HistoOne(symbol);
    }
    ASSERT_TRUE(encoder.ComputeTreeFromHisto());
    std::vector<uint8_t> buffer(256);
    BitWriter out(buffer.data(), buffer.size());
    ASSERT_TRUE(encoder.ExportTreeRle(out));
    const std::vector<uint32_t> message = {0, 9, 9, 5, 4, 0, 13, 8, 7, 10, 2, 0};
    for (uint32_t symbol : message)
        encoder.EncodeOne(out, symbol);
    const size_t bytes = out.Flush();

    HuffmanCoder decoder(16, 8);
    BitReader in(buffer.data(), bytes);
    ASSERT_TRUE(decoder.ImportTreeRle(in));
    for (uint32_t symbol : message)
        EXPECT_EQ(decoder.DecodeOne(in), symbol);
    EXPECT_FALSE(in.Overflow());
}
