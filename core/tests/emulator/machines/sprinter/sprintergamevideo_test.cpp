// SprinterGameVideo: the Game PLD configuration's picture (sprintergamevideo.h; Sprinter mame-gap-analysis V10,
// game-configuration.md) on a hand-built video RAM, without a machine: the square rules, the grid-offset register
// in beam order (the next square, across lines and frames, cleared by blank squares) and the beam-ordered
// drawing (any catch-up cadence, drawn or not, gives the same register and the same pixels).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "3rdparty/lodepng/lodepng.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/video/sprinter/sprintergamevideo.h"
#include "emulator/video/sprinter/sprintervideoram.h"

namespace
{
constexpr uint32_t kWidth = SprinterVideoRenderer::kVisibleWidth;
constexpr uint32_t kHeight = SprinterVideoRenderer::kVisibleLines;
constexpr uint32_t kFrameT = 320 * 224;

/// The byte every source pixel holds: row r, column c of the 1024 x 256 virtual screen
constexpr uint8_t Source(uint32_t row, uint32_t column)
{
    return static_cast<uint8_t>(column * 7 + row * 3 + 1);
}
}  // namespace

class SprinterGameVideo_Test : public ::testing::Test
{
protected:
    SprinterVideoRam _vram;
    SprinterGameVideo _video;
    SprinterVideoInputs _in;

    void SetUp() override
    {
        // Pen p = colour p (distinct: no palette lookup hides a wrong pen)
        for (uint32_t pen = 0; pen < SprinterVideoRam::kPens; pen++)
        {
            const uint32_t address = SprinterVideoRam::PenAddress(pen);
            _vram.Write(address, static_cast<uint8_t>(pen));
            _vram.Write(address + 1, static_cast<uint8_t>(pen >> 8));
            _vram.Write(address + 2, 0x5A);
        }
        // The virtual screen (columns #000-#2FF: the mode table and the palettes stay)
        for (uint32_t row = 0; row < 256; row++)
            for (uint32_t column = 0; column < 0x300; column++)
                _vram.Write(row * 1024 + column, Source(row, column));
        // Every square (a, b): graphics 320, palette 0, corner column 8a, row 8b, no grid offset
        for (uint8_t a = 0; a < 56; a++)
            for (uint8_t b = 0; b < 40; b++)
                SetMode(a, b, 0x20 | static_cast<uint8_t>((8 * a) >> 8), static_cast<uint8_t>(8 * a), static_cast<uint8_t>(8 * b), 0);
        _in.vram = _vram.Data();
        _in.palette = _vram.Palette();
        _in.lines = 320;
        _in.border = 3;
        _in.SetHold(0x77);
    }

    void SetMode(uint8_t a, uint8_t b, uint8_t m0, uint8_t m1, uint8_t m2, uint8_t m3)
    {
        const uint32_t address = SprinterVideoRam::ModeAddress(a, b, 0);
        _vram.Write(address, m0);
        _vram.Write(address + 1, m1);
        _vram.Write(address + 2, m2);
        _vram.Write(address + 3, m3);
    }
    void SetMode0(uint8_t a, uint8_t b, uint8_t m0) { _vram.Write(SprinterVideoRam::ModeAddress(a, b, 0), m0); }
    void SetMode3(uint8_t a, uint8_t b, uint8_t m3) { _vram.Write(SprinterVideoRam::ModeAddress(a, b, 0) + 3, m3); }

    std::vector<uint16_t> Pens() const
    {
        std::vector<uint16_t> pens(static_cast<size_t>(kWidth) * kHeight);
        _video.FramePens(_in, pens.data());
        return pens;
    }
    static uint16_t PenAt(const std::vector<uint16_t>& pens, uint32_t x, uint32_t y) { return pens[y * kWidth + x]; }

    /// The pen of the default squares at virtual position (a16, b8): square a16 / 16, b8 / 8, palette 0
    static uint16_t Expected(uint32_t a16, uint32_t b8)
    {
        const uint32_t a = a16 >> 4;
        const uint32_t b = b8 >> 3;
        return Source(8 * b + (b8 & 7), 8 * a + ((a16 & 15) >> 1));
    }
};

// Mode bytes: palette, a corner on any byte (column bits 9-8 in Mode0), one byte per two beam pixels
TEST_F(SprinterGameVideo_Test, Square_GraphicsFromAnyByteCorner)
{
    SetMode(2, 3, 0x40 | 0x20 | 0x01, 0x05, 0x13, 0);  // palette 1, column #105, row #13
    const std::vector<uint16_t> pens = Pens();
    const uint32_t x = 48 + 16 * 2;
    const uint32_t y = 16 + 8 * 3;
    for (uint32_t sub = 0; sub < 16; sub++)
        EXPECT_EQ(PenAt(pens, x + sub, y + 5), 0x100 + Source(0x13 + 5, 0x105 + sub / 2)) << "sub " << sub;
    EXPECT_EQ(PenAt(pens, x - 1, y), Expected(16 * 2 - 1, 8 * 3)) << "the square before: its own";
}

