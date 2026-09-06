#include <gtest/gtest.h>
#include <emulator/io/fdc/flux/mfm_decoder.h>
#include <emulator/io/fdc/flux/mfm_encoder.h>
#include <vector>

using namespace fdc::flux;

class MfmDecoder_Test : public ::testing::Test
{
protected:
    MfmDecoder decoder;

    std::vector<uint8_t> patternToBits(uint16_t pattern)
    {
        std::vector<uint8_t> bits;
        for (int i = 15; i >= 0; --i)
        {
            bits.push_back((pattern >> i) & 1);
        }
        return bits;
    }

    std::vector<uint8_t> encodeMfmByte(uint8_t byte, bool& prevDataBit)
    {
        std::vector<uint8_t> bits;
        for (int i = 7; i >= 0; --i)
        {
            bool dataBit = (byte >> i) & 1;
            bool clockBit = (!prevDataBit && !dataBit);
            bits.push_back(clockBit ? 1 : 0);
            bits.push_back(dataBit ? 1 : 0);
            prevDataBit = dataBit;
        }
        return bits;
    }

    bool isClockBitSet(const std::vector<uint8_t>& bitmap, size_t byteIndex)
    {
        size_t bitmapByte = byteIndex / 8;
        size_t bitmapBit = byteIndex % 8;
        if (bitmapByte >= bitmap.size()) return false;
        return (bitmap[bitmapByte] & (1 << bitmapBit)) != 0;
    }
};

TEST_F(MfmDecoder_Test, SyncA1_Pattern)
{
    auto bits = patternToBits(0x4489);
    decoder.decode(bits);

    ASSERT_EQ(decoder.byteCount(), 1);
    EXPECT_EQ(decoder.bytes()[0], 0xA1);
    EXPECT_TRUE(isClockBitSet(decoder.clockBitmap(), 0));
}

TEST_F(MfmDecoder_Test, C2Pattern_DecodesAsNormalByte)
{
    // C2 sync pattern 0x5224 is NOT specially detected
    auto bits = patternToBits(0x5224);
    decoder.decode(bits);

    ASSERT_EQ(decoder.byteCount(), 1);
    EXPECT_EQ(decoder.bytes()[0], 0xC2);
    EXPECT_FALSE(isClockBitSet(decoder.clockBitmap(), 0));
}

TEST_F(MfmDecoder_Test, NormalByte_0x00)
{
    bool prevData = false;
    auto bits = encodeMfmByte(0x00, prevData);
    decoder.decode(bits);

    ASSERT_EQ(decoder.byteCount(), 1);
    EXPECT_EQ(decoder.bytes()[0], 0x00);
    EXPECT_FALSE(isClockBitSet(decoder.clockBitmap(), 0));
}

TEST_F(MfmDecoder_Test, NormalByte_0xFF)
{
    bool prevData = false;
    auto bits = encodeMfmByte(0xFF, prevData);
    decoder.decode(bits);

    ASSERT_EQ(decoder.byteCount(), 1);
    EXPECT_EQ(decoder.bytes()[0], 0xFF);
    EXPECT_FALSE(isClockBitSet(decoder.clockBitmap(), 0));
}

TEST_F(MfmDecoder_Test, NormalByte_0x4E)
{
    bool prevData = false;
    auto bits = encodeMfmByte(0x4E, prevData);
    decoder.decode(bits);

    ASSERT_EQ(decoder.byteCount(), 1);
    EXPECT_EQ(decoder.bytes()[0], 0x4E);
    EXPECT_FALSE(isClockBitSet(decoder.clockBitmap(), 0));
}

