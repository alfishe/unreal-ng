#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "emulator/video/smoothlayers.h"
#include "emulator/video/smoothmotion.h"

/// SmoothMotion: the CPU reference of the blend, the interpolation and the game cadence modes
/// (docs/inprogress/2026-10-26-frame-presenter, sections 4.3-4.5). Synthetic pictures, no emulator

using namespace smoothmotion;

namespace
{
constexpr uint32_t BLACK = 0xFF000000;
constexpr uint32_t WHITE = 0xFFFFFFFF;
constexpr int64_t MS = 1'000'000;
constexpr int64_t PERIOD = 20 * MS;

struct Image
{
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
    Image(int w, int h, uint32_t fill = BLACK) : width(w), height(h), pixels(static_cast<size_t>(w) * h, fill) {}
    uint32_t& At(int x, int y) { return pixels[static_cast<size_t>(y) * width + x]; }
    uint32_t At(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
    Picture View() const { return {width, height, pixels.data()}; }
};

/// A deterministic pattern of ZX-like colours: what a text scroller or a sprite is made of
uint32_t PatternAt(int x, int y)
{
    uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    static const uint32_t colours[4] = {BLACK, WHITE, 0xFF0000D7, 0xFFD7D700};
    return colours[h & 3];
}

/// A band of pattern scrolled by `offset` pixels to the left, black around it
Image Scroller(int width, int height, int offset, int bandY, int bandHeight)
{
    Image img(width, height);
    for (int y = bandY; y < bandY + bandHeight; y++)
        for (int x = 0; x < width; x++)
            img.At(x, y) = PatternAt(x + offset, y);
    return img;
}

/// A 16x16 sprite of pattern at (sx, sy) over a static dark pattern (a periodic one is a known limit: smoothmotion.h)
Image Sprite(int width, int height, int sx, int sy)
{
    Image img(width, height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            img.At(x, y) = (PatternAt(x + 3000, y + 3000) & 0xFF3F3F3F) | 0xFF000000;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            img.At(sx + x, sy + y) = PatternAt(x + 1000, y + 1000);
    return img;
}

uint64_t Hash(const std::vector<uint32_t>& pixels)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t p : pixels)
        for (int c = 0; c < 32; c += 8)
        {
            h ^= (p >> c) & 0xFF;
            h *= 1099511628211ull;
        }
    return h;
}
}  // namespace

TEST(SmoothMotion_Test, MixAndWeightAreExactIntegers)
{
    EXPECT_EQ(Mix(BLACK, WHITE, 0), BLACK);
    EXPECT_EQ(Mix(BLACK, WHITE, kOne), WHITE);
    EXPECT_EQ(Mix(BLACK, WHITE, 128), 0xFF808080u);  // (255 * 128 + 128) >> 8 = 128
    EXPECT_EQ(Mix(BLACK, WHITE, 64), 0xFF404040u);   // (255 * 64 + 128) >> 8 = 64
    EXPECT_EQ(Mix(0xFF0000D7, 0xFF0000D7, 77), 0xFF0000D7u);

    EXPECT_EQ(WeightAt(0, 0, PERIOD), 0);
    EXPECT_EQ(WeightAt(PERIOD / 2, 0, PERIOD), 128);
    EXPECT_EQ(WeightAt(PERIOD, 0, PERIOD), kOne);
    EXPECT_EQ(WeightAt(-5, 0, PERIOD), 0);
    EXPECT_EQ(WeightAt(PERIOD * 2, 0, PERIOD), kOne);
    // Rounded: 1/3 of 256 = 85.33
    EXPECT_EQ(WeightAt(PERIOD / 3, 0, PERIOD), 85);
}

TEST(SmoothMotion_Test, SearchOrderStartsAtZeroAndGrowsByLength)
{
    const auto& order = MotionField::SearchOrder(2);
    ASSERT_EQ(order.size(), 25u);
    EXPECT_EQ(order[0], (MotionField::Vector{0, 0}));
    // Length 1, row by row: (0,-1), (-1,0), (1,0), (0,1)
    EXPECT_EQ(order[1], (MotionField::Vector{0, -1}));
    EXPECT_EQ(order[2], (MotionField::Vector{-1, 0}));
    EXPECT_EQ(order[3], (MotionField::Vector{1, 0}));
    EXPECT_EQ(order[4], (MotionField::Vector{0, 1}));
    for (size_t i = 1; i < order.size(); i++)
        EXPECT_LE(order[i - 1].x * order[i - 1].x + order[i - 1].y * order[i - 1].y,
                  order[i].x * order[i].x + order[i].y * order[i].y);
}

