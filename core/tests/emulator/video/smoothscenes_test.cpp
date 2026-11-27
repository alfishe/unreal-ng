#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "emulator/video/smoothlayers.h"
#include "emulator/video/smoothlite.h"
#include "emulator/video/smoothmotion.h"
#include "emulator/video/smoothscenes.h"
#include "_helpers/testtier.h"

/// The scene set the layers' rules are judged by (docs/inprogress/2026-11-22-presenter-stabilization, section 1.6):
/// scrolls beyond the search range, fades, border effects, multicolor, color cycling, interlace and a held 25 Hz
/// flicker on a 60 Hz display. Each scene asserts what any correct smooth motion keeps there, through the engine
/// (SmoothMotion: Blend, Interpolate by LayerFlow, InterpolateLite by LiteFlow) and the layers themselves.
/// The rules are frozen: a scene the engine draws wrong is a finding (DISABLED_, the defect named), not a fix.
/// The default run takes one weight, scroll step and mode where the scene asks the same of each; the long tier
/// (UNREAL_LONG_TESTS=1) every one

using namespace smoothmotion;
using namespace smoothscenes;

namespace
{
constexpr int W = 128;
constexpr int H = 96;
constexpr int BORDER = 16;
constexpr int64_t MS = 1'000'000;
constexpr int64_t PERIOD = 20 * MS;  // 50 Hz frames

using Frames = std::function<std::vector<uint32_t>(int k)>;

const char* KindName(SmoothMotion::Kind kind)
{
    switch (kind)
    {
        case SmoothMotion::Kind::None:
            return "none";
        case SmoothMotion::Kind::Frame:
            return "frame";
        case SmoothMotion::Kind::Blend:
            return "blend";
        case SmoothMotion::Kind::Interpolate:
            return "interpolate";
        case SmoothMotion::Kind::Cadence:
            return "cadence";
        case SmoothMotion::Kind::Carry:
            return "carry";
    }
    return "?";
}

/// Frames 0 .. count - 1 of a scene in the engine, PERIOD apart; renders between the last two
struct Scene
{
    SmoothMotion engine;
    int width, height, count;
    std::vector<uint32_t> a, b, out;
    Scene(Mode mode, const Frames& frames, int frameCount, int frameWidth = W, int frameHeight = H)
        : width(frameWidth), height(frameHeight), count(frameCount)
    {
        engine.SetMode(mode);
        for (int k = 0; k < count; k++)
        {
            const std::vector<uint32_t> f = frames(k);
            engine.Push(static_cast<uint64_t>(k + 1), TargetOf(k), f.data(), width, height);
            if (k == count - 2)
                a = f;
            if (k == count - 1)
                b = f;
        }
        out.assign(a.size(), 0);
    }
    static int64_t TargetOf(int k) { return 1000 * MS + k * PERIOD; }
    /// The picture at the weight w between frames count - 2 and count - 1
    SmoothMotion::Result Render(int w)
    {
        return engine.Render(TargetOf(count - 2) + PERIOD * w / kOne, out.data());
    }
    /// What drew the picture
    std::string Drew(const SmoothMotion::Result& r) const
    {
        return std::string(ModeName(engine.GetMode())) + " drew " + KindName(r.kind) +
               " (lite " + std::to_string(engine.GetStats().lite) + ") w " + std::to_string(r.weight);
    }
};

/// Pixels of `out` (where `inside`) that are none of a, b and their blend at w: content taken from elsewhere
struct Tally
{
    int count = 0;
    int x = -1, y = -1;
    std::string Text() const
    {
        return std::to_string(count) + " (first at " + std::to_string(x) + "," + std::to_string(y) + ")";
    }
};
Tally Invented(const std::vector<uint32_t>& out, const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int w,
               int width, const std::function<bool(int, int)>& inside = nullptr)
{
    Tally t;
    for (size_t p = 0; p < out.size(); p++)
    {
        const int x = static_cast<int>(p % width), y = static_cast<int>(p / width);
        if (inside && !inside(x, y))
            continue;
        if (out[p] == a[p] || out[p] == b[p] || out[p] == Mix(a[p], b[p], w))
            continue;
        if (t.count++ == 0)
            t.x = x, t.y = y;
    }
    return t;
}

/// Pixels of `out` (where `inside`) unlike `want`
Tally Differ(const std::vector<uint32_t>& out, const std::vector<uint32_t>& want, int width,
             const std::function<bool(int, int)>& inside = nullptr)
{
    Tally t;
    for (size_t p = 0; p < out.size(); p++)
    {
        const int x = static_cast<int>(p % width), y = static_cast<int>(p / width);
        if ((inside && !inside(x, y)) || out[p] == want[p])
            continue;
        if (t.count++ == 0)
            t.x = x, t.y = y;
    }
    return t;
}

constexpr Mode kAllModes[] = {Mode::Blend, Mode::Interpolate, Mode::InterpolateLite};
constexpr Mode kInterpolating[] = {Mode::Interpolate, Mode::InterpolateLite};
/// The blend weights a scene is drawn at: the middle-late one by default, both in the long tier
std::vector<int> Weights()
{
    return TestTier::Pick<std::vector<int>>({171}, {85, 171});
}
}  // namespace

