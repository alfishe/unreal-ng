#include <gtest/gtest.h>

#include "emulator/video/pictureview.h"

/// PictureView: which part of a frame a window shows, and the shape and place of it. No machine: geometries are built
/// by hand, the way Screen latches them with a frame

namespace
{
/// A 352x288 Spectrum frame: the paper 256x192 at (48, 48)
PictureGeometry Spectrum()
{
    PictureGeometry g;
    g.width = 352;
    g.height = 288;
    g.screenWindow = PictureRect{48, 48, 256, 192};
    return g;
}

/// The Next frame of a game that clips its layers to 240x176 inside the paper
PictureGeometry NextClipped()
{
    PictureGeometry g;
    g.width = 640;
    g.height = 512;
    g.screenWindow = PictureRect{64, 64, 512, 384};
    g.contentWindow = PictureRect{80, 80, 480, 352};
    return g;
}

bool Same(const PictureRect& a, const PictureRect& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
}  // namespace

TEST(PictureView_Test, NamesParseInAnyCaseAndRefuseTheRest)
{
    PictureView::Mode mode = PictureView::Mode::Full;
    ASSERT_TRUE(PictureView::Parse("Content", mode));
    EXPECT_EQ(mode, PictureView::Mode::Content);
    ASSERT_TRUE(PictureView::Parse("SCREEN", mode));
    EXPECT_EQ(mode, PictureView::Mode::Screen);
    ASSERT_TRUE(PictureView::Parse("full", mode));
    EXPECT_EQ(mode, PictureView::Mode::Full);
    EXPECT_FALSE(PictureView::Parse("paper", mode));
    EXPECT_FALSE(PictureView::Parse("", mode));
    EXPECT_EQ(PictureView::NameList(), "full, screen, content");
    for (const std::string& name : PictureView::Names())
    {
        ASSERT_TRUE(PictureView::Parse(name, mode)) << name;
        EXPECT_EQ(PictureView::NameOf(mode), name);
    }
}

TEST(PictureView_Test, SourceIsTheFrameTheScreenOrTheContent)
{
    const PictureGeometry spectrum = Spectrum();
    EXPECT_TRUE(Same(PictureView::Source(spectrum, PictureView::Mode::Full), PictureRect{0, 0, 352, 288}));
    EXPECT_TRUE(Same(PictureView::Source(spectrum, PictureView::Mode::Screen), PictureRect{48, 48, 256, 192}));
    EXPECT_TRUE(Same(PictureView::Source(spectrum, PictureView::Mode::Content), PictureRect{48, 48, 256, 192}))
        << "a machine with no finer window shows its screen as content";

    const PictureGeometry next = NextClipped();
    EXPECT_TRUE(Same(PictureView::Source(next, PictureView::Mode::Screen), PictureRect{64, 64, 512, 384}));
    EXPECT_TRUE(Same(PictureView::Source(next, PictureView::Mode::Content), PictureRect{80, 80, 480, 352}));
    EXPECT_TRUE(Same(PictureView::Source(next, PictureView::Mode::Full), PictureRect{0, 0, 640, 512}));
}

TEST(PictureView_Test, AFrameWithoutABorderIsShownWhole)
{
    // The descriptor's window is the whole frame (Layer 2 in 320x256, an external card's picture)
    PictureGeometry g;
    g.width = 640;
    g.height = 512;
    g.screenWindow = PictureRect{0, 0, 640, 512};
    for (PictureView::Mode mode : {PictureView::Mode::Full, PictureView::Mode::Screen, PictureView::Mode::Content})
        EXPECT_TRUE(Same(PictureView::Source(g, mode), PictureRect{0, 0, 640, 512})) << PictureView::NameOf(mode);
}

TEST(PictureView_Test, AnEmptyOrStrayWindowFallsBackToAWiderOne)
{
    PictureGeometry g = Spectrum();
    g.contentWindow = PictureRect{300, 280, 100, 100};  // sticks out of the frame
    EXPECT_TRUE(Same(PictureView::Source(g, PictureView::Mode::Content), g.screenWindow)) << "content -> screen";
    g.screenWindow = PictureRect{0, 0, 0, 0};
    EXPECT_TRUE(Same(PictureView::Source(g, PictureView::Mode::Screen), PictureRect{0, 0, 352, 288})) << "screen -> frame";
    EXPECT_TRUE(Same(PictureView::Source(g, PictureView::Mode::Content), PictureRect{0, 0, 352, 288}));
}

TEST(PictureView_Test, TheShapeIsTheCrtsNotTheStoredPixels)
{
    const PictureGeometry spectrum = Spectrum();
    const PictureView::Cut full = PictureView::CutOf(spectrum, 352, 288, PictureView::Mode::Full);
    EXPECT_NEAR(full.aspect, 352.0 / 288.0, 1e-9);
    const PictureView::Cut screen = PictureView::CutOf(spectrum, 352, 288, PictureView::Mode::Screen);
    EXPECT_NEAR(screen.aspect, 4.0 / 3.0, 1e-9) << "the paper is 4:3";

    // The whole 640x512 Next frame is drawn in the CRT's 352:288 shape, so a part of it is that shape cut down by its
    // share of the frame: the 512x384 paper 512*512*352 / (640*384*288), the clipped 480x352 content exactly 4:3
    const PictureGeometry next = NextClipped();
    EXPECT_NEAR(PictureView::CutOf(next, 640, 512, PictureView::Mode::Screen).aspect,
                (512.0 * 512 * 352) / (640.0 * 384 * 288), 1e-9);
    EXPECT_NEAR(PictureView::CutOf(next, 640, 512, PictureView::Mode::Content).aspect, 4.0 / 3.0, 1e-9);
}

TEST(PictureView_Test, AGeometryOfAnotherFrameIsIgnored)
{
    // The pixels in hand are 352x288 but the geometry latched with a 640x512 frame: cut the whole frame, never
    // outside it
    const PictureView::Cut cut = PictureView::CutOf(NextClipped(), 352, 288, PictureView::Mode::Content);
    EXPECT_TRUE(Same(cut.source, PictureRect{0, 0, 352, 288}));
    EXPECT_NEAR(cut.aspect, 352.0 / 288.0, 1e-9);
    // No geometry at all (a source that gives none)
    const PictureView::Cut none = PictureView::CutOf(PictureGeometry{}, 352, 288, PictureView::Mode::Screen);
    EXPECT_TRUE(Same(none.source, PictureRect{0, 0, 352, 288}));
}

TEST(PictureView_Test, FitFillsTheHeightAndKeepsTheShape)
{
    // 4:3 in the Deck's 1280x800: the whole height, bars at the sides
    const PictureView::Placement p = PictureView::Fit(4.0 / 3.0, 1280, 800);
    EXPECT_EQ(p.height, 800u);
    EXPECT_EQ(p.width, 1067u);
    EXPECT_EQ(p.y, 0u);
    EXPECT_EQ(p.x, (1280u - 1067u) / 2);

    // The whole 352:288 frame
    const PictureView::Placement frame = PictureView::Fit(352.0 / 288.0, 1280, 800);
    EXPECT_EQ(frame.height, 800u);
    EXPECT_EQ(frame.width, 978u);

    // Wider than the window: fitted by width instead, never out of the window
    const PictureView::Placement wide = PictureView::Fit(3.2, 1280, 800);
    EXPECT_EQ(wide.width, 1280u);
    EXPECT_EQ(wide.height, 400u);
    EXPECT_EQ(wide.y, 200u);

    const PictureView::Placement none = PictureView::Fit(0.0, 1280, 800);
    EXPECT_EQ(none.width, 0u);
}
