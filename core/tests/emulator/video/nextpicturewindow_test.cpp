#include <gtest/gtest.h>

#include "emulator/video/next/nextpicturewindow.h"

/// NextPictureWindow: the working window of a ZX Next mode and the content window of its layers, in frame pixels.
/// Register values only, no machine. The case that started it: a game with Layer 2 at 256x192 whose layers are all
/// clipped to x 8..247, y 8..183 shows 480x352 inside the 512x384 paper of the 640x512 grid. The grid sits in the 720x576 HDMI
/// frame at (32, 32) in the 48K / 128K families (NextPictureWindow::Origin): the boxes below are in grid frame pixels, ExpectBox
/// adds that origin

namespace
{
using NextPictureWindow::FrameBox;
using NextPictureWindow::Inputs;

/// The ULA on with its default clip window, nothing else: a plain Spectrum screen
Inputs Spectrum()
{
    Inputs in;
    in.clip[2][1] = 255;  // ULA: x2, y2 (the power-on window)
    in.clip[2][3] = 191;
    return in;
}

constexpr int kGridX = 32, kGridY = 32;  ///< the grid's place in the frame (16 grid pixels and lines of outer border, doubled)

void ExpectBox(const FrameBox& box, int x, int y, int width, int height, const char* what)
{
    EXPECT_EQ(box.x, x + kGridX) << what;
    EXPECT_EQ(box.y, y + kGridY) << what;
    EXPECT_EQ(box.width, width) << what;
    EXPECT_EQ(box.height, height) << what;
}
}  // namespace

TEST(NextPictureWindow_Test, ASpectrumScreenIsThePaperInItsBorder)
{
    const Inputs in = Spectrum();
    ExpectBox(NextPictureWindow::Screen(in), 64, 64, 512, 384, "screen window");
    ExpectBox(NextPictureWindow::Content(in), 64, 64, 512, 384, "content of the unclipped ULA");
}

TEST(NextPictureWindow_Test, Layer2In256x192HasABorder)
{
    Inputs in = Spectrum();
    in.layer2Enabled = true;
    in.nr70 = 0x00;  // 256x192
    in.clip[0][1] = 255;
    in.clip[0][3] = 191;
    ExpectBox(NextPictureWindow::Screen(in), 64, 64, 512, 384, "Layer 2 at 256x192 is a Spectrum picture");
}

TEST(NextPictureWindow_Test, Layer2In320x256AndIn640x256HaveNoBorder)
{
    for (uint8_t resolution : {0x10, 0x20})
    {
        Inputs in = Spectrum();
        in.layer2Enabled = true;
        in.nr70 = resolution;
        in.clip[0][1] = 159;  // the wide modes' X is in units of two pixels
        in.clip[0][3] = 255;
        ExpectBox(NextPictureWindow::Screen(in), 0, 0, 640, 512, "the whole grid is the picture");
        ExpectBox(NextPictureWindow::Content(in), 0, 0, 640, 512, "nothing clipped");
    }
    // The same registers with Layer 2 off: a border again
    Inputs off = Spectrum();
    off.nr70 = 0x10;
    ExpectBox(NextPictureWindow::Screen(off), 64, 64, 512, 384, "Layer 2 off");
}

TEST(NextPictureWindow_Test, AGameThatClipsItsLayersShowsTheClippedWindow)
{
    // Layer 2 (256x192x8), the ULA and the sprites all at x 8..247, y 8..183; the game's own scene on the Deck
    Inputs in;
    in.layer2Enabled = true;
    in.nr70 = 0x00;
    for (int window : {0, 1, 2})
    {
        in.clip[window][0] = 8;
        in.clip[window][1] = 247;
        in.clip[window][2] = 8;
        in.clip[window][3] = 183;
    }
    in.nr15 = 0x01;           // sprites on
    in.spritesVisible = true;
    ExpectBox(NextPictureWindow::Content(in), 80, 80, 480, 352, "240x176 inside the paper, 8 pixels from each edge");
    ExpectBox(NextPictureWindow::Screen(in), 64, 64, 512, 384, "the screen window does not move with the clip");
}

TEST(NextPictureWindow_Test, ContentIsTheUnionOfTheLayersThatAreOn)
{
    Inputs in;
    in.nr68 = 0x80;  // ULA off
    in.layer2Enabled = true;
    in.clip[0][0] = 16;
    in.clip[0][1] = 111;
    in.clip[0][2] = 16;
    in.clip[0][3] = 95;
    // Layer 2 alone: x 32+16 .. 32+111 (grid 48..143), y 48..127 -> frame 96,96 192x160
    ExpectBox(NextPictureWindow::Content(in), 96, 96, 192, 160, "Layer 2 alone");

    // A tilemap far to the right and down joins it: X in units of two grid pixels
    in.nr6B = 0x80;
    in.clip[3][0] = 100;  // grid 200..
    in.clip[3][1] = 119;  // ..239
    in.clip[3][2] = 200;
    in.clip[3][3] = 230;
    // union x 48..239, y 48..230 -> frame x 96..479 (384 wide), y 96..461 (366 high)
    ExpectBox(NextPictureWindow::Content(in), 96, 96, 384, 366, "Layer 2 and the tilemap");
}