/// region Scrolls beyond the search range

TEST(SmoothScenes_Test, FastScrollInventsNothing)
{
    // 12, 16 and 24 px a frame: no vector within 8 explains the scroll, so a correct picture is the frames or their
    // blend at every pixel (no motion is drawn: README 1.6), and the still status bar is left as it is
    for (int step : TestTier::Pick<std::vector<int>>({16}, {12, 16, 24}))
        for (Mode mode : kInterpolating)
        {
            Scene scene(mode, [step](int k) { return FastScroll(W, H, k, step); }, 6);
            for (int w : {171})
            {
                const auto r = scene.Render(w);
                const auto inStatus = [](int, int y) { return y < kStatusRows; };
                const auto inScroll = [](int, int y) { return y >= kStatusRows; };
                EXPECT_EQ(Differ(scene.out, scene.a, W, inStatus).count, 0)
                    << "step " << step << ", " << scene.Drew(r) << ": status bar moved "
                    << Differ(scene.out, scene.a, W, inStatus).Text();
                const Tally t = Invented(scene.out, scene.a, scene.b, w, W, inScroll);
                EXPECT_EQ(t.count, 0) << "step " << step << ", " << scene.Drew(r) << ": invented " << t.Text() << " of "
                                      << W * (H - kStatusRows);
            }
        }
}

/// endregion

/// region Fades

TEST(SmoothScenes_Test, FadeIsABlendOrTheFrame)
{
    // A ZX fade steps the attributes' colors down: nothing moves, though the new colors are ones other cells showed
    // before. A correct picture is the frames or their blend at every pixel.
    // Pairs: 7 -> 6 (bright goes), 3 -> 2
    for (int last : TestTier::Pick<std::vector<int>>({1}, {1, 5}))
        for (Mode mode : kAllModes)
        {
            Scene scene(mode, [](int k) { return Fade(W, H, 7 - k); }, last + 1);
            const auto r = scene.Render(128);
            const Tally t = Invented(scene.out, scene.a, scene.b, 128, W);
            EXPECT_EQ(t.count, 0) << "level " << 8 - last << " -> " << 7 - last << ", " << scene.Drew(r)
                                  << ": invented " << t.Text() << " of " << W * H;
        }
}

/// endregion

/// region Border effects

