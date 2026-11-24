#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <vector>

#include "emulator/video/smoothlite.h"
#include "emulator/video/smoothmotion.h"
#include "emulator/video/smoothscenes.h"

/// LiteFlow: smooth motion's cheap interpolation (POC 055). Synthetic pictures, no emulator. That its pictures are
/// the POC's lite19 byte for byte is checked by the POC's own binary (its `core` filter against `lite19`)

using namespace smoothmotion;

namespace
{
constexpr uint32_t BLACK = 0xFF000000;
constexpr uint32_t WHITE = 0xFFFFFFFF;
constexpr int W = 128;
constexpr int H = 64;

uint32_t PatternAt(int x, int y)
{
    uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    static const uint32_t colours[4] = {BLACK, WHITE, 0xFF0000D7, 0xFFD7D700};
    return colours[h & 3];
}

/// A band of pattern (rows 16..47) scrolled by `offset` pixels to the left, black around it
std::vector<uint32_t> Scroller(int offset)
{
    std::vector<uint32_t> frame(static_cast<size_t>(W) * H, BLACK);
    for (int y = 16; y < 48; y++)
        for (int x = 0; x < W; x++)
            frame[static_cast<size_t>(y) * W + x] = PatternAt(x + offset, y);
    return frame;
}

Picture View(const std::vector<uint32_t>& frame, uint64_t id, int width = W, int height = H)
{
    return {width, height, frame.data(), id};
}

uint64_t Fnv(const std::vector<uint32_t>& pixels, uint64_t h = 1469598103934665603ull)
{
    for (uint32_t p : pixels)
        for (int c = 0; c < 32; c += 8)
        {
            h ^= (p >> c) & 0xFF;
            h *= 1099511628211ull;
        }
    return h;
}
}  // namespace

TEST(SmoothLite_Test, FitsWholeBlocksUpToItsWidth)
{
    EXPECT_TRUE(LiteFlow::Fits(352, 288));
    EXPECT_TRUE(LiteFlow::Fits(640, 480));
    EXPECT_TRUE(LiteFlow::Fits(8, 8));
    EXPECT_FALSE(LiteFlow::Fits(350, 288));
    EXPECT_FALSE(LiteFlow::Fits(352, 284));
    EXPECT_FALSE(LiteFlow::Fits(LiteFlow::kMaxWidth + 8, 288));
    EXPECT_FALSE(LiteFlow::Fits(0, 0));

    LiteFlow flow;
    const std::vector<uint32_t> odd(static_cast<size_t>(100) * 60, BLACK);
    std::vector<uint32_t> out(odd.size(), WHITE);
    EXPECT_FALSE(flow.Compose(View(odd, 1, 100, 60), View(odd, 2, 100, 60), 128, out.data()));
    EXPECT_EQ(out[0], WHITE);  // untouched
    EXPECT_EQ(flow.GetStats().refused, 1u);
}

TEST(SmoothLite_Test, AStillPictureStaysAsItIs)
{
    LiteFlow flow;
    const std::vector<uint32_t> a = Scroller(0);
    std::vector<uint32_t> out(a.size());
    for (int w : {0, 1, 128, 255, kOne})
    {
        ASSERT_TRUE(flow.Compose(View(a, 1), View(a, 2), w, out.data()));
        EXPECT_EQ(out, a) << "w " << w;
    }
}

TEST(SmoothLite_Test, AScrollIsDrawnHalfWay)
{
    // The field is kept from pair to pair: the scroll's vector is found by the search within a few pairs, then every
    // pair has it at once
    LiteFlow flow;
    std::vector<uint32_t> out(static_cast<size_t>(W) * H);
    int exactFrom = -1;
    for (int k = 0; k < 8; k++)
    {
        const std::vector<uint32_t> a = Scroller(4 * k), b = Scroller(4 * k + 4);
        ASSERT_TRUE(flow.Compose(View(a, k + 1), View(b, k + 2), kOne / 2, out.data()));
        const std::vector<uint32_t> want = Scroller(4 * k + 2);
        bool exact = true;
        for (int y = 0; y < H && exact; y++)
            for (int x = 16; x < W - 16 && exact; x++)
                exact = out[static_cast<size_t>(y) * W + x] == want[static_cast<size_t>(y) * W + x];
        if (exact && exactFrom < 0)
            exactFrom = k;
        if (exactFrom >= 0)
            EXPECT_TRUE(exact) << "pair " << k << " lost the scroll found at pair " << exactFrom;
    }
    EXPECT_GE(exactFrom, 0);
    EXPECT_LE(exactFrom, 3) << "the scroll's vector was found too late";
    // Outside the band nothing moves
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < W; x++)
            ASSERT_EQ(out[static_cast<size_t>(y) * W + x], BLACK);
}