TEST(SmoothMotion_Test, ScrollerIsInterpolatedToTheMiddle)
{
    // The band moves 4 px left per frame: at w = 1/2 it is 2 px on, exactly, away from the edges
    const Image a = Scroller(96, 64, 0, 16, 32);
    const Image b = Scroller(96, 64, 4, 16, 32);
    const Image truth = Scroller(96, 64, 2, 16, 32);
    MotionField field;
    field.Estimate(a.View(), b.View(), MotionField::kRangeFrames);
    EXPECT_GT(field.GetStats().moved, 0);
    Image out(96, 64);
    field.Compose(a.View(), b.View(), 128, out.pixels.data());

    int exact = 0, total = 0;
    for (int y = 16; y < 48; y++)
        for (int x = 8; x < 88; x++)
        {
            total++;
            exact += out.At(x, y) == truth.At(x, y);
        }
    EXPECT_EQ(exact, total);
    // Black above and below stays black
    EXPECT_EQ(out.At(10, 4), BLACK);
    EXPECT_EQ(out.At(10, 60), BLACK);
}

TEST(SmoothMotion_Test, SpriteMovesOverAStaticBackground)
{
    // (20, 20) -> (26, 24): at w = 1/2 at (23, 22); the background where the sprite was comes back
    const Image a = Sprite(96, 64, 20, 20);
    const Image b = Sprite(96, 64, 26, 24);
    const Image truth = Sprite(96, 64, 23, 22);
    MotionField field;
    field.Estimate(a.View(), b.View(), MotionField::kRangeFrames);
    Image out(96, 64);
    field.Compose(a.View(), b.View(), 128, out.pixels.data());
    Image blend(96, 64);
    Blend(a.View(), b.View(), 128, blend.pixels.data());
    int wrong = 0, blendWrong = 0;
    for (size_t p = 0; p < out.pixels.size(); p++)
    {
        wrong += out.pixels[p] != truth.pixels[p];
        blendWrong += blend.pixels[p] != truth.pixels[p];
    }
    // Wrong only at the sprite's edges, where the background it uncovers matches no candidate (design section 4.4:
    // the blend there); far fewer than the blend gets wrong
    EXPECT_LT(wrong, blendWrong / 2) << "blend " << blendWrong;
    // The sprite's middle is exact
    for (int y = 4; y < 12; y++)
        for (int x = 4; x < 12; x++)
            EXPECT_EQ(out.At(23 + x, 22 + y), truth.At(23 + x, 22 + y)) << x << "," << y;
}

namespace
{
/// LayerFlow's picture of p, a, b at the weight w, the fields estimated as the engine does
Image Layers(const Image& p, const Image& a, const Image& b, int w)
{
    MotionField prev, next;
    prev.Estimate(p.View(), a.View(), MotionField::kRangeFrames);
    next.Estimate(a.View(), b.View(), MotionField::kRangeFrames);
    Image out(a.width, a.height);
    LayerFlow().Compose(p.View(), a.View(), b.View(), prev, next, w, out.pixels.data());
    return out;
}

int Wrong(const Image& out, const Image& truth)
{
    int wrong = 0;
    for (size_t q = 0; q < out.pixels.size(); q++)
        wrong += out.pixels[q] != truth.pixels[q];
    return wrong;
}
}  // namespace

TEST(SmoothMotion_Test, LayersMoveTheScrollerToTheMiddle)
{
    // Exact away from the edges, where the band's content comes in from outside
    const Image out = Layers(Scroller(96, 64, -4, 16, 32), Scroller(96, 64, 0, 16, 32), Scroller(96, 64, 4, 16, 32), 128);
    const Image truth = Scroller(96, 64, 2, 16, 32);
    for (int y = 0; y < 64; y++)
        for (int x = 8; x < 88; x++)
            ASSERT_EQ(out.At(x, y), truth.At(x, y)) << x << "," << y;
}

