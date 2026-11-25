#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
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

TEST(FrameScaler_Test, ScaleInto_EveryWholeFactorAndWidthMatchesThePixelFormula)
{
    // The SIMD red / blue swap takes 16 (NEON) or 4 (SSE2) pixels a step and the row expansion has a case per
    // factor: every factor 1..5 and widths around the steps, against the output pixel computed one by one
    for (uint32_t k = 1; k <= 5; k++)
        for (uint32_t w = 1; w <= 37; w += 3)
            for (bool swap : {false, true})
            {
                const uint32_t h = 3;
                const std::vector<uint32_t> src = TestPicture(w, h);
                const uint32_t dstW = w * k, dstH = h * k;
                const size_t stride = static_cast<size_t>(dstW) * 4 + 12;
                std::vector<uint8_t> dst(stride * dstH, 0x55);
                ASSERT_TRUE(FrameScaler::ScaleInto(reinterpret_cast<const uint8_t*>(src.data()), w, h, dst.data(),
                                                   stride, dstW, dstH, swap));
                for (uint32_t y = 0; y < dstH; y++)
                    for (uint32_t x = 0; x < dstW; x++)
                    {
                        const uint32_t p = src[static_cast<size_t>(y / k) * w + x / k];
                        const uint32_t expected =
                            swap ? (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16) : p;
                        ASSERT_EQ(PixelAt(dst.data(), static_cast<uint32_t>(stride / 4), x, y), expected)
                            << "k " << k << " width " << w << " swap " << swap << " at " << x << "," << y;
                    }
            }
}

TEST(FrameScaler_Test, PackedToNv12_BlackWhiteAndRed)
{
    // 2x2: black, white / red, red: BT.709 limited range
    const uint8_t px[16] = {0, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 255, 255, 0, 0, 255};
    uint8_t y[4];
    uint8_t uv[2];
    FrameScaler::PackedToNv12(px, 8, 2, 2, y, 2, uv, 2);
    EXPECT_EQ(y[0], 16);
    EXPECT_EQ(y[1], 235);
    EXPECT_EQ(y[2], 63);  // red
    // chroma of (0 + 255 + 255 + 255) / 4 = 191 per channel R, (0+255+0+0)/4 = 63 G / B
    EXPECT_EQ(uv[0], static_cast<uint8_t>(((-26 * 191 - 86 * 63 + 112 * 63 + 128) >> 8) + 128));
    EXPECT_EQ(uv[1], static_cast<uint8_t>(((112 * 191 - 102 * 63 - 10 * 63 + 128) >> 8) + 128));
}

TEST(FrameScaler_Test, PackedToNv12_IsBt709LimitedRangeWithinOneOfTheExactFormula)
{
    // HD files are tagged and played as BT.709 (FrameScaler::FfmpegColorTags, the native encoders' tags): the
    // integer arithmetic must be BT.709's, not BT.601's (a green 30 levels off when played). Flat 2x2 blocks of the
    // primaries, the secondaries and greys against Kr 0.2126, Kb 0.0722 in doubles
    const uint32_t colors[] = {0x000000u, 0xFFFFFFu, 0xFF0000u, 0x00FF00u, 0x0000FFu, 0xFFFF00u,
                                0x00FFFFu, 0xFF00FFu, 0xCDCDCDu, 0x808080u, 0x00C525u, 0xD62816u};
    for (const uint32_t c : colors)
    {
        const int r = static_cast<int>(c >> 16), g = static_cast<int>((c >> 8) & 0xFF), b = static_cast<int>(c & 0xFF);
        uint8_t px[16];
        for (int i = 0; i < 4; i++)
        {
            px[i * 4] = static_cast<uint8_t>(r);
            px[i * 4 + 1] = static_cast<uint8_t>(g);
            px[i * 4 + 2] = static_cast<uint8_t>(b);
            px[i * 4 + 3] = 255;
        }
        uint8_t y[4];
        uint8_t uv[2];
        FrameScaler::PackedToNv12(px, 8, 2, 2, y, 2, uv, 2);
        const double kr = 0.2126, kb = 0.0722, kg = 1 - kr - kb;
        const double luma = (kr * r + kg * g + kb * b) / 255.0;
        const double expectY = 16 + 219 * luma;
        const double expectU = 128 + 224 * (b / 255.0 - luma) / (2 * (1 - kb));
        const double expectV = 128 + 224 * (r / 255.0 - luma) / (2 * (1 - kr));
        EXPECT_NEAR(y[0], expectY, 1.0) << std::hex << c;
        EXPECT_NEAR(uv[0], expectU, 1.0) << std::hex << c;
        EXPECT_NEAR(uv[1], expectV, 1.0) << std::hex << c;
    }
}

