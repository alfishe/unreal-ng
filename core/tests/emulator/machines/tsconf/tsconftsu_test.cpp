// TS-Conf TSU (TSConf implementation-plan phase 4 TSU-1...5; hardware-spec
// §4.4): sprites, tiles, layer order, the descriptor cap, the TS window and
// how the TSU mixes with graphics and border.

#include "tsconffixture.h"

#include "emulator/platforms/tsconf/tsconftsu.h"
#include "emulator/video/tsconf/screentsconf.h"

class TsConfTsu_Test : public TsConfFixture
{
protected:
    static constexpr uint8_t kSpritePage = 0x20;
    static constexpr uint8_t kTilePage = 0x28;
    static constexpr uint8_t kMapPage = 0x30;

    void SetUp() override
    {
        TsConfFixture::SetUp();
        // Clean graphics / map pages (the fixture tags every byte of RAM)
        for (uint16_t page = kSpritePage; page < kMapPage + 1; page++)
            std::memset(_memory->RAMPageAddress(page), 0, PAGE_SIZE);
        Reg(TsConfReg::SGPage, kSpritePage);
        Reg(TsConfReg::TMapPage, kMapPage);
        Reg(TsConfReg::T0GPage, kTilePage);
        Reg(TsConfReg::T1GPage, kTilePage);
    }

    /// 4 bpp pixel of the 512x512 bitmap at `page`
    void Pixel(uint8_t page, uint32_t x, uint32_t y, uint8_t nibble)
    {
        uint8_t& byte = _memory->RAMPageAddress(page & 0xF8)[y * 256 + x / 2];
        byte = (x & 1) ? static_cast<uint8_t>((byte & 0xF0) | nibble) : static_cast<uint8_t>((byte & 0x0F) | (nibble << 4));
    }

    void Sprite(uint32_t d, uint16_t w0, uint16_t w1, uint16_t w2)
    {
        TsConfState& ts = _decoder->GetState();
        ts.sfile[d * 3] = w0;
        ts.sfile[d * 3 + 1] = w1;
        ts.sfile[d * 3 + 2] = w2;
    }

    void Tile(uint32_t layer, uint32_t row, uint32_t col, uint16_t entry)
    {
        uint8_t* map = _memory->RAMPageAddress(kMapPage);
        map[row * 256 + layer * 128 + col * 2] = static_cast<uint8_t>(entry);
        map[row * 256 + layer * 128 + col * 2 + 1] = static_cast<uint8_t>(entry >> 8);
    }

    /// Render TS line `y` with the current registers
    std::vector<uint8_t> Line(uint32_t y, uint32_t width = 256, uint32_t budget = 448)
    {
        TsConfLine set;
        const TsConfState& ts = _decoder->GetState();
        set.palSel = ts.regs[TsConfReg::PalSel];
        set.t0GPage = ts.regs[TsConfReg::T0GPage];
        set.t1GPage = ts.regs[TsConfReg::T1GPage];
        set.t0XOffs = static_cast<uint16_t>(ts.regs[0x40] | ((ts.regs[0x41] & 1) << 8));
        set.t1XOffs = static_cast<uint16_t>(ts.regs[0x44] | ((ts.regs[0x45] & 1) << 8));
        // The prefetch runs ahead of the line as on the hardware (TS lines y - 24 .. y)
        TsConfTsu::MapRing ring{};
        for (int line = static_cast<int>(y) - 24; line <= static_cast<int>(y); line++)
            TsConfTsu::Prefetch(ts, _memory->RAMBase(), static_cast<uint32_t>(line + 16) & 0x1FF, ring);
        std::vector<uint8_t> out(width, 0xEE);
        uint32_t used = 0;
        if (!TsConfTsu::RenderLine(ts, set, _memory->RAMBase(), ring, y, width, out.data(), budget, used))
            std::fill(out.begin(), out.end(), 0);
        return out;
    }

    static constexpr uint16_t kActive = 0x2000;
    static constexpr uint16_t kLeap = 0x4000;
};