TEST(SmoothLite_Test, TheEndsOfThePairAreTheFrames)
{
    LiteFlow flow;
    std::vector<uint32_t> out(static_cast<size_t>(W) * H);
    for (int k = 0; k < 6; k++)
    {
        const std::vector<uint32_t> a = Scroller(4 * k), b = Scroller(4 * k + 4);
        ASSERT_TRUE(flow.Compose(View(a, k + 1), View(b, k + 2), 0, out.data()));
        EXPECT_EQ(out, a) << "pair " << k;
        ASSERT_TRUE(flow.Compose(View(a, k + 1), View(b, k + 2), kOne, out.data()));
        // At B's weight the picture is B wherever a vector or stillness explains it; the columns coming in at the
        // edges are the nearer frame, B again
        EXPECT_EQ(out, b) << "pair " << k;
    }
}

TEST(SmoothLite_Test, ColourChangingInPlaceIsNotMoved)
{
    // A still block of pattern whose ink changes colour, beside a scroll: the scroll's vector fits pixels of the
    // block by chance and explains none of its changes
    LiteFlow flow;
    const auto frame = [](int k) {
        std::vector<uint32_t> f = Scroller(4 * k);
        const uint32_t ink = (k & 1) ? 0xFF00D700 : 0xFFD70000;
        for (int y = 0; y < 8; y++)
            for (int x = 40; x < 72; x++)
                f[static_cast<size_t>(y) * W + x] = (PatternAt(x, y) == BLACK) ? BLACK : ink;
        return f;
    };
    std::vector<uint32_t> out(static_cast<size_t>(W) * H);
    for (int k = 0; k < 8; k++)
    {
        const std::vector<uint32_t> a = frame(k), b = frame(k + 1);
        ASSERT_TRUE(flow.Compose(View(a, k + 1), View(b, k + 2), 64, out.data()));
        for (int y = 0; y < 8; y++)
            for (int x = 40; x < 72; x++)
                ASSERT_EQ(out[static_cast<size_t>(y) * W + x], a[static_cast<size_t>(y) * W + x])
                    << "pair " << k << " at " << x << "," << y;
    }
}

TEST(SmoothLite_Test, MoreThan256ColoursIsNotItsPicture)
{
    LiteFlow flow;
    std::vector<uint32_t> many(static_cast<size_t>(W) * H);
    for (size_t i = 0; i < many.size(); i++)
        many[i] = 0xFF000000u | static_cast<uint32_t>(i % 300) * 0x010203u;
    std::vector<uint32_t> out(many.size(), WHITE);
    EXPECT_FALSE(flow.Compose(View(many, 1), View(many, 2), 128, out.data()));
    EXPECT_EQ(out[5], WHITE);
    EXPECT_EQ(flow.GetStats().refused, 1u);
    // ... and the next pair of few colours is drawn
    const std::vector<uint32_t> a = Scroller(0), b = Scroller(4);
    EXPECT_TRUE(flow.Compose(View(a, 3), View(b, 4), 128, out.data()));
}

TEST(SmoothLite_Test, ThePaletteStartsOverWhenItFills)
{
    // 16 colours a frame, new ones every frame: over 256 in all within 20 frames
    LiteFlow flow;
    const auto frame = [](int k) {
        std::vector<uint32_t> f(static_cast<size_t>(W) * H);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                f[static_cast<size_t>(y) * W + x] =
                    0xFF000000u | static_cast<uint32_t>(k * 16 + ((x / 8 + y / 8) & 15)) * 0x000101u;
        return f;
    };
    std::vector<uint32_t> out(static_cast<size_t>(W) * H);
    for (int k = 0; k < 24; k++)
    {
        const std::vector<uint32_t> a = frame(k), b = frame(k + 1);
        ASSERT_TRUE(flow.Compose(View(a, k + 1), View(b, k + 2), 0, out.data())) << "pair " << k;
        ASSERT_EQ(out, a) << "pair " << k;
    }
    EXPECT_GE(flow.GetStats().palettes, 1u);
    EXPECT_EQ(flow.GetStats().refused, 0u);
}