TEST(FrameScaler_Test, PackedToNv12_VectorRowsMatchTheFormulaAtEveryWidth)
{
    // The NEON / SSE2 kernels take 16 pixels a step, the scalar formula the rest: every width up to three steps and
    // a tail, random colors plus the extremes (the ranges where a lane could overflow or a clamp would matter)
    auto clampByte = [](int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
    for (uint32_t w = 2; w <= 70; w += 2)
    {
        const uint32_t h = 4;
        std::vector<uint32_t> src = TestPicture(w, h);
        const uint32_t extremes[] = {0xFF000000u, 0xFFFFFFFFu, 0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFF00u};
        for (uint32_t i = 0; i < w && i < 6; i++)
            src[i] = extremes[i];  // row 0
        for (uint32_t i = 0; i < w; i++)
            src[static_cast<size_t>(w) * 3 + i] = (i & 1) ? 0xFFFFFFFFu : 0xFF000000u;  // row 3: black / white
        const uint8_t* px = reinterpret_cast<const uint8_t*>(src.data());

        // Padded rows on both sides: a stride is not the width
        const size_t srcStride = static_cast<size_t>(w) * 4;
        const size_t pitch = w + 7;
        std::vector<uint8_t> yPlane(pitch * h, 0x77);
        std::vector<uint8_t> uvPlane(pitch * h / 2, 0x77);
        FrameScaler::PackedToNv12(px, srcStride, w, h, yPlane.data(), pitch, uvPlane.data(), pitch);

        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
            {
                const uint8_t* p = px + y * srcStride + x * 4;
                const uint8_t expected = clampByte(((47 * p[0] + 157 * p[1] + 16 * p[2] + 128) >> 8) + 16);
                ASSERT_EQ(yPlane[y * pitch + x], expected) << "Y " << x << "," << y << " width " << w;
            }
        for (uint32_t y = 0; y < h; y += 2)
            for (uint32_t x = 0; x < w; x += 2)
            {
                const uint8_t* a = px + y * srcStride + x * 4;
                const uint8_t* b = a + srcStride;
                int avg[3];
                for (int c = 0; c < 3; c++)
                    avg[c] = (a[c] + a[c + 4] + b[c] + b[c + 4]) / 4;
                const uint8_t u = clampByte(((-26 * avg[0] - 86 * avg[1] + 112 * avg[2] + 128) >> 8) + 128);
                const uint8_t v = clampByte(((112 * avg[0] - 102 * avg[1] - 10 * avg[2] + 128) >> 8) + 128);
                ASSERT_EQ(uvPlane[y / 2 * pitch + x], u) << "U " << x << "," << y << " width " << w;
                ASSERT_EQ(uvPlane[y / 2 * pitch + x + 1], v) << "V " << x << "," << y << " width " << w;
            }
        for (uint32_t y = 0; y < h; y++)
            EXPECT_EQ(yPlane[y * pitch + w], 0x77) << "the padding is not written, width " << w;
    }
}

TEST(FrameScaler_Test, PackedToNv12_BgraGivesTheBytesOfTheSameColoursInRgba)
{
    // The window recorder composes B,G,R,A frames (QImage ARGB32): the same picture with red and blue swapped must
    // give the same planes, on the vector rows and the scalar tail alike
    for (uint32_t w = 2; w <= 70; w += 2)
    {
        const uint32_t h = 4;
        std::vector<uint32_t> rgba = TestPicture(w, h);
        const uint32_t extremes[] = {0xFF000000u, 0xFFFFFFFFu, 0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFF00u};
        for (uint32_t i = 0; i < w && i < 6; i++)
            rgba[i] = extremes[i];
        std::vector<uint32_t> bgra(rgba.size());
        for (size_t i = 0; i < rgba.size(); i++)
            bgra[i] = (rgba[i] & 0xFF00FF00u) | ((rgba[i] & 0xFFu) << 16) | ((rgba[i] >> 16) & 0xFFu);
        const size_t stride = static_cast<size_t>(w) * 4;
        std::vector<uint8_t> y0(static_cast<size_t>(w) * h), uv0(static_cast<size_t>(w) * h / 2);
        std::vector<uint8_t> y1(y0.size(), 0x77), uv1(uv0.size(), 0x77);
        FrameScaler::PackedToNv12(reinterpret_cast<const uint8_t*>(rgba.data()), stride, w, h, y0.data(), w, uv0.data(), w);
        FrameScaler::PackedToNv12(reinterpret_cast<const uint8_t*>(bgra.data()), stride, w, h, y1.data(), w, uv1.data(), w,
                                  true);
        ASSERT_EQ(y0, y1) << "width " << w;
        ASSERT_EQ(uv0, uv1) << "width " << w;
    }
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
                          {352, 288, 200, 120}, {320, 255, 3840 / 4, 2160 / 4}, {8, 8, 18, 12}, {1, 1, 4, 2},
                          // wide rows for the vector chroma kernel and its tail: odd factors 3 and 5, a fitted 7.2x
                          {43, 11, 130, 34}, {64, 36, 320, 180}, {29, 13, 210, 94}};
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

/// region <PackedToYuv, PlaneScaler>

namespace
{
std::vector<uint8_t> NoiseBytes(size_t count, uint32_t seed)
{
    std::vector<uint8_t> bytes(count);
    uint32_t state = seed;
    for (uint8_t& byte : bytes)
    {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<uint8_t>(state >> 24);
    }
    return bytes;
}

double KeysReference(double x)
{
    const double a = -0.6;
    x = std::fabs(x);
    return x < 1 ? ((a + 2) * x - (a + 3)) * x * x + 1 : x < 2 ? ((a * x - 5 * a) * x + 8 * a) * x - 4 * a : 0;
}

/// The kernel in doubles, edges clamped: the reference of the fixed point
double CubicReference(const std::vector<uint8_t>& plane, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh, uint32_t x,
                      uint32_t y)
{
    const double cx = (x + 0.5) * sw / dw - 0.5, cy = (y + 0.5) * sh / dh - 0.5;
    const int bx = static_cast<int>(std::floor(cx)) - 1, by = static_cast<int>(std::floor(cy)) - 1;
    double sum = 0, wsum = 0;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
        {
            const double w = KeysReference(cx - (bx + i)) * KeysReference(cy - (by + j));
            const int sx = std::clamp(bx + i, 0, static_cast<int>(sw) - 1);
            const int sy = std::clamp(by + j, 0, static_cast<int>(sh) - 1);
            sum += w * plane[static_cast<size_t>(sy) * sw + sx];
            wsum += w;
        }
    return std::clamp(sum / wsum, 0.0, 255.0);
}
}  // namespace

TEST(FrameScaler_Test, PackedToYuv_LumaIsPackedToNv12sAndChromaOfFlatBlocksIsItsToo)
{
    // Every width 2..70 (the vector bodies and their tails); RGBA and the swapped BGRA give the same planes
    for (uint32_t w = 2; w <= 70; w += 2)
    {
        const uint32_t h = 4;
        std::vector<uint8_t> rgba = NoiseBytes(static_cast<size_t>(w) * h * 4, w);
        for (uint32_t y = 0; y < h; y += 2)  // flat 2x2 blocks: their averaged chroma is any pixel's chroma
            for (uint32_t x = 0; x < w; x += 2)
                for (uint32_t dy = 0; dy < 2; dy++)
                    for (uint32_t dx = 0; dx < 2; dx++)
                        std::memcpy(&rgba[(static_cast<size_t>(y + dy) * w + x + dx) * 4],
                                    &rgba[(static_cast<size_t>(y) * w + x) * 4], 4);
        std::vector<uint8_t> bgra = rgba;
        for (size_t p = 0; p < bgra.size(); p += 4)
            std::swap(bgra[p], bgra[p + 2]);
        std::vector<uint8_t> nvY(static_cast<size_t>(w) * h), nvUV(static_cast<size_t>(w) * h / 2);
        FrameScaler::PackedToNv12(rgba.data(), w * 4, w, h, nvY.data(), w, nvUV.data(), w);
        for (const bool swapped : {false, true})
        {
            std::vector<uint8_t> y(static_cast<size_t>(w) * h), u(y.size()), v(y.size());
            FrameScaler::PackedToYuv((swapped ? bgra : rgba).data(), w * 4, w, h, swapped, y.data(), u.data(), v.data(), w);
            ASSERT_EQ(y, nvY) << "width " << w << (swapped ? " bgra" : " rgba");
            for (uint32_t row = 0; row < h; row += 2)
                for (uint32_t x = 0; x < w; x += 2)
                {
                    ASSERT_EQ(u[static_cast<size_t>(row) * w + x], nvUV[static_cast<size_t>(row / 2) * w + x]) << w;
                    ASSERT_EQ(v[static_cast<size_t>(row) * w + x], nvUV[static_cast<size_t>(row / 2) * w + x + 1]) << w;
                }
        }
    }
}

TEST(FrameScaler_Test, PlaneScaler_SameSizeIsACopyAndAFlatPlaneStaysFlat)
{
    const uint32_t w = 37, h = 21;
    const std::vector<uint8_t> plane = NoiseBytes(static_cast<size_t>(w) * h, 3);
    std::vector<uint8_t> out(plane.size());
    FrameScaler::PlaneScaler scaler;
    ASSERT_TRUE(scaler.Prepare(w, h, w, h));
    scaler.Rows(plane.data(), w, out.data(), w, 0, h);
    EXPECT_EQ(out, plane);

    const struct { uint32_t sw, sh, dw, dh; } sizes[] = {{8, 6, 16, 12}, {9, 7, 25, 19}, {40, 30, 13, 11}, {5, 4, 63, 47}};
    for (const auto& s : sizes)
    {
        const std::vector<uint8_t> flat(static_cast<size_t>(s.sw) * s.sh, 77);
        std::vector<uint8_t> scaled(static_cast<size_t>(s.dw) * s.dh);
        ASSERT_TRUE(scaler.Prepare(s.sw, s.sh, s.dw, s.dh));
        scaler.Rows(flat.data(), s.sw, scaled.data(), s.dw, 0, s.dh);
        EXPECT_TRUE(std::all_of(scaled.begin(), scaled.end(), [](uint8_t b) { return b == 77; }))
            << s.sw << "x" << s.sh << " -> " << s.dw << "x" << s.dh;
    }
}

TEST(FrameScaler_Test, PlaneScaler_FixedPointIsWithinOneOfTheKernelInDoubles)
{
    const struct { uint32_t sw, sh, dw, dh; } sizes[] = {{37, 23, 101, 64}, {32, 24, 64, 48}, {50, 40, 31, 17}, {659, 9, 2636, 36}};
    for (const auto& s : sizes)
    {
        const std::vector<uint8_t> plane = NoiseBytes(static_cast<size_t>(s.sw) * s.sh, s.sw);
        std::vector<uint8_t> out(static_cast<size_t>(s.dw) * s.dh);
        FrameScaler::PlaneScaler scaler;
        ASSERT_TRUE(scaler.Prepare(s.sw, s.sh, s.dw, s.dh));
        scaler.Rows(plane.data(), s.sw, out.data(), s.dw, 0, s.dh);
        for (uint32_t y = 0; y < s.dh; y++)
            for (uint32_t x = 0; x < s.dw; x++)
                ASSERT_LE(std::fabs(out[static_cast<size_t>(y) * s.dw + x] - CubicReference(plane, s.sw, s.sh, s.dw, s.dh, x, y)), 1.0)
                    << s.sw << "x" << s.sh << " -> " << s.dw << "x" << s.dh << " at " << x << "," << y;
    }
}

TEST(FrameScaler_Test, PlaneScaler_BandsAndTheInterleavedPairGiveTheBytesOfWholeCalls)
{
    const uint32_t sw = 44, sh = 33, dw = 60, dh = 45, stride = 80;
    const std::vector<uint8_t> u = NoiseBytes(static_cast<size_t>(sw) * sh, 5), v = NoiseBytes(u.size(), 6);
    FrameScaler::PlaneScaler scaler;
    ASSERT_TRUE(scaler.Prepare(sw, sh, dw, dh));
    std::vector<uint8_t> wholeU(static_cast<size_t>(stride) * dh, 0x55), wholeV = wholeU;
    scaler.Rows(u.data(), sw, wholeU.data(), stride, 0, dh);
    scaler.Rows(v.data(), sw, wholeV.data(), stride, 0, dh);
    std::vector<uint8_t> bands(static_cast<size_t>(stride) * dh, 0x55);
    for (uint32_t first = 0; first < dh; first += 7)
        scaler.Rows(u.data(), sw, bands.data(), stride, first, first + 7);
    EXPECT_EQ(bands, wholeU);
    EXPECT_EQ(bands[dw], 0x55) << "nothing written past the target's width";

    std::vector<uint8_t> pairs(static_cast<size_t>(dw) * 2 * dh);
    for (uint32_t first = 0; first < dh; first += 11)
        scaler.RowsInterleaved(u.data(), v.data(), sw, pairs.data(), dw * 2, first, first + 11);
    for (uint32_t y = 0; y < dh; y++)
        for (uint32_t x = 0; x < dw; x++)
        {
            ASSERT_EQ(pairs[(static_cast<size_t>(y) * dw + x) * 2], wholeU[static_cast<size_t>(y) * stride + x]);
            ASSERT_EQ(pairs[(static_cast<size_t>(y) * dw + x) * 2 + 1], wholeV[static_cast<size_t>(y) * stride + x]);
        }
}

namespace
{
/// The scaler's fixed point written out plainly (weights, folding, both passes): the SIMD bodies must give its bytes
struct FixedAxis
{
    std::vector<int> first, weights;
};

FixedAxis FixedPointAxis(uint32_t src, uint32_t dst)
{
    FixedAxis axis;
    const int last = static_cast<int>(src) - 1;
    for (uint32_t i = 0; i < dst; i++)
    {
        const double centre = (i + 0.5) * src / dst - 0.5;
        const int base = static_cast<int>(std::floor(centre)) - 1;
        double raw[4], sum = 0;
        for (int k = 0; k < 4; k++)
            sum += raw[k] = KeysReference(centre - (base + k));
        int weight[4], total = 0;
        for (int k = 0; k < 4; k++)
            total += weight[k] = static_cast<int>(std::lround(raw[k] / sum * 16384));
        weight[1] += 16384 - total;
        const int low = std::clamp(base, 0, last - 3);
        int folded[4] = {0, 0, 0, 0};
        for (int k = 0; k < 4; k++)
            folded[std::clamp(base + k, 0, last) - low] += weight[k];
        axis.first.push_back(low);
        axis.weights.insert(axis.weights.end(), folded, folded + 4);
    }
    return axis;
}

std::vector<uint8_t> FixedPointScale(const std::vector<uint8_t>& plane, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh)
{
    const FixedAxis ax = FixedPointAxis(sw, dw), ay = FixedPointAxis(sh, dh);
    std::vector<int> mid(static_cast<size_t>(sh) * dw);
    for (uint32_t y = 0; y < sh; y++)
        for (uint32_t x = 0; x < dw; x++)
        {
            const uint8_t* p = &plane[static_cast<size_t>(y) * sw + ax.first[x]];
            const int* w = &ax.weights[static_cast<size_t>(x) * 4];
            mid[static_cast<size_t>(y) * dw + x] = (p[0] * w[0] + p[1] * w[1] + p[2] * w[2] + p[3] * w[3] + 128) >> 8;
        }
    std::vector<uint8_t> out(static_cast<size_t>(dw) * dh);
    for (uint32_t y = 0; y < dh; y++)
        for (uint32_t x = 0; x < dw; x++)
        {
            const int* w = &ay.weights[static_cast<size_t>(y) * 4];
            int v = 0;
            for (int k = 0; k < 4; k++)
                v += mid[static_cast<size_t>(ay.first[y] + k) * dw + x] * w[k];
            out[static_cast<size_t>(y) * dw + x] = static_cast<uint8_t>(std::clamp((v + (1 << 19)) >> 20, 0, 255));
        }
    return out;
}
}  // namespace

TEST(FrameScaler_Test, PlaneScaler_VectorBodiesGiveTheFixedPointBytesAtEveryWidth)
{
    // Every output width 1..40 (the 4- and 8-wide bodies and their tails), up and down; noise, and a 0/255
    // checkerboard whose cubic overshoot must clamp alike
    for (uint32_t dw = 1; dw <= 40; dw++)
        for (const uint32_t sw : {4u, 7u, 13u, 64u})
            for (const bool checker : {false, true})
            {
                const uint32_t sh = 9, dh = 23;
                std::vector<uint8_t> u = NoiseBytes(static_cast<size_t>(sw) * sh, dw * 100 + sw), v = NoiseBytes(u.size(), dw);
                if (checker)
                    for (size_t i = 0; i < u.size(); i++)
                        u[i] = ((i % sw + i / sw) & 1) ? 255 : 0;
                FrameScaler::PlaneScaler scaler;
                ASSERT_TRUE(scaler.Prepare(sw, sh, dw, dh));
                std::vector<uint8_t> out(static_cast<size_t>(dw) * dh), pairs(out.size() * 2);
                scaler.Rows(u.data(), sw, out.data(), dw, 0, dh);
                const std::vector<uint8_t> expectU = FixedPointScale(u, sw, sh, dw, dh);
                const std::vector<uint8_t> expectV = FixedPointScale(v, sw, sh, dw, dh);
                ASSERT_EQ(out, expectU) << sw << " -> " << dw << (checker ? " checker" : " noise");
                scaler.RowsInterleaved(u.data(), v.data(), sw, pairs.data(), dw * 2, 0, dh);
                for (size_t i = 0; i < out.size(); i++)
                {
                    ASSERT_EQ(pairs[i * 2], expectU[i]) << sw << " -> " << dw << " at " << i;
                    ASSERT_EQ(pairs[i * 2 + 1], expectV[i]) << sw << " -> " << dw << " at " << i;
                }
            }
}

TEST(FrameScaler_Test, PlaneScaler_ASourceUnderFourSamplesOrAZeroSizeIsRefused)
{
    FrameScaler::PlaneScaler scaler;
    EXPECT_FALSE(scaler.Prepare(3, 10, 20, 20));
    EXPECT_FALSE(scaler.Prepare(10, 3, 20, 20));
    EXPECT_FALSE(scaler.Prepare(10, 10, 0, 20));
    EXPECT_TRUE(scaler.Prepare(4, 4, 1, 1));
}

/// endregion </PackedToYuv, PlaneScaler>
