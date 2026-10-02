// The CHD hunk codecs (chdcodec.h): each one's round trip, the "not smaller" rule, and codec list parsing.

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

#include "emulator/io/storage/chd/chdcodec.h"

using namespace chd;

TEST(ChdCodec_Test, EveryCodecRoundTripsAndRefusesToGrow)
{
    const uint32_t hunk = 4096;
    std::vector<uint8_t> text(hunk);
    const char* words = "a sector of a hard disk holds five hundred and twelve bytes ";
    for (uint32_t i = 0; i < hunk; i++)
        text[i] = static_cast<uint8_t>(words[i % 61]);
    std::vector<uint8_t> audio(hunk);
    for (uint32_t i = 0; i < hunk / 2; i++)
    {
        const int16_t s = static_cast<int16_t>(10000 * std::sin(i / 9.0));
        audio[2 * i] = static_cast<uint8_t>(s);
        audio[2 * i + 1] = static_cast<uint8_t>(s >> 8);
    }
    std::mt19937 rng(3);
    std::vector<uint8_t> noise(hunk);
    for (uint8_t& b : noise)
        b = static_cast<uint8_t>(rng());

    for (uint32_t tag : {kCodecZlib, kCodecLzma, kCodecHuffman, kCodecFlac, kCodecZstd})
    {
        auto codec = CreateCodec(tag, hunk);
        ASSERT_NE(codec, nullptr) << CodecName(tag);
        const std::vector<uint8_t>& input = tag == kCodecFlac ? audio : text;
        std::vector<uint8_t> packed(hunk);
        uint32_t written = 0;
        ASSERT_TRUE(codec->Compress(input.data(), hunk, packed.data(), written)) << CodecName(tag);
        EXPECT_LT(written, hunk) << CodecName(tag);
        std::vector<uint8_t> unpacked(hunk);
        ASSERT_TRUE(codec->Decompress(packed.data(), written, unpacked.data(), hunk)) << CodecName(tag);
        EXPECT_EQ(unpacked, input) << CodecName(tag);

        // Noise does not get smaller: the hunk is stored as is
        EXPECT_FALSE(codec->Compress(noise.data(), hunk, packed.data(), written)) << CodecName(tag);
    }
    EXPECT_EQ(CreateCodec(kCodecCdLzma, hunk), nullptr);
    EXPECT_EQ(CreateCodec(kCodecAvHuff, hunk), nullptr);
}

TEST(ChdCodec_Test, CodecListsByName)
{
    CodecList codecs{};
    std::string error;
    ASSERT_TRUE(ParseCodecList("none", codecs, &error));
    EXPECT_EQ(codecs[0], kCodecNone);
    ASSERT_TRUE(ParseCodecList("default", codecs, &error));
    EXPECT_EQ(FormatCodecList(codecs), "lzma,zlib,huff,flac");
    ASSERT_TRUE(ParseCodecList("ZSTD + huffman", codecs, &error));
    EXPECT_EQ(FormatCodecList(codecs), "zstd,huff");
    EXPECT_FALSE(ParseCodecList("lzma,cdlz", codecs, &error));
    EXPECT_NE(error.find("cdlz"), std::string::npos) << error;
    EXPECT_FALSE(ParseCodecList("lzma,lzma", codecs, &error));
    EXPECT_FALSE(ParseCodecList("lzma,zlib,huff,flac,zstd", codecs, &error));
    EXPECT_EQ(CodecName(kCodecCdFlac), "cdfl");
}