// The virtual screen is 1024 x 256: a corner near the right / bottom edge wraps inside it
TEST_F(SprinterGameVideo_Test, Square_SourceWrapsInsideTheVirtualScreen)
{
    SetMode(0, 0, 0x20 | 0x03, 0xFE, 0xFD, 0);  // column #3FE, row #FD
    const std::vector<uint16_t> pens = Pens();
    // Column #3FE + 3 = #401 -> #001 of the same row; row #FD + 5 = #102 -> row 2
    EXPECT_EQ(PenAt(pens, 48 + 6, 16 + 5), _vram.Read(2 * 1024 + 0x001));
    EXPECT_EQ(PenAt(pens, 48 + 2, 16 + 1), _vram.Read(0xFE * 1024 + 0x3FF));
}

// Mode0 = %111x xxxx: border (#400 + 9 x border), with bits 3-2 = %11 blank (#400) - also with bit 4 clear
TEST_F(SprinterGameVideo_Test, Square_BorderAndBlank)
{
    SetMode0(1, 1, 0xE0);
    SetMode0(2, 1, 0xEC);
    SetMode0(3, 1, 0xF3);
    const std::vector<uint16_t> pens = Pens();
    EXPECT_EQ(PenAt(pens, 48 + 16, 24), 0x400 + 9 * 3);
    EXPECT_EQ(PenAt(pens, 48 + 32, 24), 0x400);
    EXPECT_EQ(PenAt(pens, 48 + 48, 24), 0x400 + 9 * 3);
}

// Mode0 bit 2: Mode3 becomes the grid offset when the beam leaves the square - the square itself is drawn
// unshifted, the next one with (X x 2 pixels, Y lines), and it stays for the rest of the frame (every other
// square has bit 2 clear here), the next lines and the next frame included
TEST_F(SprinterGameVideo_Test, GridOffset_FromTheNextSquareOnInBeamOrder)
{
    SetMode(2, 4, 0x20 | 0x04, 16, 32, 0x31);  // bit 2, the default corner, offset X 1, Y 3
    const std::vector<uint16_t> before = Pens();
    const uint32_t y = 16 + 8 * 4;  // the first line of row 4

    EXPECT_EQ(PenAt(before, 48 + 32 + 15, y), Expected(32 + 15, 32)) << "square 2 itself: no shift";
    EXPECT_EQ(PenAt(before, 48 + 48, y), Expected(48 + 2, 32 + 3)) << "square 3: 2 pixels left, 3 lines up";
    EXPECT_EQ(PenAt(before, 48 + 48, y - 1), Expected(48, 31)) << "the line before: no offset yet";
    EXPECT_EQ(PenAt(before, 48, y + 1), Expected(2, 33 + 3)) << "the next line's square 0: the register stays";
    EXPECT_EQ(PenAt(before, 0, 0), Expected(896 - 48, 320 - 16)) << "this frame's start: no offset";

    // The next frame starts: it starts with the register the last square left
    _video.CloseFrame(_in, 1);
    EXPECT_EQ(_video.State().frameOffset, 0x31);
    const std::vector<uint16_t> next = Pens();
    EXPECT_EQ(PenAt(next, 0, 0), Expected(896 - 48 + 2, 320 - 16 + 3));
}

// Blank squares (%1111 11xx) have bit 2 too: their Mode3 loads the register (RELOAD.ASZ clears every Mode3
// "including border and blank!"); square 55's Mode3 is the offset of the next line's square 0
TEST_F(SprinterGameVideo_Test, GridOffset_BlankSquaresLoadTheirMode3)
{
    SetMode(2, 4, 0x24, 16, 32, 0x22);
    for (uint8_t a = 40; a < 56; a++)
        SetMode(a, 4, 0xFC, 0, 0, 0);  // the blanking squares of the row: offset 0 again
    SetMode3(55, 4, 0x05);             // ... but the last one sets X 5 for the next line's square 0
    const std::vector<uint16_t> pens = Pens();
    const uint32_t y = 16 + 8 * 4;
    EXPECT_EQ(PenAt(pens, 48 + 48, y), Expected(48 + 4, 32 + 2));
    // Square 55 is at x 32..47 of the next line (a16 = x - 48 mod 896): from x 48 on the offset is (5, 0)
    EXPECT_EQ(PenAt(pens, 48, y + 1), Expected(10, 33));
}