TEST(SmoothMotion_Test, LayersMoveASpriteOverPaper)
{
    // (14, 16) -> (20, 20) -> (26, 24) over black paper: at w = 1/2 at (23, 22), exact. Known limit: over random
    // background of few colours the pixels the sprite left between k - 1 and k are not still, and moving vectors fit
    // the noise by chance there (smoothlayers.h)
    const auto sprite = [](int sx, int sy) {
        Image img(96, 64);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                img.At(sx + x, sy + y) = PatternAt(x + 1000, y + 1000);
        return img;
    };
    EXPECT_EQ(Wrong(Layers(sprite(14, 16), sprite(20, 20), sprite(26, 24), 128), sprite(23, 22)), 0);
}

TEST(SmoothMotion_Test, LayersKeepAStillPictureAndUnrelatedChanges)
{
    // Nothing moves: a; a line recoloured fits no layer: the nearer frame
    const Image a = Sprite(64, 32, 8, 8);
    Image b = a;
    for (int x = 0; x < 64; x++)
        b.At(x, 30) = PatternAt(x, 3);
    EXPECT_EQ(Wrong(Layers(a, a, b, 100), a), 0);
    EXPECT_EQ(Wrong(Layers(a, a, b, 200), b), 0);
}

TEST(SmoothMotion_Test, NoMotionFoundIsTheBlend)
{
    // Unrelated noise: no block matches, the output is the blend everywhere
    Image a(64, 32), b(64, 32);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 64; x++)
        {
            a.At(x, y) = PatternAt(x, y);
            b.At(x, y) = PatternAt(x + 5000, y + 7000);
        }
    MotionField field;
    field.Estimate(a.View(), b.View(), MotionField::kRangeFrames);
    EXPECT_EQ(field.GetStats().unmatched, field.GetStats().blocks);
    Image out(64, 32), blend(64, 32);
    field.Compose(a.View(), b.View(), 100, out.pixels.data());
    Blend(a.View(), b.View(), 100, blend.pixels.data());
    int differ = 0;
    for (size_t p = 0; p < out.pixels.size(); p++)
        differ += out.pixels[p] != blend.pixels[p];
    // Where two random pixels happen to agree under a candidate, that pixel is taken: rare
    EXPECT_LT(differ, static_cast<int>(out.pixels.size() / 20));
}

TEST(SmoothMotion_Test, ExtrapolationCarriesTheMotionAndLeavesStillAreasAlone)
{
    // A -> B moves the band 4 px; w = 2 carries it 4 px more. The black around it never moves
    const Image a = Scroller(96, 64, 0, 16, 32);
    const Image b = Scroller(96, 64, 4, 16, 32);
    const Image truth = Scroller(96, 64, 8, 16, 32);
    MotionField field;
    field.Estimate(a.View(), b.View(), MotionField::kRangePictures);
    Image out(96, 64);
    field.Compose(a.View(), b.View(), 2 * kOne, out.pixels.data());
    int exact = 0, total = 0;
    for (int y = 16; y < 48; y++)
        for (int x = 8; x < 80; x++)
        {
            total++;
            exact += out.At(x, y) == truth.At(x, y);
        }
    EXPECT_GT(exact, total * 9 / 10);
    for (int x = 0; x < 96; x++)
        EXPECT_EQ(out.At(x, 2), BLACK);
}

TEST(SmoothMotion_Test, MaskLimitsTheComposition)
{
    const Image a = Scroller(64, 32, 0, 0, 32);
    const Image b = Scroller(64, 32, 4, 0, 32);
    MotionField field;
    field.Estimate(a.View(), b.View(), MotionField::kRangeFrames);
    std::vector<uint8_t> mask(64 * 32, 0);
    for (int y = 0; y < 16; y++)
        std::memset(&mask[static_cast<size_t>(y) * 64], 1, 64);
    Image out(64, 32, 0xFF123456);
    field.Compose(a.View(), b.View(), 128, out.pixels.data(), mask.data());
    for (int y = 16; y < 32; y++)
        for (int x = 0; x < 64; x++)
            ASSERT_EQ(out.At(x, y), 0xFF123456u);
    EXPECT_NE(out.At(20, 4), 0xFF123456u);
}