/// TSU-1: one sprite - position, size, palette, both flips, nibble 0 transparent
TEST_F(TsConfTsu_Test, TSU1_SingleSprite)
{
    Reg(TsConfReg::TConfig, 0x80);
    Pixel(kSpritePage, 8, 8 + 2, 5);   // bitmap cell (row 1, column 1), line 2, pixel 0
    Pixel(kSpritePage, 15, 8 + 2, 6);  // pixel 7
    Sprite(0, kActive | (1 << 9) | 10, 20, (3 << 12) | 0x041);  // Y 10, 16 high; X 20, 8 wide; PAL 3

    std::vector<uint8_t> line = Line(12);
    EXPECT_EQ(line[20], 0x35);
    EXPECT_EQ(line[27], 0x36);
    EXPECT_EQ(line[21], 0x00) << "nibble 0 is transparent";
    EXPECT_EQ(line[19], 0x00);
    EXPECT_EQ(Line(9)[20], 0x00) << "above the sprite";
    EXPECT_EQ(Line(26)[20], 0x00) << "16 lines: 10..25";

    Sprite(0, kActive | (1 << 9) | 10, 0x8000 | 20, (3 << 12) | 0x041);  // XF
    EXPECT_EQ(Line(12)[27], 0x35);
    EXPECT_EQ(Line(12)[20], 0x36);

    Sprite(0, 0x8000 | kActive | (1 << 9) | 10, 20, (3 << 12) | 0x041);  // YF: line 2 shows bitmap line 13
    EXPECT_EQ(Line(12)[20], 0x00);
    EXPECT_EQ(Line(23)[20], 0x35);

    Sprite(0, (1 << 9) | 10, 20, (3 << 12) | 0x041);  // not active
    EXPECT_EQ(Line(12)[20], 0x00);
}

/// TSU-2: layer order S0 < T0 < S1 < T1 < S2 (LEAP ends a sprite layer)
TEST_F(TsConfTsu_Test, TSU2_LayerOrder)
{
    Reg(TsConfReg::TConfig, 0x80 | 0x40 | 0x20);
    Reg(TsConfReg::PalSel, 0x00);
    Pixel(kSpritePage, 0, 0, 1);            // sprite bitmap cell 0 pixel -> nibble 1
    Pixel(kTilePage, 8, 0, 2);              // tile 1 pixel -> nibble 2
    Tile(0, 0, 0, 0x0001);                  // T0: tile 1, pal 0
    Tile(1, 0, 0, 0x2001);                  // T1: tile 1, pal 2

    Sprite(0, kActive | kLeap, 0, 0x1000);  // S0 (ends S0), PAL 1
    EXPECT_EQ(Line(0)[0], 0x22) << "T1 over T0 over S0";

    Reg(TsConfReg::TConfig, 0x80 | 0x20);   // T1 off
    EXPECT_EQ(Line(0)[0], 0x02) << "T0 over S0";

    Sprite(0, kLeap, 0, 0);                 // descriptor 0: inactive, still ends S0
    Sprite(1, kActive, 0, 0x4000);          // S1, PAL 4
    EXPECT_EQ(Line(0)[0], 0x41) << "S1 over T0";

    Reg(TsConfReg::TConfig, 0x80 | 0x40 | 0x20);
    EXPECT_EQ(Line(0)[0], 0x22) << "T1 over S1";

    Sprite(1, kActive | kLeap, 0, 0x4000);  // ends S1
    Sprite(2, kActive, 0, 0x5000);          // S2, PAL 5
    EXPECT_EQ(Line(0)[0], 0x51) << "S2 over T1";
}