TEST(NextPictureWindow_Test, SpritesCountOnlyWhenOneShows)
{
    Inputs in;
    in.nr68 = 0x80;
    in.layer2Enabled = true;
    in.clip[0][0] = 8;
    in.clip[0][1] = 247;
    in.clip[0][2] = 8;
    in.clip[0][3] = 183;
    in.nr15 = 0x01;
    in.clip[1][0] = 0;
    in.clip[1][1] = 255;
    in.clip[1][2] = 0;
    in.clip[1][3] = 191;  // the sprites' default window: the whole paper

    in.spritesVisible = false;
    ExpectBox(NextPictureWindow::Content(in), 80, 80, 480, 352, "sprites on but none shows");
    in.spritesVisible = true;
    ExpectBox(NextPictureWindow::Content(in), 64, 64, 512, 384, "a sprite may show anywhere in its window");

    // Over the border without a clip of their own: the whole grid
    in.nr15 = 0x03;
    ExpectBox(NextPictureWindow::Content(in), 0, 0, 640, 512, "sprites over the border");
}

TEST(NextPictureWindow_Test, NothingOnIsNoContentAndAnEmptyWindowIsIgnored)
{
    Inputs in;
    in.nr68 = 0x80;
    EXPECT_TRUE(NextPictureWindow::Content(in).Empty());

    // A window with x1 > x2 draws nothing and adds nothing
    Inputs inverted = Spectrum();
    inverted.clip[2][0] = 200;
    inverted.clip[2][1] = 100;
    EXPECT_TRUE(NextPictureWindow::Content(inverted).Empty());
}

TEST(NextPictureWindow_Test, ContentNeverLeavesTheFrame)
{
    Inputs in;
    in.nr68 = 0x80;
    in.nr6B = 0x80;
    in.clip[3][0] = 0;
    in.clip[3][1] = 255;  // 2 * 255 + 1 = 511: past the 320-pixel grid
    in.clip[3][2] = 0;
    in.clip[3][3] = 255;
    const FrameBox box = NextPictureWindow::Content(in);
    EXPECT_LE(box.x + box.width, kGridX + 640);
    EXPECT_LE(box.y + box.height, kGridY + 512);
    ExpectBox(box, 0, 0, 640, 512, "the whole grid");
}

TEST(NextPictureWindow_Test, TheGridSitsInTheHdmiFrameOfItsFamily)
{
    // zxula_timing.vhd: the HDMI window against the grid's first pixel and line
    const NextPictureWindow::FrameOrigin zx = NextPictureWindow::Origin(2, false);
    EXPECT_EQ(zx.left, 16);
    EXPECT_EQ(zx.top, 16);
    EXPECT_EQ(NextPictureWindow::Origin(1, false).left, 16);
    const NextPictureWindow::FrameOrigin pentagon = NextPictureWindow::Origin(4, true);  // the Pentagon has no 60 Hz frame
    EXPECT_EQ(pentagon.left, 20);
    EXPECT_EQ(pentagon.top, 24);
    EXPECT_FALSE(pentagon.blackOutside);
    EXPECT_EQ(NextPictureWindow::kGridHeight + pentagon.top + 8, NextPictureWindow::kFrameHeight);  // 8 lines below
    EXPECT_EQ(NextPictureWindow::kGridWidth + pentagon.left + 20, NextPictureWindow::kFrameWidth);
    EXPECT_EQ(NextPictureWindow::kGridWidth + zx.left + 24, NextPictureWindow::kFrameWidth);

    Inputs in = Spectrum();
    in.origin = pentagon;
    const FrameBox box = NextPictureWindow::Screen(in);
    EXPECT_EQ(box.x, (20 + 32) * 2);
    EXPECT_EQ(box.y, (24 + 32) * 2);

    // 60 Hz: 240 lines, grid lines 8..247, the paper where the 50 Hz one is; a box is cut to the lines shown
    const NextPictureWindow::FrameOrigin sixty = NextPictureWindow::Origin(2, true);
    EXPECT_EQ(sixty.firstLine, 8);
    EXPECT_EQ(sixty.lastLine, 247);
    EXPECT_TRUE(sixty.blackOutside);
    Inputs wide;
    wide.layer2Enabled = true;
    wide.nr70 = 0x10;
    wide.origin = sixty;
    const FrameBox grid = NextPictureWindow::Screen(wide);
    EXPECT_EQ(grid.y, (16 + 8) * 2);
    EXPECT_EQ(grid.height, 240 * 2);
}