TEST(SmoothMotion_Test, SameBitsOnEveryCompiler)
{
    // The outputs of a fixed scene, hashed: clang, gcc, MSVC and MinGW give these bits or the GPU passes have no
    // reference to match
    const Image a = Sprite(80, 48, 10, 12);
    Image b = Sprite(80, 48, 15, 14);
    for (int x = 0; x < 80; x++)
        b.At(x, 40) = PatternAt(x, 3);  // a line that changes unrelated: the blend fallback
    MotionField field;
    field.Estimate(a.View(), b.View(), MotionField::kRangeFrames);
    Image half(80, 48), third(80, 48), carry(80, 48), blend(80, 48);
    field.Compose(a.View(), b.View(), 128, half.pixels.data());
    field.Compose(a.View(), b.View(), 85, third.pixels.data());
    field.Compose(a.View(), b.View(), 400, carry.pixels.data());
    Blend(a.View(), b.View(), 77, blend.pixels.data());
    // LayerFlow: the sprite on from a frame before a
    const Image p = Sprite(80, 48, 5, 10);
    const Image layersHalf = Layers(p, a, b, 128), layersThird = Layers(p, a, b, 85);
    const uint64_t hashes[6] = {Hash(half.pixels),   Hash(third.pixels),      Hash(carry.pixels),
                                Hash(blend.pixels),  Hash(layersHalf.pixels), Hash(layersThird.pixels)};
    const uint64_t expected[6] = {4633665985508127287ull, 14177168654969719964ull, 6201765027649431927ull,
                                  1468177541638179538ull, 1473332912888063342ull,  18128183336690002154ull};
    for (int i = 0; i < 6; i++)
        EXPECT_EQ(hashes[i], expected[i]) << "output " << i << ": 0x" << std::hex << hashes[i];
}

namespace
{
/// Frames into the engine, PERIOD apart from t = 1000 ms
struct Feed
{
    SmoothMotion engine;
    uint64_t serial = 0;
    int64_t t = 1000 * MS;
    void Push(const Image& img)
    {
        engine.Push(++serial, t, img.pixels.data(), img.width, img.height);
        t += PERIOD;
    }
    int64_t TargetOf(uint64_t s) const { return 1000 * MS + static_cast<int64_t>(s - 1) * PERIOD; }
};
}  // namespace

TEST(SmoothMotion_Test, EngineShowsTheFrameDueWhenOff)
{
    Feed feed;
    feed.Push(Scroller(64, 32, 0, 0, 32));
    feed.Push(Scroller(64, 32, 4, 0, 32));
    Image out(64, 32);
    const auto r = feed.engine.Render(feed.TargetOf(1) + PERIOD / 2, out.pixels.data());
    EXPECT_EQ(r.kind, SmoothMotion::Kind::Frame);
    EXPECT_EQ(r.serial, 1u);
    EXPECT_EQ(out.pixels, Scroller(64, 32, 0, 0, 32).pixels);
    // Before every target: the oldest frame
    EXPECT_EQ(feed.engine.Render(0, out.pixels.data()).serial, 1u);
    // After the last: the last
    EXPECT_EQ(feed.engine.Render(feed.TargetOf(2) + 10 * PERIOD, out.pixels.data()).serial, 2u);
}

TEST(SmoothMotion_Test, EngineBlendsAndInterpolatesBetweenFrames)
{
    Feed feed;
    const Image a = Scroller(96, 64, 0, 16, 32);
    const Image b = Scroller(96, 64, 4, 16, 32);
    feed.Push(a);
    feed.Push(b);
    Image out(96, 64);

    feed.engine.SetMode(Mode::Blend);
    auto r = feed.engine.Render(feed.TargetOf(1) + PERIOD / 4, out.pixels.data());
    EXPECT_EQ(r.kind, SmoothMotion::Kind::Blend);
    EXPECT_EQ(r.weight, 64);
    EXPECT_EQ(out.At(40, 20), Mix(a.At(40, 20), b.At(40, 20), 64));
    // At the target itself: the frame
    EXPECT_EQ(feed.engine.Render(feed.TargetOf(1), out.pixels.data()).kind, SmoothMotion::Kind::Frame);

    feed.engine.SetMode(Mode::Interpolate);
    r = feed.engine.Render(feed.TargetOf(1) + PERIOD / 2, out.pixels.data());
    EXPECT_EQ(r.kind, SmoothMotion::Kind::Interpolate);
    EXPECT_EQ(r.from, 1u);
    EXPECT_EQ(r.to, 2u);
    EXPECT_EQ(out.At(40, 20), Scroller(96, 64, 2, 16, 32).At(40, 20));
    // One field per pair
    feed.engine.Render(feed.TargetOf(1) + PERIOD / 3, out.pixels.data());
    EXPECT_EQ(feed.engine.GetStats().estimates, 1u);
}