namespace
{
/// A picture of a border scene between frames a and b at the weight w: the paper as a has it (the same in both
/// frames), the border nothing but the frames' pixels or their blend. The stripes move 3 lines a frame and are 4
/// lines high, so a stripe moved by any part of its step shows a's or b's color at every line: correct motion
/// passes, content carried across the paper's edge does not
void ExpectBorderPicture(const std::vector<uint32_t>& out, const std::vector<uint32_t>& a,
                         const std::vector<uint32_t>& b, int w, bool loader, const std::string& what)
{
    const auto inPaper = [](int x, int y) { return InPaper(W, H, BORDER, x, y); };
    const auto inBorder = [](int x, int y) { return !InPaper(W, H, BORDER, x, y); };
    const Tally paper = Differ(out, a, W, inPaper);
    EXPECT_EQ(paper.count, 0) << (loader ? "loader, " : "stripes, ") << what << ": paper pixels changed "
                              << paper.Text() << " of " << (W - 2 * BORDER) * (H - 2 * BORDER);
    const Tally border = Invented(out, a, b, w, W, inBorder);
    EXPECT_EQ(border.count, 0) << (loader ? "loader, " : "stripes, ") << what << ": border invented "
                               << border.Text() << " of " << W * H - (W - 2 * BORDER) * (H - 2 * BORDER);
}

/// A border scene rendered once, between its frames 4 and 5
void ExpectPaperStays(Mode mode, bool loader)
{
    Scene scene(mode, [loader](int k) { return Border(W, H, BORDER, k, loader); }, 6);
    for (int w : Weights())
    {
        const auto r = scene.Render(w);
        ExpectBorderPicture(scene.out, scene.a, scene.b, w, loader, scene.Drew(r));
    }
}
}  // namespace

TEST(SmoothScenes_Test, BorderEffectsLeaveThePaperAlone)
{
    // Stripes moving down the border 3 lines a frame, or a tape loader's random lines, round a still picture:
    // whatever the border shows, the paper's pixels are the same in both frames and stay, and the border shows
    // nothing from the paper. LayerFlow on the moving stripes: LayersMoveThePaperWithBorderStripes
    for (Mode mode : {Mode::Blend, Mode::InterpolateLite})
        ExpectPaperStays(mode, false);
    for (Mode mode : kAllModes)
        ExpectPaperStays(mode, true);
}

// Was a defect: the stripes fit every sideways shift, and the cover rule took a shift such as (4, 3) whose path from
// the border to the border crosses the paper's edge column (x = 16): 3 paper pixels changed at w = 85, 9 at w = 171.
// The cover rule now leaves a sample to the slowest vector fitting it, (0, 3), which stays in the border. And past
// the half weight carryFirst took b's sample of row 15 from the paper's first row, where the stripes go under the
// still paper (82 border pixels at w = 171): a still sample is not the moving layer's
TEST(SmoothScenes_Test, LayersMoveThePaperWithBorderStripes)
{
    ExpectPaperStays(Mode::Interpolate, false);
}

// Was a defect: random loader lines matched moving vectors by chance and were drawn moved (LayerFlow 1384 at w = 85
// and 1272 at w = 171 of 6144 border pixels none of a, b, blend; LiteFlow 160). A line of one colour across a block
// holds 8 values, not 64: a moving vector matched only there is now taken only when the pair before confirms it
TEST(SmoothScenes_Test, LoaderStripesAreNotMotion)
{
    // A tape loader's border: each line red or cyan at random, new every frame. Nothing moves: the border is the
    // frames or their blend at every pixel
    for (Mode mode : kInterpolating)
    {
        Scene scene(mode, [](int k) { return Border(W, H, BORDER, k, true); }, 6);
        for (int w : Weights())
        {
            const auto r = scene.Render(w);
            const auto inBorder = [](int x, int y) { return !InPaper(W, H, BORDER, x, y); };
            const Tally border = Invented(scene.out, scene.a, scene.b, w, W, inBorder);
            EXPECT_EQ(border.count, 0) << scene.Drew(r) << ": border invented " << border.Text() << " of "
                                       << W * H - (W - 2 * BORDER) * (H - 2 * BORDER);
        }
    }
}

