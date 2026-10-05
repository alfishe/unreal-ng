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

TEST(FrameScaler_Test, Layout_ZxFrameInUhdFitsTheHeightWithItsAspect)
{
    // 352x288 (PAL frame with border) in 3840x2160: the full height, 2640 wide (7.5x), bars 600 left and right
    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(352, 288, 3840, 2160);
    EXPECT_EQ(layout.scale, 0u) << "7.5x is not a whole multiple";
    EXPECT_EQ(layout.height, 2160u);
    EXPECT_EQ(layout.width, 2640u);
    EXPECT_EQ(layout.offsetX, 600u);
    EXPECT_EQ(layout.offsetY, 0u);
}

TEST(FrameScaler_Test, Layout_OddShapedPictureStillFillsTheHeight)
{
    // 320x255 (the recording that looked small at 6x = 1530 high): the full height now
    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(320, 255, 3840, 2160);
    EXPECT_EQ(layout.height, 2160u);
    EXPECT_EQ(layout.width, 2710u);
    EXPECT_EQ(layout.offsetX, 565u);
    EXPECT_EQ(layout.offsetY, 0u);
}

TEST(FrameScaler_Test, Layout_SixteenByNineFillsTheFrameExactlyAsAWholeMultiple)
{
    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(1920, 1080, 3840, 2160);
    EXPECT_EQ(layout.scale, 2u);
    EXPECT_EQ(layout.width, 3840u);
    EXPECT_EQ(layout.offsetX, 0u);
    EXPECT_EQ(layout.offsetY, 0u);
}

TEST(FrameScaler_Test, Layout_WiderPictureFitsTheWidth)
{
    // 800x300 in 1920x1080: the full width (2.4x), bars above and below
    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(800, 300, 1920, 1080);
    EXPECT_EQ(layout.width, 1920u);
    EXPECT_EQ(layout.height, 720u);
    EXPECT_EQ(layout.offsetY, 180u);
}