TEST(SmoothMotion_Test, PlanNamesThePictureDrawDraws)
{
    // The screen draws a refresh's picture only when its plan differs from the last one: the same plan must be the
    // same pixels, and Render = Plan + Draw
    Feed feed;
    feed.engine.SetMode(Mode::Interpolate);
    for (int f = 0; f < 4; f++)
        feed.Push(Scroller(96, 64, f * 3, 16, 32));
    Image planned(96, 64), rendered(96, 64);
    for (int64_t t = feed.TargetOf(1) - PERIOD; t < feed.TargetOf(4) + PERIOD; t += PERIOD / 3)
    {
        const auto plan = feed.engine.Plan(t);
        ASSERT_TRUE(feed.engine.Draw(plan, planned.pixels.data()));
        EXPECT_EQ(feed.engine.Render(t, rendered.pixels.data()), plan);
        EXPECT_EQ(planned.pixels, rendered.pixels) << "t " << t;
    }
    // A plan whose frames are gone draws nothing
    SmoothMotion::Result gone = feed.engine.Plan(feed.TargetOf(2) + PERIOD / 2);
    for (int f = 0; f < static_cast<int>(SmoothMotion::kFrames); f++)
        feed.Push(Scroller(96, 64, 0, 16, 32));
    EXPECT_FALSE(feed.engine.Draw(gone, planned.pixels.data()));
    EXPECT_FALSE(feed.engine.Draw({}, planned.pixels.data()));
}

namespace
{
/// A device that runs the steps on the CPU reference and counts them, or refuses them
struct CountingDevice : Device
{
    bool accept = true;
    int estimates = 0, blends = 0, composes = 0, layers = 0;
    bool Estimate(const Picture& a, const Picture& b, int range, MotionField& field) override
    {
        estimates++;
        if (accept)
            field.Estimate(a, b, range);
        return accept;
    }
    bool Blend(const Picture& a, const Picture& b, int w, uint32_t* out) override
    {
        blends++;
        if (accept)
            smoothmotion::Blend(a, b, w, out);
        return accept;
    }
    bool Compose(const MotionField& field, const Picture& a, const Picture& b, int w, uint32_t* out,
                 const uint8_t* mask) override
    {
        composes++;
        if (accept)
            field.Compose(a, b, w, out, mask);
        return accept;
    }
    bool ComposeLayers(const Picture& p, const Picture& a, const Picture& b, const MotionField& prev,
                       const MotionField& next, int w, uint32_t* out) override
    {
        layers++;
        if (accept)
            LayerFlow().Compose(p, a, b, prev, next, w, out);
        return accept;
    }
};
}  // namespace

TEST(SmoothMotion_Test, StepsGoToTheDeviceOrBackToTheCpu)
{
    for (bool accept : {true, false})
    {
        Feed feed, cpu;
        CountingDevice device;
        device.accept = accept;
        feed.engine.SetDevice(&device);
        for (Feed* f : {&feed, &cpu})
        {
            f->Push(Scroller(96, 64, 0, 16, 32));
            f->Push(Scroller(96, 64, 4, 16, 32));
        }
        const int64_t t = feed.TargetOf(1) + PERIOD / 2;
        for (Mode mode : {Mode::Blend, Mode::Interpolate})
        {
            Image out(96, 64), reference(96, 64);
            feed.engine.SetMode(mode);
            cpu.engine.SetMode(mode);
            feed.engine.Render(t, out.pixels.data());
            cpu.engine.Render(t, reference.pixels.data());
            // Refused or not: the CPU's picture
            EXPECT_EQ(out.pixels, reference.pixels) << (accept ? "device" : "refused");
        }
        EXPECT_EQ(device.estimates, 1);
        EXPECT_EQ(device.blends, 1);
        // Interpolate is LayerFlow's; Compose is the game modes'
        EXPECT_EQ(device.composes, 0);
        EXPECT_EQ(device.layers, 1);
        EXPECT_EQ(feed.engine.GetStats().onDevice, accept ? 3u : 0u);
    }
}