// Both border scenes as the window draws them: every pair in order, a picture between each two frames (LiteFlow's
// kept field and vectors carried from pair to pair), 12 frames: nothing invented in any of them
TEST(SmoothScenes_Test, BorderEffectsInAStreamInventNothing)
{
    constexpr int kFrames = 12;
    for (bool loader : {false, true})
        for (Mode mode : kInterpolating)
        {
            SmoothMotion engine;
            engine.SetMode(mode);
            std::vector<uint32_t> before, out(static_cast<size_t>(W) * H);
            for (int k = 0; k < kFrames; k++)
            {
                const std::vector<uint32_t> frame = Border(W, H, BORDER, k, loader);
                engine.Push(static_cast<uint64_t>(k + 1), Scene::TargetOf(k), frame.data(), W, H);
                if (k > 0)
                    for (int w : Weights())
                    {
                        const auto r = engine.Render(Scene::TargetOf(k - 1) + PERIOD * w / kOne, out.data());
                        ExpectBorderPicture(out, before, frame, w, loader,
                                            std::string(ModeName(mode)) + " pair " + std::to_string(k - 1) + "-" +
                                                std::to_string(k) + " drew " + KindName(r.kind) + " w " +
                                                std::to_string(r.weight));
                    }
                before = frame;
            }
        }
}

// A loader's stripes 8 to 16 lines high, new every frame: a block inside one is of one color, and a vector carrying
// it exactly from the frame before through a to b happens by chance (two colors). Every pair of 12 frames in order,
// as the window draws them: nothing invented
TEST(SmoothScenes_Test, ThickLoaderStripesInventNothing)
{
    constexpr int kFrames = 12;
    const auto frameOf = [](int k) {
        std::vector<uint32_t> frame = Border(W, H, BORDER, k, true);
        uint32_t line = 0;
        for (int y = 0, next = 0; y < H; y++)
        {
            if (y == next)
            {
                const uint32_t h = Hash(11, static_cast<uint32_t>(y), static_cast<uint32_t>(k));
                next = y + 8 + static_cast<int>(h % 9);
                line = ZxColor((h >> 8) & 1 ? 2 : 5, false);
            }
            for (int x = 0; x < W; x++)
                if (!InPaper(W, H, BORDER, x, y))
                    frame[static_cast<size_t>(y) * W + x] = line;
        }
        return frame;
    };
    for (Mode mode : kInterpolating)
    {
        SmoothMotion engine;
        engine.SetMode(mode);
        std::vector<uint32_t> before, out(static_cast<size_t>(W) * H);
        for (int k = 0; k < kFrames; k++)
        {
            const std::vector<uint32_t> frame = frameOf(k);
            engine.Push(static_cast<uint64_t>(k + 1), Scene::TargetOf(k), frame.data(), W, H);
            if (k > 0)
                for (int w : Weights())
                {
                    const auto r = engine.Render(Scene::TargetOf(k - 1) + PERIOD * w / kOne, out.data());
                    ExpectBorderPicture(out, before, frame, w, true,
                                        std::string(ModeName(mode)) + " pair " + std::to_string(k - 1) + "-" +
                                            std::to_string(k) + " drew " + KindName(r.kind) + " w " +
                                            std::to_string(r.weight));
                }
            before = frame;
        }
    }
    // LiteFlow itself on every pair, whatever the engine would choose
    LiteFlow lite;
    std::vector<uint32_t> out(static_cast<size_t>(W) * H);
    for (int k = 0; k + 1 < kFrames; k++)
    {
        const std::vector<uint32_t> a = frameOf(k), b = frameOf(k + 1);
        for (int w : Weights())
        {
            ASSERT_TRUE(lite.Compose({W, H, a.data(), static_cast<uint64_t>(k + 1)},
                                     {W, H, b.data(), static_cast<uint64_t>(k + 2)}, w, out.data()));
            ExpectBorderPicture(out, a, b, w, true,
                                "LiteFlow pair " + std::to_string(k) + "-" + std::to_string(k + 1) + " w " +
                                    std::to_string(w));
        }
    }
}

/// endregion

/// region Text

