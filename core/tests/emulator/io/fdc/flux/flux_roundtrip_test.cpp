#include <gtest/gtest.h>
#include <emulator/io/fdc/flux/flux_pll.h>
#include <emulator/io/fdc/flux/mfm_decoder.h>
#include <emulator/io/fdc/flux/mfm_encoder.h>
#include <vector>

using namespace fdc::flux;

class FluxRoundtrip_Test : public ::testing::Test
{
protected:
    bool isClockBitSet(const std::vector<uint8_t>& bitmap, size_t byteIndex)
    {
        size_t bitmapByte = byteIndex / 8;
        size_t bitmapBit = byteIndex % 8;
        if (bitmapByte >= bitmap.size()) return false;
        return (bitmap[bitmapByte] & (1 << bitmapBit)) != 0;
    }
};

TEST_F(FluxRoundtrip_Test, MFM_Encode_Decode_SingleByte)
{
    MfmEncoder encoder;
    MfmDecoder decoder;

    std::vector<uint8_t> original = {0x4E};
    std::vector<uint8_t> clockBitmap = {0};

    encoder.encode(original, clockBitmap);
    decoder.decode(encoder.bitCells());

    EXPECT_EQ(decoder.byteCount(), 1);
    EXPECT_EQ(decoder.bytes()[0], 0x4E);
}

TEST_F(FluxRoundtrip_Test, MFM_Encode_Decode_SyncA1)
{
    MfmEncoder encoder;
    MfmDecoder decoder;

    std::vector<uint8_t> original = {0xA1, 0xA1, 0xA1, 0xFE};
    std::vector<uint8_t> clockBitmap = {0x07};

    encoder.encode(original, clockBitmap);
    decoder.decode(encoder.bitCells());

    ASSERT_EQ(decoder.byteCount(), 4);
    for (size_t i = 0; i < 4; i++)
    {
        EXPECT_EQ(decoder.bytes()[i], original[i]) << "Byte " << i << " mismatch";
    }

    EXPECT_TRUE((decoder.clockBitmap()[0] & 0x07) == 0x07) << "First 3 bytes should be sync marks";
    EXPECT_FALSE((decoder.clockBitmap()[0] & 0x08)) << "Fourth byte should not be sync mark";
}

TEST_F(FluxRoundtrip_Test, MFM_Encode_Decode_SimpleSector)
{
    MfmEncoder encoder;
    MfmDecoder decoder;

    std::vector<uint8_t> original;
    std::vector<uint8_t> clockBitmap;

    auto addBytes = [&](std::initializer_list<uint8_t> bytes, bool sync = false) {
        for (uint8_t b : bytes)
        {
            size_t byteIdx = original.size();
            original.push_back(b);

            size_t bitmapByte = byteIdx / 8;
            while (clockBitmap.size() <= bitmapByte)
            {
                clockBitmap.push_back(0);
            }

            if (sync)
            {
                clockBitmap[bitmapByte] |= (1 << (byteIdx % 8));
            }
        }
    };

    for (int i = 0; i < 5; i++) addBytes({0x4E});

    addBytes({0xA1}, true);
    addBytes({0xA1}, true);
    addBytes({0xA1}, true);
    addBytes({0xFE});
    addBytes({0x00, 0x00, 0x01, 0x01});

    addBytes({0xA1}, true);
    addBytes({0xA1}, true);
    addBytes({0xA1}, true);
    addBytes({0xFB});

    for (int i = 0; i < 16; i++) addBytes({static_cast<uint8_t>(0x10 + i)});

    encoder.encode(original, clockBitmap);
    decoder.decode(encoder.bitCells());

    ASSERT_EQ(decoder.byteCount(), original.size());
    for (size_t i = 0; i < original.size(); i++)
    {
        EXPECT_EQ(decoder.bytes()[i], original[i]) << "Byte " << i << " mismatch";
    }
}

TEST_F(FluxRoundtrip_Test, MFM_Encode_Decode_AllByteValues)
{
    MfmEncoder encoder;
    MfmDecoder decoder;

    std::vector<uint8_t> original;
    for (int i = 0; i < 256; i++)
    {
        original.push_back(static_cast<uint8_t>(i));
    }
    std::vector<uint8_t> clockBitmap(32, 0);

    encoder.encode(original, clockBitmap);
    decoder.decode(encoder.bitCells());

    ASSERT_EQ(decoder.byteCount(), 256);
    for (int i = 0; i < 256; i++)
    {
        EXPECT_EQ(decoder.bytes()[i], static_cast<uint8_t>(i)) << "Byte value " << i << " mismatch";
    }
}

TEST_F(FluxRoundtrip_Test, FM_Encode_Decode_SingleByte)
{
    FmEncoder encoder;
    FmDecoder decoder;

    std::vector<uint8_t> original = {0x55};
    std::vector<uint8_t> clockBitmap = {0};

    encoder.encode(original, clockBitmap);
    decoder.decode(encoder.bitCells());

    EXPECT_EQ(decoder.byteCount(), 1);
    EXPECT_EQ(decoder.bytes()[0], 0x55);
}

TEST_F(FluxRoundtrip_Test, FM_Encode_Decode_AddressMarks)
{
    FmEncoder encoder;
    FmDecoder decoder;

    std::vector<uint8_t> original = {0xFF, 0xFE, 0x00, 0x00, 0x01, 0x00};
    std::vector<uint8_t> clockBitmap = {0x02};

    encoder.encode(original, clockBitmap);
    decoder.decode(encoder.bitCells());

    ASSERT_EQ(decoder.byteCount(), 6);
    for (size_t i = 0; i < 6; i++)
    {
        EXPECT_EQ(decoder.bytes()[i], original[i]) << "Byte " << i << " mismatch";
    }

    EXPECT_TRUE(decoder.clockBitmap()[0] & 0x02) << "IDAM should be marked";
}

TEST_F(FluxRoundtrip_Test, MFM_FluxPll_FullPipeline)
{
    MfmEncoder encoder;
    MfmDecoder decoder;

    // Use a longer sequence with trailing gap bytes to ensure complete data
    std::vector<uint8_t> original = {0xA1, 0xA1, 0xA1, 0xFE, 0x4E, 0x4E};
    std::vector<uint8_t> clockBitmap = {0x07};  // First 3 are sync marks

    encoder.encode(original, clockBitmap);

    // Convert bit cells to flux intervals
    std::vector<uint32_t> intervals;
    int cellsSinceTransition = 0;
    for (uint8_t cell : encoder.bitCells())
    {
        cellsSinceTransition++;
        if (cell == 1)
        {
            intervals.push_back(cellsSinceTransition * 2000);
            cellsSinceTransition = 0;
        }
    }
    // Add a final interval if there are trailing zero cells
    if (cellsSinceTransition > 0)
    {
        intervals.push_back(cellsSinceTransition * 2000);
    }

    FluxPll pll{FluxPll::Mode::MFM};
    pll.addIntervals(intervals);
    decoder.decode(pll.bitCells());

    // Verify A1 sync is detected
    bool foundA1 = false;
    for (size_t i = 0; i < decoder.byteCount(); i++)
    {
        if (decoder.bytes()[i] == 0xA1 && isClockBitSet(decoder.clockBitmap(), i))
        {
            foundA1 = true;
            break;
        }
    }
    EXPECT_TRUE(foundA1) << "A1 sync should be detected in flux pipeline";

    // The full pipeline should recover bytes - exact count may vary due to phase
    EXPECT_GE(decoder.byteCount(), 4) << "Should decode at least 4 bytes";
}