TEST_F(MfmDecoder_Test, MultipleBytes_GapAndSync)
{
    std::vector<uint8_t> allBits;
    bool prevData = false;

    auto gapBits = encodeMfmByte(0x4E, prevData);
    allBits.insert(allBits.end(), gapBits.begin(), gapBits.end());

    for (int i = 0; i < 3; i++)
    {
        auto a1Bits = patternToBits(0x4489);
        allBits.insert(allBits.end(), a1Bits.begin(), a1Bits.end());
        prevData = true;
    }

    auto feBits = encodeMfmByte(0xFE, prevData);
    allBits.insert(allBits.end(), feBits.begin(), feBits.end());

    decoder.decode(allBits);

    ASSERT_EQ(decoder.byteCount(), 5);
    EXPECT_EQ(decoder.bytes()[0], 0x4E);
    EXPECT_EQ(decoder.bytes()[1], 0xA1);
    EXPECT_EQ(decoder.bytes()[2], 0xA1);
    EXPECT_EQ(decoder.bytes()[3], 0xA1);
    EXPECT_EQ(decoder.bytes()[4], 0xFE);

    EXPECT_FALSE(isClockBitSet(decoder.clockBitmap(), 0));
    EXPECT_TRUE(isClockBitSet(decoder.clockBitmap(), 1));
    EXPECT_TRUE(isClockBitSet(decoder.clockBitmap(), 2));
    EXPECT_TRUE(isClockBitSet(decoder.clockBitmap(), 3));
    EXPECT_FALSE(isClockBitSet(decoder.clockBitmap(), 4));
}

TEST_F(MfmDecoder_Test, Reset_ClearsState)
{
    auto bits = patternToBits(0x4489);
    decoder.decode(bits);
    EXPECT_GT(decoder.byteCount(), 0);

    decoder.reset();
    EXPECT_EQ(decoder.byteCount(), 0);
    EXPECT_EQ(decoder.clockBitmap().size(), 0);
}

class FmDecoder_Test : public ::testing::Test
{
protected:
    FmDecoder decoder;

    std::vector<uint8_t> encodeFmByte(uint8_t byte, uint8_t clockPattern = 0xFF)
    {
        std::vector<uint8_t> bits;
        for (int i = 7; i >= 0; --i)
        {
            bits.push_back((clockPattern >> i) & 1);
            bits.push_back((byte >> i) & 1);
        }
        return bits;
    }

    bool isClockBitSet(const std::vector<uint8_t>& bitmap, size_t byteIndex)
    {
        size_t bitmapByte = byteIndex / 8;
        size_t bitmapBit = byteIndex % 8;
        if (bitmapByte >= bitmap.size()) return false;
        return (bitmap[bitmapByte] & (1 << bitmapBit)) != 0;
    }
};

TEST_F(FmDecoder_Test, IdamMark_FE)
{
    auto bits = encodeFmByte(0xFE, 0xC7);
    decoder.decode(bits);

    ASSERT_EQ(decoder.byteCount(), 1);
    EXPECT_EQ(decoder.bytes()[0], 0xFE);
    EXPECT_TRUE(isClockBitSet(decoder.clockBitmap(), 0));
}

TEST_F(FmDecoder_Test, DamMark_FB)
{
    auto bits = encodeFmByte(0xFB, 0xD7);
    decoder.decode(bits);

    ASSERT_EQ(decoder.byteCount(), 1);
    EXPECT_EQ(decoder.bytes()[0], 0xFB);
    EXPECT_TRUE(isClockBitSet(decoder.clockBitmap(), 0));
}

TEST_F(FmDecoder_Test, NormalByte_AllClocks)
{
    auto bits = encodeFmByte(0x55, 0xFF);
    decoder.decode(bits);

    ASSERT_EQ(decoder.byteCount(), 1);
    EXPECT_EQ(decoder.bytes()[0], 0x55);
    EXPECT_FALSE(isClockBitSet(decoder.clockBitmap(), 0));
}

TEST_F(FmDecoder_Test, MultipleBytes)
{
    std::vector<uint8_t> allBits;

    auto gap = encodeFmByte(0xFF, 0xFF);
    allBits.insert(allBits.end(), gap.begin(), gap.end());

    auto idam = encodeFmByte(0xFE, 0xC7);
    allBits.insert(allBits.end(), idam.begin(), idam.end());

    auto id = encodeFmByte(0x00, 0xFF);
    allBits.insert(allBits.end(), id.begin(), id.end());

    decoder.decode(allBits);

    ASSERT_EQ(decoder.byteCount(), 3);
    EXPECT_EQ(decoder.bytes()[0], 0xFF);
    EXPECT_EQ(decoder.bytes()[1], 0xFE);
    EXPECT_EQ(decoder.bytes()[2], 0x00);
    EXPECT_TRUE(isClockBitSet(decoder.clockBitmap(), 1));
}