TEST(SmoothLite_Test, APairIsEstimatedOnceByItsIds)
{
    const std::vector<uint32_t> a = Scroller(0), b = Scroller(4);
    std::vector<uint32_t> out(a.size());
    LiteFlow byId;
    for (int w : {64, 128, 192})
        ASSERT_TRUE(byId.Compose(View(a, 1), View(b, 2), w, out.data()));
    EXPECT_EQ(byId.GetStats().pairs, 1u);
    EXPECT_EQ(byId.GetStats().packed, 2u);
    // No ids: every call is a new pair
    LiteFlow unknown;
    for (int w : {64, 128, 192})
        ASSERT_TRUE(unknown.Compose(View(a, 0), View(b, 0), w, out.data()));
    EXPECT_EQ(unknown.GetStats().pairs, 3u);
    // Reset forgets the frames
    byId.Reset();
    ASSERT_TRUE(byId.Compose(View(a, 1), View(b, 2), 64, out.data()));
    EXPECT_EQ(byId.GetStats().pairs, 2u);
}

TEST(SmoothLite_Test, ThePicturesAreTheSameEverywhere)
{
    // Integers only: one sum on every platform and compiler. A change of the algorithm changes it: then the POC's
    // suite (tools/poc/055-interpolation-low-end/score.sh) says whether for the better
    LiteFlow flow;
    const int width = 96, height = 64;
    std::vector<uint32_t> out(static_cast<size_t>(width) * height);
    uint64_t sum = 1469598103934665603ull;
    for (int k = 0; k < 6; k++)
    {
        const std::vector<uint32_t> a = smoothscenes::Dots(width, height, k, false);
        const std::vector<uint32_t> b = smoothscenes::Dots(width, height, k + 1, false);
        for (int w : {77, 180})
        {
            ASSERT_TRUE(flow.Compose(View(a, k + 1, width, height), View(b, k + 2, width, height), w, out.data()));
            sum = Fnv(out, sum);
        }
    }
    EXPECT_EQ(sum, 0x43436730d6816f33ull) << std::hex << sum;
}

TEST(SmoothLite_Test, TheEngineDrawsInterpolateLiteWithIt)
{
    Mode mode = Mode::Off;
    ASSERT_TRUE(ParseMode("interpolate-lite", mode));
    EXPECT_EQ(mode, Mode::InterpolateLite);
    EXPECT_STREQ(ModeName(Mode::InterpolateLite), "interpolate-lite");

    constexpr int64_t MS = 1'000'000, PERIOD = 20 * MS;
    const auto run = [&](Mode engineMode, bool stepDown) {
        SmoothMotion engine;
        engine.SetMode(engineMode);
        engine.SetLite(stepDown);
        std::vector<uint32_t> out(static_cast<size_t>(W) * H);
        int64_t t = 1000 * MS;
        for (uint64_t serial = 1; serial <= 6; serial++, t += PERIOD)
        {
            const std::vector<uint32_t> frame = Scroller(4 * static_cast<int>(serial));
            engine.Push(serial, t, frame.data(), W, H);
            if (serial >= 2)
            {
                const auto r = engine.Render(t - PERIOD / 2, out.data());
                EXPECT_EQ(r.kind, SmoothMotion::Kind::Interpolate);
            }
        }
        return engine.GetStats();
    };
    const auto lite = run(Mode::InterpolateLite, false);
    EXPECT_EQ(lite.lite, lite.interpolated);
    EXPECT_GT(lite.lite, 0u);
    EXPECT_EQ(lite.estimates, 0u);  // no field of the reference's
    const auto stepped = run(Mode::Interpolate, true);
    EXPECT_EQ(stepped.lite, stepped.interpolated);
    const auto reference = run(Mode::Interpolate, false);
    EXPECT_EQ(reference.lite, 0u);
    EXPECT_GT(reference.estimates, 0u);
}
