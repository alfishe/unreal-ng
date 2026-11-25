#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "emulator/video/smoothtile.h"

/// smoothmotion::Tile: one stream of frames among many (a video wall's tile). A scripted frame source, no emulator

using namespace smoothmotion;

namespace
{
constexpr uint32_t BLACK = 0xFF000000;
constexpr int W = 64;
constexpr int H = 32;
constexpr int64_t MS = 1'000'000;
constexpr int64_t PERIOD = 20 * MS;
constexpr int64_t REFRESH = 16'666'667;
constexpr int64_t START = 1000 * MS;

uint32_t PatternAt(int x, int y)
{
    uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    static const uint32_t colours[4] = {BLACK, 0xFFFFFFFF, 0xFF0000D7, 0xFFD7D700};
    return colours[h & 3];
}

std::vector<uint32_t> Scroller(int offset, int width = W, int height = H)
{
    std::vector<uint32_t> frame(static_cast<size_t>(width) * height, BLACK);
    for (int y = 8; y < height - 8; y++)
        for (int x = 0; x < width; x++)
            frame[static_cast<size_t>(y) * width + x] = PatternAt(x + offset, y);
    return frame;
}

/// The machine's side: frames latched so far, served after a serial as Screen::CopyNextFrame does
struct Source
{
    struct Frame
    {
        uint64_t serial;
        int64_t targetNs;
        std::vector<uint32_t> pixels;
        bool atOnce;
        uint16_t window;
    };
    std::vector<Frame> frames;
    int served = 0;

    void Latch(std::vector<uint32_t> pixels, bool atOnce = false, uint16_t window = 0)
    {
        const uint64_t serial = frames.size() + 1;
        frames.push_back({serial, START + static_cast<int64_t>(serial - 1) * PERIOD, std::move(pixels), atOnce, window});
    }
    int64_t TargetOf(uint64_t serial) const { return START + static_cast<int64_t>(serial - 1) * PERIOD; }
    Tile::NextFrame Next()
    {
        return [this](uint64_t after, uint8_t* dst, size_t bytes, PresentFrameInfo* info) {
            for (const Frame& f : frames)
                if (f.serial > after)
                {
                    if (bytes != f.pixels.size() * sizeof(uint32_t))
                        return false;
                    std::memcpy(dst, f.pixels.data(), bytes);
                    info->serial = f.serial;
                    info->targetNs = f.targetNs;
                    info->atOnce = f.atOnce;
                    info->geometry.screenWindow.x = f.window;
                    served++;
                    return true;
                }
            return false;
        };
    }
};

std::vector<uint32_t> PictureOf(const Tile& tile)
{
    return std::vector<uint32_t>(tile.Picture(), tile.Picture() + static_cast<size_t>(tile.Width()) * tile.Height());
}
}  // namespace

TEST(SmoothTile_Test, NoFramesIsNothing)
{
    Tile tile;
    tile.SetMode(Mode::Blend);
    Source source;
    EXPECT_EQ(tile.Advance(source.Next(), W, H, START, REFRESH), Tile::Step::Nothing);
    EXPECT_FALSE(tile.Moving());
    EXPECT_EQ(tile.Advance(source.Next(), 0, 0, START, REFRESH), Tile::Step::Nothing);
    EXPECT_EQ(tile.Advance(nullptr, W, H, START, REFRESH), Tile::Step::Nothing);
}

TEST(SmoothTile_Test, PullsEveryFrameOnceAndDrawsBetweenThem)
{
    Tile tile;
    tile.SetMode(Mode::Blend);
    Source source;
    source.Latch(Scroller(0));
    source.Latch(Scroller(4));
    // On the first frame's target: the frame as it is
    ASSERT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(1), REFRESH), Tile::Step::Updated);
    EXPECT_EQ(PictureOf(tile), Scroller(0));
    EXPECT_EQ(tile.GetStats().frames, 2u);
    EXPECT_EQ(source.served, 2);
    // The same refresh asked again: nothing to draw, nothing pulled twice
    EXPECT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(1), REFRESH), Tile::Step::Unchanged);
    EXPECT_EQ(source.served, 2);
    // Half way to the second: the blend
    ASSERT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(1) + PERIOD / 2, REFRESH), Tile::Step::Updated);
    const std::vector<uint32_t> a = Scroller(0), b = Scroller(4), mid = PictureOf(tile);
    for (size_t p = 0; p < mid.size(); p++)
        ASSERT_EQ(mid[p], Mix(a[p], b[p], kOne / 2)) << p;
    EXPECT_TRUE(tile.Moving());
    EXPECT_EQ(tile.GetStats().between, 1u);
    // Past the last frame: the frame, and with nothing new and nothing between, the wall may rest
    ASSERT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(2) + PERIOD, REFRESH), Tile::Step::Updated);
    EXPECT_EQ(PictureOf(tile), Scroller(4));
    EXPECT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(2) + 2 * PERIOD, REFRESH), Tile::Step::Unchanged);
    EXPECT_FALSE(tile.Moving());
}