/// TSU-2b: the THIRD LEAP ends the sprites (S2 runs to it, not to descriptor 84; [V] video_ts.v:263-274, the layer
/// machine skips every layer that has ended). Descriptors after it are never processed: a program ends its list with
/// a LEAP descriptor (zifi.spg: "exit") and may leave anything behind it - zifi.spg loads the whole 512-byte SFILE
/// from a table followed by ASCII text, and those descriptors drew colored noise over the list
TEST_F(TsConfTsu_Test, TSU2b_ThirdLeapEndsTheSprites)
{
    Reg(TsConfReg::TConfig, 0x80);
    Reg(TsConfReg::PalSel, 0x00);
    Pixel(kSpritePage, 0, 0, 1);  // bitmap cell 0, pixel 0 -> nibble 1

    Sprite(0, kLeap, 0, 0);                  // ends S0 (inactive)
    Sprite(1, kLeap, 0, 0);                  // ends S1 (inactive)
    Sprite(2, kActive, 0, 0x2000);           // S2, PAL 2: drawn
    Sprite(3, kLeap, 0, 0);                  // the third LEAP: S2 and the sprites end here
    Sprite(4, kActive, 8, 0x5000);           // behind it: X 8, PAL 5 - never processed
    Sprite(84, kActive, 16, 0x6000);         // nor this one

    std::vector<uint8_t> line = Line(0);
    EXPECT_EQ(line[0], 0x21) << "S2 before the third LEAP is drawn";
    EXPECT_EQ(line[8], 0x00) << "a descriptor behind the third LEAP is not";
    EXPECT_EQ(line[16], 0x00);

    // The LEAP descriptor itself is the last one of S2 and is drawn when it is active
    Sprite(3, kLeap | kActive, 24, 0x3000);  // X 24, PAL 3
    line = Line(0);
    EXPECT_EQ(line[24], 0x31) << "the LEAP descriptor belongs to the layer it ends";
    EXPECT_EQ(line[8], 0x00);

    // With fewer than three LEAPs S2 still runs to descriptor 84
    Sprite(3, kActive, 24, 0x3000);
    EXPECT_EQ(Line(0)[16], 0x61);
}

/// TSU-3: 85 descriptors per frame - descriptor 84 is drawn
TEST_F(TsConfTsu_Test, TSU3_DescriptorCap)
{
    Reg(TsConfReg::TConfig, 0x80);
    Pixel(kSpritePage, 0, 0, 7);
    Sprite(84, kActive, 100, 0x2000);
    EXPECT_EQ(Line(0)[100], 0x27);
}

/// TSU-4: tile 0 is skipped unless TxZ; index {PAL_SEL bank, entry pal, nibble}; X/Y offsets
TEST_F(TsConfTsu_Test, TSU4_TilesZeroAndIndex)
{
    Reg(TsConfReg::TConfig, 0x20);
    Reg(TsConfReg::PalSel, 0x20);         // T0 bank 2
    Pixel(kTilePage, 0, 0, 9);            // tile 0, pixel 0
    Pixel(kTilePage, 8 + 3, 5, 4);        // tile 1, pixel (3, 5)
    Tile(0, 0, 0, 0x3000);                // tile 0, pal 3
    EXPECT_EQ(Line(0)[0], 0x00) << "tile 0 skipped";
    Reg(TsConfReg::TConfig, 0x20 | 0x04); // T0Z
    EXPECT_EQ(Line(0)[0], 0x80 | 0x30 | 9);

    Tile(0, 0, 1, 0x1001);                // column 1: tile 1, pal 1
    EXPECT_EQ(Line(5)[8 + 3], 0x80 | 0x10 | 4);
    Reg(TsConfReg::T0XOffsL, 8);          // X offset one tile: column 1 at x 0
    EXPECT_EQ(Line(5)[3], 0x80 | 0x10 | 4);
    Reg(TsConfReg::T0XOffsL + 2, 5);      // T0 Y offset 5: TS line 0 shows tile line 5
    EXPECT_EQ(Line(0)[3], 0x80 | 0x10 | 4);
}

