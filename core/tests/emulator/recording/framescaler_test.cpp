#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "common/framescaler.h"

/// FrameScaler: the sharp 4K / 1080p output scaler of the recording profiles - nearest, the largest integer factor
/// that fits, aspect kept, black bars.

namespace
{
uint32_t PixelAt(const uint8_t* out, uint32_t stride, uint32_t x, uint32_t y)
{
    uint32_t value;
    std::memcpy(&value, out + (static_cast<size_t>(y) * stride + x) * 4, 4);
    return value;
}

constexpr uint32_t kBlack = 0xFF000000u;  // little-endian bytes 00 00 00 FF
}  // namespace

TEST(FrameScaler_Test, Layout_ZxFrameInUhdTakesTheLargestIntegerFactor)
{
    // 352x288 (PAL frame with border) in 3840x2160: 7x = 2464x2016, bars 688 left/right, 72 top/bottom
    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(352, 288, 3840, 2160);
    EXPECT_EQ(layout.scale, 7u);
    EXPECT_EQ(layout.width, 2464u);
    EXPECT_EQ(layout.height, 2016u);
    EXPECT_EQ(layout.offsetX, 688u);
    EXPECT_EQ(layout.offsetY, 72u);
}

TEST(FrameScaler_Test, Layout_SixteenByNineFillsTheFrameExactly)
{
    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(1920, 1080, 3840, 2160);
    EXPECT_EQ(layout.scale, 2u);
    EXPECT_EQ(layout.width, 3840u);
    EXPECT_EQ(layout.offsetX, 0u);
    EXPECT_EQ(layout.offsetY, 0u);
}

TEST(FrameScaler_Test, Layout_LargerPictureIsFittedWithItsAspect)
{
    // A 5120x2880 window into 1920x1080: no integer factor, fitted whole
    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(5120, 2880, 1920, 1080);
    EXPECT_EQ(layout.scale, 0u);
    EXPECT_EQ(layout.width, 1920u);
    EXPECT_EQ(layout.height, 1080u);
}

TEST(FrameScaler_Test, Layout_ZeroSizeGivesNothing)
{
    EXPECT_EQ(FrameScaler::ComputeLayout(0, 192, 3840, 2160).width, 0u);
    EXPECT_EQ(FrameScaler::ComputeLayout(256, 192, 0, 2160).width, 0u);
}

TEST(FrameScaler_Test, Scale_EverySourcePixelBecomesAnExactBlockAndBarsAreOpaqueBlack)
{
    // 4x2 source, distinct pixels, into 20x10: k = 5, picture 20x10 - fills; use 22x12 to get bars of 1
    const uint32_t srcW = 4;
    const uint32_t srcH = 2;
    std::vector<uint32_t> src(srcW * srcH);
    for (uint32_t i = 0; i < src.size(); i++)
        src[i] = 0x00112233u + i * 0x01010101u;

    FrameScaler::Scaler scaler;
    const uint8_t* out = scaler.Scale(reinterpret_cast<const uint8_t*>(src.data()), srcW, srcH, 22, 12);
    ASSERT_NE(out, nullptr);

    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(srcW, srcH, 22, 12);
    ASSERT_EQ(layout.scale, 5u);
    ASSERT_EQ(layout.offsetX, 1u);
    ASSERT_EQ(layout.offsetY, 1u);

    for (uint32_t y = 0; y < 12; y++)
    {
        for (uint32_t x = 0; x < 22; x++)
        {
            const bool inside = x >= 1 && x < 21 && y >= 1 && y < 11;
            const uint32_t expected = inside ? src[((y - 1) / 5) * srcW + (x - 1) / 5] : kBlack;
            ASSERT_EQ(PixelAt(out, 22, x, y), expected) << "x=" << x << " y=" << y;
        }
    }
}

TEST(FrameScaler_Test, Scale_ReusedBufferRedrawsBarsWhenThePictureChangesShape)
{
    FrameScaler::Scaler scaler;
    std::vector<uint32_t> wide(8 * 2, 0xFFFFFFFFu);
    std::vector<uint32_t> narrow(2 * 2, 0xFFFFFFFFu);

    const uint8_t* out = scaler.Scale(reinterpret_cast<const uint8_t*>(wide.data()), 8, 2, 16, 8);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(PixelAt(out, 16, 0, 0), kBlack);   // bar above the 16x4 picture
    EXPECT_EQ(PixelAt(out, 16, 0, 2), 0xFFFFFFFFu);

    // 2x2 into 16x8: k = 4, 8x8 picture, bars left and right; the old picture's pixels there must be black
    out = scaler.Scale(reinterpret_cast<const uint8_t*>(narrow.data()), 2, 2, 16, 8);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(PixelAt(out, 16, 0, 2), kBlack);
    EXPECT_EQ(PixelAt(out, 16, 4, 2), 0xFFFFFFFFu);
    EXPECT_EQ(PixelAt(out, 16, 15, 7), kBlack);
}