TEST(SmoothTile_Test, NeverShowsAPictureOlderThanTheOneShown)
{
    Tile tile;
    tile.SetMode(Mode::Blend);
    Source source;
    for (int k = 0; k < 4; k++)
        source.Latch(Scroller(4 * k));
    ASSERT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(3), REFRESH), Tile::Step::Updated);
    EXPECT_EQ(PictureOf(tile), Scroller(8));
    // A refresh asked for an earlier time (the caller's clock stepped back): the picture stays
    EXPECT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(1), REFRESH), Tile::Step::Unchanged);
    EXPECT_EQ(PictureOf(tile), Scroller(8));
    EXPECT_EQ(tile.GetStats().behind, 1u);
}

TEST(SmoothTile_Test, LowLatencyFramesAreShownAsTheyAre)
{
    Tile tile;
    tile.SetMode(Mode::InterpolateLite);
    Source source;
    source.Latch(Scroller(0), true);
    source.Latch(Scroller(4), true);
    // Whatever the time asked: the newest frame, nothing between
    ASSERT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(1) + PERIOD / 2, REFRESH), Tile::Step::Updated);
    EXPECT_EQ(PictureOf(tile), Scroller(4));
    EXPECT_EQ(tile.GetStats().between, 0u);
    source.Latch(Scroller(8), true);
    ASSERT_EQ(tile.Advance(source.Next(), W, H, 0, REFRESH), Tile::Step::Updated);
    EXPECT_EQ(PictureOf(tile), Scroller(8));
}

TEST(SmoothTile_Test, TheGeometryIsOfTheFrameShown)
{
    Tile tile;
    tile.SetMode(Mode::Blend);
    Source source;
    source.Latch(Scroller(0), false, 10);
    source.Latch(Scroller(4), false, 20);
    ASSERT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(1), REFRESH), Tile::Step::Updated);
    EXPECT_EQ(tile.Geometry().screenWindow.x, 10);
    ASSERT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(2), REFRESH), Tile::Step::Updated);
    EXPECT_EQ(tile.Geometry().screenWindow.x, 20);
}

TEST(SmoothTile_Test, AnotherFrameSizeStartsOver)
{
    Tile tile;
    tile.SetMode(Mode::Blend);
    Source small;
    small.Latch(Scroller(0));
    ASSERT_EQ(tile.Advance(small.Next(), W, H, small.TargetOf(1), REFRESH), Tile::Step::Updated);
    // The machine switched its video mode: the source serves frames of the new size from the next serial on
    Source big;
    big.Latch(Scroller(0, 96, 48));
    big.frames[0].serial = 2;
    ASSERT_EQ(tile.Advance(big.Next(), 96, 48, big.TargetOf(1), REFRESH), Tile::Step::Updated);
    EXPECT_EQ(tile.Width(), 96);
    EXPECT_EQ(tile.Height(), 48);
    EXPECT_EQ(PictureOf(tile), Scroller(0, 96, 48));
}