TEST(FrameScaler_Test, Layout_LargerPictureIsFittedWithItsAspect)
{
    // A 5120x2880 window into 1920x1080: fitted whole
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
    // 4x2 source, distinct pixels, into 20x14: a whole multiple (5x), bars of 2 above and below
    const uint32_t srcW = 4;
    const uint32_t srcH = 2;
    std::vector<uint32_t> src(srcW * srcH);
    for (uint32_t i = 0; i < src.size(); i++)
        src[i] = 0x00112233u + i * 0x01010101u;

    FrameScaler::Scaler scaler;
    const uint8_t* out = scaler.Scale(reinterpret_cast<const uint8_t*>(src.data()), srcW, srcH, 20, 14);
    ASSERT_NE(out, nullptr);

    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(srcW, srcH, 20, 14);
    ASSERT_EQ(layout.scale, 5u);
    ASSERT_EQ(layout.offsetX, 0u);
    ASSERT_EQ(layout.offsetY, 2u);

    for (uint32_t y = 0; y < 14; y++)
    {
        for (uint32_t x = 0; x < 20; x++)
        {
            const bool inside = y >= 2 && y < 12;
            const uint32_t expected = inside ? src[((y - 2) / 5) * srcW + x / 5] : kBlack;
            ASSERT_EQ(PixelAt(out, 20, x, y), expected) << "x=" << x << " y=" << y;
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
    // 4x2 source into 20x14 (5x, bars of 2 above and below) with a padded stride (a pixel buffer's rows are usually longer than the picture)
    const uint32_t srcW = 4;
    const uint32_t srcH = 2;
    std::vector<uint32_t> src(srcW * srcH);
    for (uint32_t i = 0; i < src.size(); i++)
        src[i] = 0x00112233u + i * 0x01010101u;

    const uint32_t dstW = 20;
    const uint32_t dstH = 14;
    const size_t stride = (dstW + 6) * 4;
    std::vector<uint8_t> dst(stride * dstH, 0x5A);  // garbage everywhere, padding included

    ASSERT_TRUE(FrameScaler::ScaleInto(reinterpret_cast<const uint8_t*>(src.data()), srcW, srcH, dst.data(), stride,
                                       dstW, dstH, false));
    for (uint32_t y = 0; y < dstH; y++)
    {
        for (uint32_t x = 0; x < dstW; x++)
        {
            const bool inside = y >= 2 && y < 12;
            const uint32_t expected = inside ? src[((y - 2) / 5) * srcW + x / 5] : kBlack;
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

namespace
{
struct Nv12Frame
{
    std::vector<uint8_t> y;
    std::vector<uint8_t> uv;
};

std::vector<uint32_t> TestPicture(uint32_t w, uint32_t h)
{
    std::vector<uint32_t> px(static_cast<size_t>(w) * h);
    uint32_t seed = 12345u;
    for (auto& p : px)
    {
        seed = seed * 1664525u + 1013904223u;
        p = seed | 0xFF000000u;
    }
    return px;
}

/// The reference: the picture scaled into packed R,G,B,A (bars and all), then the whole frame converted
Nv12Frame ReferenceNv12(const std::vector<uint32_t>& src, uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH)
{
    std::vector<uint8_t> packed(static_cast<size_t>(dstW) * dstH * 4);
    EXPECT_TRUE(FrameScaler::ScaleInto(reinterpret_cast<const uint8_t*>(src.data()), srcW, srcH, packed.data(),
                                       static_cast<size_t>(dstW) * 4, dstW, dstH, false));
    Nv12Frame frame;
    frame.y.assign(static_cast<size_t>(dstW) * dstH, 0);
    frame.uv.assign(static_cast<size_t>(dstW) * dstH / 2, 0);
    FrameScaler::PackedToNv12(packed.data(), static_cast<size_t>(dstW) * 4, dstW, dstH, frame.y.data(), dstW,
                              frame.uv.data(), dstW);
    return frame;
}
}  // namespace

TEST(FrameScaler_Test, PackedToNv12_BlackWhiteAndRed)
{
    // 2x2: black, white / red, red: BT.601 limited range
    const uint8_t px[16] = {0, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 255, 255, 0, 0, 255};
    uint8_t y[4];
    uint8_t uv[2];
    FrameScaler::PackedToNv12(px, 8, 2, 2, y, 2, uv, 2);
    EXPECT_EQ(y[0], 16);
    EXPECT_EQ(y[1], 235);
    EXPECT_EQ(y[2], 82);  // red
    // chroma of (0 + 255 + 255 + 255) / 4 = 191 per channel R, (0+255+0+0)/4 = 63 G / B
    EXPECT_EQ(uv[0], static_cast<uint8_t>(((-38 * 191 - 74 * 63 + 112 * 63 + 128) >> 8) + 128));
    EXPECT_EQ(uv[1], static_cast<uint8_t>(((112 * 191 - 94 * 63 - 18 * 63 + 128) >> 8) + 128));
}

TEST(FrameScaler_Test, ScaleIntoNv12_MatchesTheFullFrameConversionByteForByte)
{
    struct Case
    {
        uint32_t srcW, srcH, dstW, dstH;
    };
    // Even factor, odd factor (chroma blocks on source edges), odd picture size (bars at odd offsets), 1x, a bigger
    // picture (no integer factor: the fallback path), the real ZX frame in a 4K-shaped output
    // Since the picture is FITTED, most of these are not whole factors (the generic nearest path); 6x6 -> 6x6 is
    // 1x and 4x3 -> 24x18 an exact 6x (the fast paths)
    const Case cases[] = {{4, 3, 24, 18},  {5, 3, 22, 20},   {7, 5, 32, 24},    {6, 6, 6, 6},
                          {40, 30, 24, 16}, {11, 9, 48, 40},  {352, 288, 384, 216 * 2}, {320, 255, 192, 108},
                          {352, 288, 200, 120}, {320, 255, 3840 / 4, 2160 / 4}, {8, 8, 18, 12}, {1, 1, 4, 2}};
    for (const Case& c : cases)
    {
        const std::vector<uint32_t> src = TestPicture(c.srcW, c.srcH);
        const Nv12Frame expected = ReferenceNv12(src, c.srcW, c.srcH, c.dstW, c.dstH);

        // The target with padded rows (a hardware buffer's pitch) and garbage in it
        const size_t pitch = c.dstW + 16;
        std::vector<uint8_t> buffer(pitch * c.dstH * 3 / 2, 0x77);
        uint8_t* yPlane = buffer.data();
        uint8_t* uvPlane = buffer.data() + pitch * c.dstH;
        ASSERT_TRUE(FrameScaler::ScaleIntoNv12(reinterpret_cast<const uint8_t*>(src.data()), c.srcW, c.srcH, yPlane,
                                               pitch, uvPlane, pitch, c.dstW, c.dstH))
            << c.srcW << "x" << c.srcH << " -> " << c.dstW << "x" << c.dstH;

        for (uint32_t row = 0; row < c.dstH; row++)
            ASSERT_EQ(std::memcmp(yPlane + row * pitch, expected.y.data() + static_cast<size_t>(row) * c.dstW, c.dstW), 0)
                << "Y row " << row << " of " << c.srcW << "x" << c.srcH << " -> " << c.dstW << "x" << c.dstH;
        for (uint32_t row = 0; row < c.dstH / 2; row++)
            ASSERT_EQ(std::memcmp(uvPlane + row * pitch, expected.uv.data() + static_cast<size_t>(row) * c.dstW, c.dstW), 0)
                << "UV row " << row << " of " << c.srcW << "x" << c.srcH << " -> " << c.dstW << "x" << c.dstH;
    }
}

TEST(FrameScaler_Test, ScaleIntoNv12_BarsAreBlackAndOddOutputIsRefused)
{
    const std::vector<uint32_t> src = TestPicture(2, 2);
    std::vector<uint8_t> y(16 * 8, 0x77);
    std::vector<uint8_t> uv(16 * 4, 0x77);
    ASSERT_TRUE(FrameScaler::ScaleIntoNv12(reinterpret_cast<const uint8_t*>(src.data()), 2, 2, y.data(), 16, uv.data(),
                                           16, 16, 8));
    // 2x2 in 16x8: k = 4, picture 8x8 at x = 4: the first column is a bar
    EXPECT_EQ(y[0], 16);
    EXPECT_EQ(uv[0], 128);
    EXPECT_EQ(uv[1], 128);
    EXPECT_FALSE(FrameScaler::ScaleIntoNv12(reinterpret_cast<const uint8_t*>(src.data()), 2, 2, y.data(), 17, uv.data(),
                                            17, 17, 8));
}

TEST(FrameScaler_Test, Scale_NonWholeFactorSamplesNearestAndFillsTheFrame)
{
    // 2x2 picture into a 5x5 frame: 2.5x, no bars; columns 0,0,0,1,1 (x * 2 / 5), the same rows
    const uint32_t src[4] = {0xFF0000A0u, 0xFF0000B0u, 0xFF0000C0u, 0xFF0000D0u};
    FrameScaler::Scaler scaler;
    const uint8_t* out = scaler.Scale(reinterpret_cast<const uint8_t*>(src), 2, 2, 5, 5);
    ASSERT_NE(out, nullptr);
    const uint32_t column[5] = {0, 0, 0, 1, 1};
    for (uint32_t y = 0; y < 5; y++)
        for (uint32_t x = 0; x < 5; x++)
            ASSERT_EQ(PixelAt(out, 5, x, y), src[column[y] * 2 + column[x]]) << "x=" << x << " y=" << y;
}

TEST(FrameScaler_Test, ScaleInto_NonWholeFactorFitsWithBarsAndSwaps)
{
    // 3x2 picture in 10x10: width-bound (3.33x): 10 wide, 6 high (2 * 10 / 3), bars of 2 above and below
    const uint32_t src[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    std::vector<uint8_t> dst(10 * 10 * 4, 0x5A);
    ASSERT_TRUE(FrameScaler::ScaleInto(reinterpret_cast<const uint8_t*>(src), 3, 2, dst.data(), 40, 10, 10, false));
    const FrameScaler::Layout layout = FrameScaler::ComputeLayout(3, 2, 10, 10);
    ASSERT_EQ(layout.width, 10u);
    ASSERT_EQ(layout.height, 6u);
    ASSERT_EQ(layout.offsetY, 2u);
    EXPECT_EQ(PixelAt(dst.data(), 10, 0, 0), kBlack);
    EXPECT_EQ(PixelAt(dst.data(), 10, 9, 9), kBlack);
    for (uint32_t y = 0; y < 6; y++)
        for (uint32_t x = 0; x < 10; x++)
            ASSERT_EQ(PixelAt(dst.data(), 10, x, 2 + y), src[(y * 2 / 6) * 3 + x * 3 / 10]) << "x=" << x << " y=" << y;
}