TEST(FrameScaler_Test, Scale_NullSourceOrEmptySizeIsRefused)
{
    FrameScaler::Scaler scaler;
    EXPECT_EQ(scaler.Scale(nullptr, 4, 4, 16, 16), nullptr);
    std::vector<uint32_t> px(16, 0);
    EXPECT_EQ(scaler.Scale(reinterpret_cast<const uint8_t*>(px.data()), 0, 4, 16, 16), nullptr);
}

TEST(FrameScaler_Test, ScaleInto_WritesPictureAndBarsOverGarbageWithTheCallersStride)
{
    // 4x2 source into 22x12 with a padded stride (a pixel buffer's rows are usually longer than the picture)
    const uint32_t srcW = 4;
    const uint32_t srcH = 2;
    std::vector<uint32_t> src(srcW * srcH);
    for (uint32_t i = 0; i < src.size(); i++)
        src[i] = 0x00112233u + i * 0x01010101u;

    const uint32_t dstW = 22;
    const uint32_t dstH = 12;
    const size_t stride = (dstW + 6) * 4;
    std::vector<uint8_t> dst(stride * dstH, 0x5A);  // garbage everywhere, padding included

    ASSERT_TRUE(FrameScaler::ScaleInto(reinterpret_cast<const uint8_t*>(src.data()), srcW, srcH, dst.data(), stride,
                                       dstW, dstH, false));
    for (uint32_t y = 0; y < dstH; y++)
    {
        for (uint32_t x = 0; x < dstW; x++)
        {
            const bool inside = x >= 1 && x < 21 && y >= 1 && y < 11;
            const uint32_t expected = inside ? src[((y - 1) / 5) * srcW + (x - 1) / 5] : kBlack;
            ASSERT_EQ(PixelAt(dst.data(), static_cast<uint32_t>(stride / 4), x, y), expected) << "x=" << x << " y=" << y;
        }
        // The padding after the output row is not touched
        for (size_t b = dstW * 4; b < stride; b++)
            ASSERT_EQ(dst[y * stride + b], 0x5A);
    }
}

TEST(FrameScaler_Test, ScaleInto_SwapsRedAndBlueOncePerSourcePixelAndKeepsAlpha)
{
    // R,G,B,A bytes 11 22 33 44 -> B,G,R,A bytes 33 22 11 44; the bars stay 00 00 00 FF
    const uint8_t rgba[4] = {0x11, 0x22, 0x33, 0x44};
    std::vector<uint8_t> dst(6 * 4 * 4);
    ASSERT_TRUE(FrameScaler::ScaleInto(rgba, 1, 1, dst.data(), 6 * 4, 6, 4, true));
    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(1, 1, 6, 4);
    ASSERT_EQ(layout.scale, 4u);
    const uint8_t* picturePixel = dst.data() + (static_cast<size_t>(layout.offsetY) * 6 + layout.offsetX) * 4;
    EXPECT_EQ(picturePixel[0], 0x33);
    EXPECT_EQ(picturePixel[1], 0x22);
    EXPECT_EQ(picturePixel[2], 0x11);
    EXPECT_EQ(picturePixel[3], 0x44);
    EXPECT_EQ(PixelAt(dst.data(), 6, 0, 0), kBlack);
}

TEST(FrameScaler_Test, ScaleInto_DownscaleSwapsToo)
{
    std::vector<uint32_t> src(8 * 8, 0x00FF0000u);  // bytes 00 00 FF 00: R = 0, B = 0xFF in memory order R,G,B,A
    std::vector<uint8_t> dst(4 * 4 * 4);
    ASSERT_TRUE(FrameScaler::ScaleInto(reinterpret_cast<const uint8_t*>(src.data()), 8, 8, dst.data(), 16, 4, 4, true));
    EXPECT_EQ(PixelAt(dst.data(), 4, 1, 1), 0x000000FFu);
}

TEST(FrameScaler_Test, ScaleInto_RefusesAShortStrideOrNothingToScale)
{
    std::vector<uint32_t> px(4, 0);
    std::vector<uint8_t> dst(64 * 4);
    EXPECT_FALSE(FrameScaler::ScaleInto(reinterpret_cast<const uint8_t*>(px.data()), 2, 2, dst.data(), 8, 4, 4, false));
    EXPECT_FALSE(FrameScaler::ScaleInto(nullptr, 2, 2, dst.data(), 16, 4, 4, false));
    EXPECT_FALSE(FrameScaler::ScaleInto(reinterpret_cast<const uint8_t*>(px.data()), 0, 2, dst.data(), 16, 4, 4, false));
}
