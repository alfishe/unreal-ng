#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
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

constexpr uint32_t BOX = 0xFF00D7D7;

/// The colour of bar `bar` (0..15) in frame `k`: `fading`, a colour of its own every frame (a palette fade: new
/// colours every frame); otherwise one of three a bar, in turn (any three frames in a row differ there as well)
uint32_t BarColour(int bar, int k, bool fading)
{
    if (fading)
        return 0xFD000000u | ((static_cast<uint32_t>(k * 16 + bar) + 1) * 0x9E3779u & 0xFFFFFFu);
    return 0xFE000000u | static_cast<uint32_t>(bar * 3 + k % 3 + 1);
}

/// The scroller, 16 bars of colour changing in place above it (rows 0..15), and a box of one colour 24 x 16 moving 2
/// pixels right a frame below it (rows 48..63): no block of the box has detail, so the frame before a confirms its
/// motion
std::vector<uint32_t> Busy(int k, bool fading)
{
    std::vector<uint32_t> frame = Scroller(4 * k);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < W; x++)
            frame[static_cast<size_t>(y) * W + x] = BarColour(x / 8, k, fading);
    for (int y = 48; y < 64; y++)
        for (int x = 4 + 2 * k; x < 28 + 2 * k; x++)
            frame[static_cast<size_t>(y) * W + x] = BOX;
    return frame;
}

/// The pictures of frames first..last of `frames`, pair after pair at weight `w` (ids k + 1 + idBase)
std::vector<std::vector<uint32_t>> Stream(LiteFlow& flow, const std::function<std::vector<uint32_t>(int)>& frames,
                                          int first, int last, int w, uint64_t idBase = 0)
{
    std::vector<std::vector<uint32_t>> pictures;
    for (int k = first; k < last; k++)
    {
        const std::vector<uint32_t> a = frames(k), b = frames(k + 1);
        std::vector<uint32_t> out(a.size());
        EXPECT_TRUE(flow.Compose(View(a, idBase + k + 1), View(b, idBase + k + 2), w, out.data())) << "pair " << k;
        pictures.push_back(std::move(out));
    }
    return pictures;
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

TEST(SmoothLite_Test, ThePaletteIsCompactedWhenItFills)
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
    EXPECT_EQ(flow.GetStats().packed, 25u);  // every frame once: the frames kept stay over a compaction
}

TEST(SmoothLite_Test, ThePicturesDoNotDependOnTheColoursIndices)
{
    // The planes ask only which pixels are equal: a stream drawn with its colours met in another order (a palette
    // of 250 colours left by a picture of another size, compacted later in the stream) is the same picture, and
    // its colours mapped one to one are the pictures mapped
    LiteFlow fresh;
    const auto plain = [](int k) { return Busy(k, false); };
    const std::vector<std::vector<uint32_t>> want = Stream(fresh, plain, 0, 8, 100);

    const auto map = [](uint32_t c) { return c ^ 0x00FFFFFFu; };
    const auto mapped = [&](int k) {
        std::vector<uint32_t> f = Busy(k, false);
        for (uint32_t& p : f)
            p = map(p);
        return f;
    };
    // the stream's colours last first, then others to 250
    std::vector<uint32_t> colours;
    for (int k = 2; k >= 0; k--)
        for (int bar = 15; bar >= 0; bar--)
            colours.push_back(map(BarColour(bar, k, false)));
    for (uint32_t c : {BOX, 0xFFD7D700u, 0xFF0000D7u, WHITE, BLACK})
        colours.push_back(map(c));
    for (uint32_t i = 0; colours.size() < 250; i++)
        colours.push_back(0xFC000000u | i);
    std::vector<uint32_t> other(static_cast<size_t>(64) * 32);
    for (size_t p = 0; p < other.size(); p++)
        other[p] = colours[p % colours.size()];
    LiteFlow used;
    std::vector<uint32_t> out(other.size());
    ASSERT_TRUE(used.Compose(View(other, 1000, 64, 32), View(other, 1001, 64, 32), 100, out.data()));
    const std::vector<std::vector<uint32_t>> got = Stream(used, mapped, 0, 8, 100);
    EXPECT_GE(used.GetStats().palettes, 1u) << "the stream should have compacted the palette";
    for (size_t pair = 0; pair < want.size(); pair++)
    {
        int differ = 0;
        for (size_t p = 0; p < want[pair].size(); p++)
            differ += got[pair][p] != map(want[pair][p]);
        EXPECT_EQ(differ, 0) << "pair " << pair;
    }
}