/// TSU-5 and the video plex: TSU over graphics, NOTSU, GFXOVR, the TS window over the border
TEST_F(TsConfTsu_Test, TSU5_MixingAndTheTsWindow)
{
    auto* screen = dynamic_cast<ScreenTSConf*>(_context->pScreen);
    ASSERT_NE(screen, nullptr);
    TsConfState& ts = _decoder->GetState();
    Pixel(kSpritePage, 0, 0, 3);
    Sprite(0, kActive, 0, 0x7000);        // X 0, Y 0: the TS window origin; index 0x73
    ts.cram[0x73] = 0x03E0;
    Ram(5, 0x0000) = 0x80;                // ZX ink dot at graphics (0, 0)
    Ram(5, 0x1800) = 0x07;
    ts.cram[0xF7] = 0x7FFF;

    auto pixel = [&](uint32_t dot, uint32_t line) {
        TsConfEngine& engine = _decoder->GetEngine();
        engine.OnMachineFrameRollover(TsConfEngine::kFrameTacts);
        engine.CatchUp(TsConfEngine::kFrameTacts - 1);
        screen->InitRaster();
        screen->RenderFrameBatch();
        uint32_t* fb = nullptr;
        size_t size = 0;
        screen->GetFramebufferData(&fb, &size);
        return fb[(line - 32) * 720 + (dot - 88) * 2];
    };

    Reg(TsConfReg::TConfig, 0x80);
    EXPECT_EQ(pixel(140, 80), ScreenTSConf::CramToRgba(0x03E0)) << "sprite at the rres 0 window origin, over the ink";
    Reg(TsConfReg::VConfig, 0x08);        // GFXOVR
    EXPECT_EQ(pixel(140, 80), ScreenTSConf::CramToRgba(0x7FFF)) << "visible graphics dot wins";
    Reg(TsConfReg::VConfig, 0x10);        // NOTSU
    EXPECT_EQ(pixel(140, 80), ScreenTSConf::CramToRgba(0x7FFF));

    Reg(TsConfReg::VConfig, 0x20);        // NOGFX: the window shows the border, the TSU still draws
    Reg(TsConfReg::Border, 0x44);
    ts.cram[0x44] = 0x001F;
    EXPECT_EQ(pixel(140, 80), ScreenTSConf::CramToRgba(0x03E0));
    EXPECT_EQ(pixel(141, 80), ScreenTSConf::CramToRgba(0x001F)) << "border in the graphics window";

    Reg(TsConfReg::VConfig, 0x00);
    Reg(TsConfReg::TConfig, 0x81);        // 360x288 TS window: origin (88, 32), over the border
    EXPECT_EQ(pixel(88, 32), ScreenTSConf::CramToRgba(0x03E0));
    EXPECT_EQ(pixel(140, 80), ScreenTSConf::CramToRgba(0x7FFF)) << "no sprite at the graphics origin now";
}

/// TSU-7: a coarse Y offset change reaches the picture through the prefetch
/// ring (~16 lines late); the fine bits act at once
TEST_F(TsConfTsu_Test, TSU7_CoarseYActsThroughThePrefetch)
{
    Reg(TsConfReg::TConfig, 0x20);
    for (uint32_t tile = 1; tile <= 8; tile++)
        for (uint32_t line = 0; line < 8; line++)
            Pixel(kTilePage, tile * 8, line, static_cast<uint8_t>(tile));  // tile t, pixel 0: nibble t
    for (uint32_t row = 0; row < 64; row++)
        for (uint32_t col = 0; col < 64; col++)
            Tile(0, row, col, static_cast<uint16_t>((row & 7) + 1));       // map row k: tile (k & 7) + 1

    TsConfEngine& engine = _decoder->GetEngine();
    auto tsuAt = [&](uint32_t y) { return engine.TsuPixel(80 + y, 140); };  // rres 0 window at (140, 80)

    engine.OnMachineFrameRollover(TsConfEngine::kFrameTacts);
    _z80->t = 0;
    engine.CatchUp((80 + 40) * TsConfEngine::kLineTacts + 100);
    _z80->t = (80 + 40) * TsConfEngine::kLineTacts + 100;
    EXPECT_EQ(tsuAt(40), 40 / 8 + 1);
    Reg(TsConfReg::T0XOffsL + 2, 8);  // T0 Y offset: one tile row down (coarse)
    engine.CatchUp(TsConfEngine::kFrameTacts - 1);

    EXPECT_EQ(tsuAt(41), 41 / 8 + 1) << "the rows already fetched stay";
    EXPECT_EQ(tsuAt(63), 63 / 8 + 1) << "fetched at TS line 40, before the write";
    EXPECT_EQ(tsuAt(64), (64 / 8 + 1) % 8 + 1) << "fetched after the write";
    EXPECT_EQ(tsuAt(100), (100 / 8 + 1) % 8 + 1);
}