TEST(SmoothMotion_Test, EngineLeavesIdenticalFramesAlone)
{
    Feed feed;
    feed.engine.SetMode(Mode::Blend);
    const Image a = Scroller(64, 32, 0, 0, 32);
    feed.Push(a);
    feed.Push(a);
    Image out(64, 32);
    EXPECT_EQ(feed.engine.Render(feed.TargetOf(1) + PERIOD / 2, out.pixels.data()).kind, SmoothMotion::Kind::Frame);
}

TEST(SmoothMotion_Test, FlickerIsHeldAsTheFrameDue)
{
    // Rows 0-15 scroll 4 px a frame; rows 16-31 alternate between two pictures (gigascreen)
    const auto frame = [](int k) {
        Image img = Scroller(96, 32, 4 * k, 0, 16);
        for (int y = 16; y < 32; y++)
            for (int x = 0; x < 96; x++)
                img.At(x, y) = PatternAt(x + (k % 2) * 500, y);
        return img;
    };
    for (Mode mode : {Mode::Blend, Mode::Interpolate})
    {
        Feed feed;
        feed.engine.SetMode(mode);
        for (int k = 0; k < 4; k++)
            feed.Push(frame(k));
        Image out(96, 32);
        // Frames 2 -> 3: the rows went 0, 1, 0, 1
        const auto r = feed.engine.Render(feed.TargetOf(3) + PERIOD / 2, out.pixels.data());
        EXPECT_EQ(r.from, 3u);
        EXPECT_EQ(r.to, 4u);
        const Image due = frame(2);
        const Image next = frame(3);
        for (int x = 0; x < 96; x++)
        {
            // The alternating rows: the frame due wherever they differ
            EXPECT_EQ(out.At(x, 20), due.At(x, 20)) << ModeName(mode) << " x " << x;
            // The scroller: between the frames
            if (mode == Mode::Blend)
                EXPECT_EQ(out.At(x, 8), Mix(due.At(x, 8), next.At(x, 8), r.weight)) << "x " << x;
        }
        if (mode == Mode::Interpolate)
            EXPECT_EQ(out.At(40, 4), Scroller(96, 32, 10, 0, 16).At(40, 4));
        EXPECT_EQ(feed.engine.GetStats().heldFlicker, 1u);
    }

    // Nothing but flicker: the frame
    Feed feed;
    feed.engine.SetMode(Mode::Interpolate);
    const Image a = Scroller(64, 32, 0, 0, 32);
    const Image b = Scroller(64, 32, 300, 0, 32);
    for (int k = 0; k < 4; k++)
        feed.Push(k % 2 ? b : a);
    Image out(64, 32);
    auto r = feed.engine.Render(feed.TargetOf(3) + PERIOD / 2, out.pixels.data());
    EXPECT_EQ(r.kind, SmoothMotion::Kind::Frame);
    EXPECT_EQ(out.pixels, a.pixels);
    // Three frames are no evidence yet: the first pairs are interpolated
    EXPECT_EQ(feed.engine.Render(feed.TargetOf(2) + PERIOD / 2, out.pixels.data()).kind,
              SmoothMotion::Kind::Interpolate);
}