// The register runs in beam order however it is driven: one Advance, many small ones, drawn or not, and the
// redraw from the frame start all give the same pixels and the same register
TEST_F(SprinterGameVideo_Test, Advance_SameResultForAnyCadence)
{
    SetMode(7, 10, 0x24, 56, 80, 0x47);
    SetMode(30, 20, 0x24, 0xF0, 160, 0x18);
    std::vector<uint32_t> one(static_cast<size_t>(kWidth) * kHeight, 0);
    std::vector<uint32_t> many(one.size(), 0);
    std::vector<uint32_t> redraw(one.size(), 0);

    SprinterGameVideo a;
    a.Start(7, 0);
    a.Advance(_in, 7, kFrameT, one.data(), nullptr);

    SprinterGameVideo b;
    b.Start(7, 0);
    for (uint32_t t = 0; t < kFrameT;)
    {
        const uint32_t next = std::min(kFrameT, t + 1 + (t * 7919u) % 613u);
        // Part of the frame not drawn (a skipped render): the register must still run
        b.Advance(_in, 7, next, (t > 20000 && t < 30000) ? nullptr : many.data(), nullptr);
        t = next;
    }
    EXPECT_EQ(a.State().offset, b.State().offset);
    EXPECT_EQ(a.State().offset, 0x18);

    SprinterGameVideo c;
    c.Start(7, 0);
    c.Redraw(_in, 0, kFrameT, redraw.data(), nullptr);
    EXPECT_EQ(one, redraw) << "a redraw from the frame start";
    for (size_t i = 0; i < one.size(); i++)
    {
        const uint32_t t = static_cast<uint32_t>((i / kWidth) * 224 + (i % kWidth) / 4);
        if (t > 20000 && t < 31000)
            continue;  // not drawn (the last undrawn step can reach 613 T past 30 000)
        ASSERT_EQ(one[i], many[i]) << "pixel " << i;
    }

    // A position behind the state is ignored; the next frame closes this one first, whoever comes first
    b.Advance(_in, 7, 100, many.data(), nullptr);
    EXPECT_EQ(b.State().beamT, kFrameT);
    b.Advance(_in, 8, 100, nullptr, nullptr);
    EXPECT_EQ(b.State().frame, 8u);
    EXPECT_EQ(b.State().beamT, 100u);
    EXPECT_EQ(b.State().frameOffset, 0x18);
    b.CloseFrame(_in, 8);
    EXPECT_EQ(b.State().beamT, 100u) << "frame 8 is open already: nothing to close";

    // A frame not run at all (a skipped render without catch-ups) is closed in one go at the next frame start
    SprinterGameVideo d;
    d.Start(7, 0);
    d.CloseFrame(_in, 8);
    EXPECT_EQ(d.State().frame, 8u);
    EXPECT_EQ(d.State().beamT, 0u);
    EXPECT_EQ(d.State().frameOffset, 0x18) << "the same register as a drawn frame";
}

// MAME's picture of GAME_00 (BIOS 3.06, the MAME pack's disk with SYSTEM.BAT starting it; frame 1000) drawn again from
// MAME's own video RAM of that moment (testdata/machines/sprinter/reference/game/README.md): the graphics pixels
// (pens below #400; the border's colour depends on the #FE writes inside the frame) must agree but for the rule
// differences named in sprintergamevideo.h - MAME restarts each line's grid offset and switches it inside a square
TEST_F(SprinterGameVideo_Test, MameCapture_SamePictureFromMamesVideoRam)
{
    const auto dir = TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "sprinter" / "reference" / "game";
    std::vector<unsigned char> rgba;
    unsigned width = 0, height = 0;
    if (FileHelper::ReadFileToBuffer((dir / "mame-game00-306-vram.bin").string(), _vram.Data(), SprinterVideoRam::kSize) !=
            SprinterVideoRam::kSize ||
        lodepng::decode(rgba, width, height, (dir / "mame-game00-306.png").string()) != 0)
        GTEST_SKIP() << "the MAME capture is missing";
    ASSERT_EQ(width, kWidth);
    ASSERT_EQ(height, kHeight);
    _vram.RefreshPalette();

    const std::vector<uint16_t> pens = Pens();
    size_t graphics = 0, differ = 0;
    for (size_t i = 0; i < pens.size(); i++)
    {
        if (pens[i] >= 0x400)
            continue;
        graphics++;
        const uint32_t ours = _vram.Pen(pens[i]);
        const uint32_t mame = rgba[i * 4] | (rgba[i * 4 + 1] << 8) | (static_cast<uint32_t>(rgba[i * 4 + 2]) << 16);
        differ += ((ours ^ mame) & 0x00FFFFFFu) ? 1 : 0;
    }
    const double share = graphics ? static_cast<double>(differ) / static_cast<double>(graphics) : 1.0;
    std::printf("[ Game ] MAME frame 1000: %zu of %zu graphics pixels differ (%.2f%%)\n", differ, graphics, share * 100);
    EXPECT_GT(graphics, 100000u);
    EXPECT_LT(share, 0.05);
}