// Was a defect: the palette only grew, and when it filled, it started over with the pair's two frames: the frame
// before a was dropped with it, so the box's motion went unconfirmed (no detail) at the pair after, a pop every
// 16 frames of a fade. Now the palette is compacted to the colours of the frames kept
TEST(SmoothLite_Test, AFadingPaletteDrawsAsASteadyOne)
{
    // 16 bars a new colour every frame against the same bars in three colours each, in turn: the same pixels equal
    // in every frame, so the same pictures but for the bars' colours
    constexpr int kFrames = 20;
    LiteFlow steady, fading;
    const std::vector<std::vector<uint32_t>> want = Stream(steady, [](int k) { return Busy(k, false); }, 0, kFrames, 100);
    const std::vector<std::vector<uint32_t>> got = Stream(fading, [](int k) { return Busy(k, true); }, 0, kFrames, 100);
    EXPECT_GE(fading.GetStats().palettes, 1u);
    for (int k = 0; k + 1 < kFrames; k++)
    {
        std::unordered_map<uint32_t, uint32_t> map;
        for (int bar = 0; bar < 16; bar++)
            for (int f : {k, k + 1})
                map[BarColour(bar, f, false)] = BarColour(bar, f, true);
        int differ = 0, firstAt = -1;
        for (size_t p = 0; p < want[static_cast<size_t>(k)].size(); p++)
        {
            const uint32_t steadyPixel = want[static_cast<size_t>(k)][p];
            const auto it = map.find(steadyPixel);
            if (got[static_cast<size_t>(k)][p] != (it == map.end() ? steadyPixel : it->second) && differ++ == 0)
                firstAt = static_cast<int>(p);
        }
        EXPECT_EQ(differ, 0) << "pair " << k << ", first at " << firstAt % W << "," << firstAt / W;
    }
}

// Was a defect: a pair of over 256 colours was packed again at every refresh, and each try started the palette
// over, dropping the frames kept for the next pair
TEST(SmoothLite_Test, ARefusedPairIsNotTriedAgain)
{
    LiteFlow flow;
    const std::vector<uint32_t> a = Scroller(0), b = Scroller(4);
    std::vector<uint32_t> out(a.size());
    ASSERT_TRUE(flow.Compose(View(a, 1), View(b, 2), 128, out.data()));
    const std::vector<uint32_t> first = out;
    std::vector<uint32_t> many(a.size());
    for (size_t i = 0; i < many.size(); i++)
        many[i] = 0xFF000000u | static_cast<uint32_t>(i % 300) * 0x010203u;
    EXPECT_FALSE(flow.Compose(View(many, 3), View(many, 4), 128, out.data()));
    const LiteFlow::Stats once = flow.GetStats();
    EXPECT_FALSE(flow.Compose(View(many, 3), View(many, 4), 171, out.data()));
    EXPECT_EQ(flow.GetStats().refused, 2u);
    EXPECT_EQ(flow.GetStats().palettes, once.palettes) << "the refused pair was tried again";
    // the good pair is still there: not estimated, not packed again
    ASSERT_TRUE(flow.Compose(View(a, 1), View(b, 2), 128, out.data()));
    EXPECT_EQ(out, first);
    EXPECT_EQ(flow.GetStats().pairs, 1u);
    EXPECT_EQ(flow.GetStats().packed, 2u);
}

TEST(SmoothLite_Test, ResetAndAnotherSizeStartFresh)
{
    const auto busy = [](int k) { return Busy(k, false); };
    const auto dots = [](int k) { return smoothscenes::Dots(96, 48, k, false); };
    LiteFlow fresh;
    const std::vector<std::vector<uint32_t>> want = Stream(fresh, busy, 0, 6, 77);
    // another stream first, then Reset
    LiteFlow reset;
    Stream(reset, [](int k) { return Scroller(3 * k + 1); }, 0, 5, 128);
    reset.Reset();
    EXPECT_EQ(Stream(reset, busy, 0, 6, 77), want);
    // another size first: its field and frames are no use to this one (the palette's colours do not matter)
    LiteFlow resized;
    for (int k = 0; k < 4; k++)
    {
        const std::vector<uint32_t> a = dots(k), b = dots(k + 1);
        std::vector<uint32_t> out(a.size());
        ASSERT_TRUE(resized.Compose(View(a, 100 + k, 96, 48), View(b, 101 + k, 96, 48), 128, out.data()));
    }
    EXPECT_EQ(Stream(resized, busy, 0, 6, 77), want);
}

// Was a defect: only the last pair was kept, so a pair asked again after another was estimated again from the field
// as it had moved on since: other bits for the same frames and weight
TEST(SmoothLite_Test, APairAskedAgainIsTheSamePicture)
{
    const auto busy = [](int k) { return Busy(k, false); };
    const auto at = [&](LiteFlow& flow, int k, int w) {
        const std::vector<uint32_t> a = busy(k), b = busy(k + 1);
        std::vector<uint32_t> out(a.size());
        EXPECT_TRUE(flow.Compose(View(a, k + 1), View(b, k + 2), w, out.data()));
        return out;
    };
    // (a, b), then (k, a), then (a, b) again
    LiteFlow flow;
    const std::vector<uint32_t> ab = at(flow, 1, 100);
    const std::vector<uint32_t> ka = at(flow, 0, 100);
    EXPECT_EQ(at(flow, 1, 100), ab);
    EXPECT_EQ(at(flow, 0, 100), ka);
    // in order, then a step back and on again
    LiteFlow inOrder;
    std::vector<std::vector<uint32_t>> first;
    for (int k = 0; k < 4; k++)
        first.push_back(at(inOrder, k, 171));
    EXPECT_EQ(at(inOrder, 2, 171), first[2]);
    EXPECT_EQ(at(inOrder, 3, 171), first[3]);
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