TEST(SmoothMotion_Test, CadenceStepsToTheNextPictureInTheLookahead)
{
    // A game drawing every 4th frame, the band 8 px on per picture, the bottom rows a score that changes every frame
    // (excluded)
    Feed feed;
    GameVideo game;
    game.picturePeriod = 4;
    game.mode = GameMode::Cadence;
    game.window = {0, 0, 96, 56};
    game.exclude = {{0, 48, 96, 8}};
    feed.engine.SetMode(Mode::Interpolate);
    feed.engine.SetGame(&game);
    for (int f = 0; f < 9; f++)
    {
        Image img = Scroller(96, 64, (f / 4) * 8, 8, 32);
        for (int x = 0; x < 96; x++)
            img.At(x, 50) = PatternAt(x, f);  // the score
        feed.Push(img);
    }
    // Pictures at frames 1, 5, 9 (serials)
    EXPECT_EQ(feed.engine.GetStats().pictures, 3u);
    Image out(96, 64);
    // Two frames before picture 5 the step starts: at 1.5 frames before it, a quarter of the way
    const int64_t t = feed.TargetOf(5) - PERIOD * 3 / 2;
    const auto r = feed.engine.Render(t, out.pixels.data());
    EXPECT_EQ(r.kind, SmoothMotion::Kind::Cadence);
    EXPECT_EQ(r.from, 1u);
    EXPECT_EQ(r.to, 5u);
    EXPECT_EQ(r.weight, 64);
    const Image truth = Scroller(96, 64, 2, 8, 32);
    int exact = 0, total = 0;
    for (int y = 8; y < 40; y++)
        for (int x = 8; x < 88; x++)
        {
            total++;
            exact += out.At(x, y) == truth.At(x, y);
        }
    EXPECT_EQ(exact, total);
    // The score row is the frame due's, untouched
    const uint64_t dueSerial = r.serial;
    for (int x = 0; x < 96; x++)
        EXPECT_EQ(out.At(x, 50), PatternAt(x, static_cast<int>(dueSerial) - 1));
    // Earlier in the period: the picture as it is
    EXPECT_EQ(feed.engine.Render(feed.TargetOf(2), out.pixels.data()).kind, SmoothMotion::Kind::Frame);
}

TEST(SmoothMotion_Test, CarryGoesOnWithTheLastMotion)
{
    Feed feed;
    GameVideo game;
    game.picturePeriod = 4;
    game.mode = GameMode::Carry;
    feed.engine.SetMode(Mode::Interpolate);
    feed.engine.SetGame(&game);
    for (int f = 0; f < 6; f++)
        feed.Push(Scroller(96, 64, (f / 4) * 8, 8, 32));
    // Pictures at serials 1 and 5; half a period after 5 the band is 4 px further on
    Image out(96, 64);
    const auto r = feed.engine.Render(feed.TargetOf(5) + 2 * PERIOD, out.pixels.data());
    EXPECT_EQ(r.kind, SmoothMotion::Kind::Carry);
    EXPECT_EQ(r.weight, kOne + 128);
    const Image truth = Scroller(96, 64, 12, 8, 32);
    int exact = 0, total = 0;
    for (int y = 8; y < 40; y++)
        for (int x = 8; x < 72; x++)
        {
            total++;
            exact += out.At(x, y) == truth.At(x, y);
        }
    EXPECT_GT(exact, total * 9 / 10);
    // A period and more after the last picture: the motion stopped, the frame as it is
    EXPECT_EQ(feed.engine.Render(feed.TargetOf(5) + 6 * PERIOD, out.pixels.data()).kind, SmoothMotion::Kind::Frame);
}

TEST(SmoothMotion_Test, GameOffKeepsTheRepeat)
{
    Feed feed;
    GameVideo game;
    game.mode = GameMode::Off;
    feed.engine.SetMode(Mode::Interpolate);
    feed.engine.SetGame(&game);
    feed.Push(Scroller(64, 32, 0, 0, 32));
    feed.Push(Scroller(64, 32, 4, 0, 32));
    Image out(64, 32);
    EXPECT_EQ(feed.engine.Render(feed.TargetOf(1) + PERIOD / 2, out.pixels.data()).kind, SmoothMotion::Kind::Frame);
}

TEST(SmoothMotion_Test, SizeChangeStartsOver)
{
    Feed feed;
    feed.engine.SetMode(Mode::Blend);
    feed.Push(Scroller(64, 32, 0, 0, 32));
    feed.Push(Scroller(32, 16, 0, 0, 16));
    EXPECT_EQ(feed.engine.Width(), 32);
    Image out(32, 16);
    const auto r = feed.engine.Render(feed.TargetOf(1) + PERIOD / 2, out.pixels.data());
    EXPECT_EQ(r.kind, SmoothMotion::Kind::Frame);
    EXPECT_EQ(r.serial, 2u);
}