TEST(SmoothTile_Test, TooSlowStepsDownToTheCheapInterpolationThenTheBlend)
{
    // A refresh of one nanosecond: every interpolated picture takes "most of a refresh"
    Tile tile;
    tile.SetMode(Mode::Interpolate);
    Source source;
    source.Latch(Scroller(0));
    int pictures = 0;
    bool steppedAt = false, slowAt = false;
    for (int k = 1; k <= 3 * static_cast<int>(SmoothGuard::kWindow) && !slowAt; k++)
    {
        source.Latch(Scroller(4 * k));
        if (tile.Advance(source.Next(), W, H, source.TargetOf(static_cast<uint64_t>(k)) + PERIOD / 2, 1) ==
            Tile::Step::Updated)
            pictures++;
        if (tile.GetStats().steppedDown && !steppedAt)
        {
            steppedAt = true;
            EXPECT_EQ(pictures, static_cast<int>(SmoothGuard::kWindow));
            EXPECT_TRUE(tile.Engine().Lite());
            EXPECT_FALSE(tile.GetStats().tooSlow);
        }
        slowAt = tile.GetStats().tooSlow;
    }
    EXPECT_TRUE(steppedAt);
    ASSERT_TRUE(slowAt);
    EXPECT_EQ(pictures, 2 * static_cast<int>(SmoothGuard::kWindow));
    // From now on the blend of the two frames
    const uint64_t blendedBefore = tile.Engine().GetStats().blended;
    source.Latch(Scroller(4000));
    const uint64_t last = source.frames.size();
    ASSERT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(last - 1) + PERIOD / 2, 1), Tile::Step::Updated);
    EXPECT_EQ(tile.Engine().GetStats().blended, blendedBefore + 1);
    // Set again: the interpolation gets its chance again
    tile.SetMode(Mode::Blend);
    tile.SetMode(Mode::Interpolate);
    EXPECT_FALSE(tile.GetStats().steppedDown);
    EXPECT_FALSE(tile.GetStats().tooSlow);
}

TEST(SmoothTile_Test, InTimeNeverStepsDown)
{
    Tile tile;
    tile.SetMode(Mode::InterpolateLite);
    Source source;
    source.Latch(Scroller(0));
    for (int k = 1; k <= 3 * static_cast<int>(SmoothGuard::kWindow); k++)
    {
        source.Latch(Scroller(4 * k));
        // a refresh an hour long: no picture is slow
        tile.Advance(source.Next(), W, H, source.TargetOf(static_cast<uint64_t>(k)) + PERIOD / 2, 3600LL * 1000 * MS);
    }
    EXPECT_FALSE(tile.GetStats().steppedDown);
    EXPECT_FALSE(tile.GetStats().tooSlow);
    EXPECT_GT(tile.Engine().GetStats().lite, 0u);
}

TEST(SmoothTile_Test, TheWallStepsATileDownToTheFramesAsTheyAre)
{
    Tile tile;
    tile.SetMode(Mode::Interpolate);
    Source source;
    source.Latch(Scroller(0));
    source.Latch(Scroller(4));
    ASSERT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(1) + PERIOD / 2, REFRESH), Tile::Step::Updated);
    // interpolate -> the cheap one -> the blend -> the frames
    ASSERT_TRUE(tile.StepDown());
    EXPECT_TRUE(tile.GetStats().steppedDown);
    EXPECT_TRUE(tile.Engine().Lite());
    ASSERT_TRUE(tile.StepDown());
    EXPECT_TRUE(tile.GetStats().tooSlow);
    ASSERT_TRUE(tile.StepDown());
    EXPECT_TRUE(tile.GetStats().framesOnly);
    EXPECT_FALSE(tile.StepDown());
    // The frames as they are: the newest one whatever the time asked, nothing between, the engine fed nothing
    const uint64_t pushed = tile.Engine().GetStats().frames;
    source.Latch(Scroller(8));
    source.Latch(Scroller(12));
    ASSERT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(3) + PERIOD / 2, REFRESH), Tile::Step::Updated);
    EXPECT_EQ(PictureOf(tile), Scroller(12));
    EXPECT_FALSE(tile.Moving());
    EXPECT_EQ(tile.Engine().GetStats().frames, pushed);
    EXPECT_EQ(tile.Advance(source.Next(), W, H, source.TargetOf(4), REFRESH), Tile::Step::Unchanged);
    // The blend steps straight to the frames; the mode set again starts from the top
    Tile blend;
    blend.SetMode(Mode::Blend);
    ASSERT_TRUE(blend.StepDown());
    EXPECT_TRUE(blend.GetStats().framesOnly);
    blend.SetMode(Mode::InterpolateLite);
    EXPECT_FALSE(blend.GetStats().framesOnly);
    ASSERT_TRUE(blend.StepDown());
    EXPECT_TRUE(blend.GetStats().tooSlow);
    EXPECT_FALSE(blend.GetStats().framesOnly);
}