TEST(SmoothScenes_Test, PageFlipInventsNothing)
{
    // A page of text flipped to another: nothing moves, though many a new letter is an old one a cell over (the
    // field matches it, the letters' detail confirms the vector). Every pixel is a frame's or their blend, LayerFlow
    // alone (the page held before the flip, and flipped every frame) and the engine
    std::vector<uint32_t> out(static_cast<size_t>(W) * H);
    LayerFlow flow;
    for (bool held : {true, false})
    {
        const std::vector<uint32_t> p = TextPage(W, H, held ? 1 : 0), a = TextPage(W, H, 1), b = TextPage(W, H, 2);
        const Picture pp{W, H, p.data()}, pa{W, H, a.data()}, pb{W, H, b.data()};
        MotionField prev, next;
        prev.Estimate(pp, pa, MotionField::kRangeFrames);
        next.Estimate(pa, pb, MotionField::kRangeFrames);
        for (int w : TestTier::Pick<std::vector<int>>({128}, {43, 85, 128, 171, 213}))
        {
            flow.Compose(pp, pa, pb, prev, next, w, out.data());
            const Tally t = Invented(out, a, b, w, W);
            EXPECT_EQ(t.count, 0) << (held ? "held page" : "a page every frame") << ", w " << w << ": invented "
                                  << t.Text();
        }
    }
    for (Mode mode : kInterpolating)
    {
        Scene scene(mode, [](int k) { return TextPage(W, H, k < 5 ? 0 : 5); }, 6);
        for (int w : Weights())
        {
            const auto r = scene.Render(w);
            const Tally t = Invented(scene.out, scene.a, scene.b, w, W);
            EXPECT_EQ(t.count, 0) << scene.Drew(r) << ": invented " << t.Text();
        }
    }
}

/// endregion

/// region Multicolor and color cycling

TEST(SmoothScenes_Test, StaticMulticolorPassesThrough)
{
    // Per-line attributes, the same every frame: the engine shows the frame, and the layers given the same picture
    // three times draw it as it is
    const auto frames = [](int k) { return Multicolor(W, H, k, false, false); };
    for (Mode mode : kAllModes)
    {
        Scene scene(mode, frames, 6);
        const auto r = scene.Render(128);
        EXPECT_EQ(r.kind, SmoothMotion::Kind::Frame) << scene.Drew(r);
        EXPECT_EQ(scene.out, scene.a) << scene.Drew(r);
    }
    const std::vector<uint32_t> a = frames(0);
    const Picture pa{W, H, a.data(), 1}, pb{W, H, a.data(), 2};
    MotionField field;
    field.Estimate(pa, pb, MotionField::kRangeFrames);
    std::vector<uint32_t> out(a.size());
    LayerFlow().Compose(pa, pa, pb, field, field, 128, out.data());
    EXPECT_EQ(Differ(out, a, W).count, 0) << "layers: " << Differ(out, a, W).Text();
    LiteFlow lite;
    ASSERT_TRUE(lite.Compose(pa, pb, 128, out.data()));
    EXPECT_EQ(Differ(out, a, W).count, 0) << "lite: " << Differ(out, a, W).Text();
}

TEST(SmoothScenes_Test, MulticolorKeepsItsPixelsBesideAChange)
{
    // The same multicolor picture with a 16 x 8 counter whose digits change every frame: the pair is drawn, and
    // every pixel outside the counter stays; the counter itself is the frames or their blend (it does not move)
    for (Mode mode : kAllModes)
    {
        Scene scene(mode, [](int k) { return Multicolor(W, H, k, false, true); }, 6);
        for (int w : Weights())
        {
            const auto r = scene.Render(w);
            const auto inCounter = [](int x, int y) { return x < 16 && y < 8; };
            const auto outside = [&](int x, int y) { return !inCounter(x, y); };
            const Tally still = Differ(scene.out, scene.a, W, outside);
            EXPECT_EQ(still.count, 0) << scene.Drew(r) << ": still pixels changed " << still.Text();
            const Tally counter = Invented(scene.out, scene.a, scene.b, w, W, inCounter);
            EXPECT_EQ(counter.count, 0) << scene.Drew(r) << ": counter invented " << counter.Text();
        }
    }
}

TEST(SmoothScenes_Test, ColorCyclingIsNotMotion)
{
    // The multicolor picture's colors step round every frame, nothing moves: a color of b sits next to where a
    // had it, but a correct picture is the frames or their blend at every pixel
    for (Mode mode : kInterpolating)
    {
        Scene scene(mode, [](int k) { return Multicolor(W, H, k, true, false); }, 6);
        for (int w : Weights())
        {
            const auto r = scene.Render(w);
            const Tally t = Invented(scene.out, scene.a, scene.b, w, W);
            EXPECT_EQ(t.count, 0) << scene.Drew(r) << ": invented " << t.Text() << " of " << W * H;
        }
    }
}

/// endregion

/// region Interlace and flicker

TEST(SmoothScenes_Test, InterlaceIsHeldAsTheFrameDue)
{
    // Two fields every other frame: the lines of two pictures swapped, or one picture a line lower. Flicker, not
    // motion (smoothmotion.h): with five frames known the pair differs in nothing else, and the frame due is shown
    for (bool jitter : {false, true})
        for (Mode mode : kAllModes)
        {
            Scene scene(mode, [jitter](int k) { return Interlace(W, H, k, jitter); }, 5);
            const auto r = scene.Render(128);
            EXPECT_EQ(r.kind, SmoothMotion::Kind::Frame) << (jitter ? "jitter, " : "lines, ") << scene.Drew(r);
            const Tally t = Differ(scene.out, scene.a, W);
            EXPECT_EQ(t.count, 0) << (jitter ? "jitter, " : "lines, ") << scene.Drew(r) << ": unlike the frame due "
                                  << t.Text();
        }
}

TEST(SmoothScenes_Test, HeldFlickerBeatsWithSixtyHertz)
{
    // Rows 0-15 scroll 4 px a frame, rows 16-31 alternate between two pictures (a 25 Hz flicker), 50 Hz frames on a
    // 60 Hz display for half a second. Every refresh shows the flicker as the frame due (the scroll between the
    // frames), so the flicker keeps the frame due's beat: 5 of the 30 refreshes repeat the field before them (a 10 Hz
    // beat), none shows a blend of the two fields
    constexpr int FW = 96, FH = 32;
    const auto frame = [](int k) {
        std::vector<uint32_t> img(static_cast<size_t>(FW) * FH);
        for (int y = 0; y < FH; y++)
            for (int x = 0; x < FW; x++)
                img[static_cast<size_t>(y) * FW + x] = y < 16 ? ZxPixel(x + 4 * k, y, 21) : ZxPixel(x, y, 22 + (k & 1));
        return img;
    };
    const auto inFlicker = [](int, int y) { return y >= 16; };
    // The flicker is held by the interpolation's guard; Blend and Lite in the long tier
    for (Mode mode : TestTier::Pick<std::vector<Mode>>({Mode::Interpolate},
                                                       {Mode::Blend, Mode::Interpolate, Mode::InterpolateLite}))
    {
        SmoothMotion engine;
        engine.SetMode(mode);
        int pushed = 0;
        const auto push = [&] {
            const std::vector<uint32_t> f = frame(pushed);
            engine.Push(static_cast<uint64_t>(pushed + 1), Scene::TargetOf(pushed), f.data(), FW, FH);
            pushed++;
        };
        for (int k = 0; k < 5; k++)
            push();
        std::vector<uint32_t> out(static_cast<size_t>(FW) * FH);
        int repeats = 0, wrong = 0, between = 0, frames = 0, lastParity = -1;
        const int64_t start = Scene::TargetOf(4);
        for (int i = 0; i < 30; i++)
        {
            const int64_t t = start + i * 1'000'000'000LL / 60;
            while (Scene::TargetOf(pushed - 1) <= t + PERIOD)
                push();
            const auto r = engine.Render(t, out.data());
            const int due = static_cast<int>(r.serial) - 1;
            ASSERT_GE(due, 0);
            const Tally t1 = Differ(out, frame(due), FW, inFlicker);
            wrong += t1.count;
            if ((due & 1) == lastParity)
                repeats++;
            lastParity = due & 1;
            if (r.kind == SmoothMotion::Kind::Frame)
                frames++;
            else
                between++;
        }
        EXPECT_EQ(wrong, 0) << ModeName(mode) << ": flicker pixels unlike the frame due over 30 refreshes";
        EXPECT_EQ(repeats, 5) << ModeName(mode);
        // The refreshes on a frame's own time are the frame; the other 25 draw the scroll between frames
        EXPECT_EQ(between, 25) << ModeName(mode) << ", frames " << frames;
    }
}

/// endregion
