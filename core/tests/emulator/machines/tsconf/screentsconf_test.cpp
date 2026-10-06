// TS-Conf video (TSConf implementation-plan phase 3 VID items, GFX-3 for TXT;
// hardware-spec §4): geometry, CRAM colors, the ZX palette index, border, TXT.

#include "tsconffixture.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/platforms/tsconf/tsconfgeometry.h"
#include "emulator/video/tsconf/screentsconf.h"


namespace
{
    /// The per-dot reference renderer (the renderer before TS-O2, kept
    /// verbatim as the oracle): color index of raster dot `dot`, half dot `sub`
    uint8_t ReferenceGraphics(const TsConfLine& set, const uint8_t* ram, uint32_t frameCounter, uint32_t wx, uint32_t sub,
                              bool& visible)
    {
        const uint8_t mode = set.vConfig & 0x03;
        const uint8_t palBank = static_cast<uint8_t>((set.palSel & 0x0F) << 4);
        const uint32_t gx = (wx + set.gxOffs) & 0x1FF;
        const uint32_t gy = set.cntRow & 0x1FF;
        const uint32_t vPage = set.vPage;
        switch (mode)
        {
            case 0:
            {
                // G_X_OFFS in ZX: the RTL's fetch order (GX-1, tools/machines/tsconf/rtl-sim): fetch k is the byte
                // pair at column 2 ((k >> 1) & 15), pixels if k is even, attributes if odd; the 16-pixel group m of
                // the stream (the window starts G_X_OFFS[1:0] pixels in) shows fetch c + 2m in the colors of c + 2m + 1,
                // c = G_X_OFFS[6:2]; past the 16th group the 33rd fetch, in the colors of fetch c + 31
                const uint32_t y = gy & 0xFF;
                const uint8_t* page = ram + (vPage << 14);
                const uint32_t c = (set.gxOffs >> 2) & 0x1F;
                const uint32_t s = wx + (set.gxOffs & 3);
                const uint32_t m = s >> 4;
                auto fetch = [&](uint32_t k) -> uint8_t {
                    const uint32_t column = 2 * ((k >> 1) & 0x0F) + ((s >> 3) & 1);
                    return (k & 1) ? page[0x1800 + (y >> 3) * 32 + column]
                                   : page[((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | column];
                };
                const uint8_t pixels = fetch(c + 2 * m);
                const uint8_t attr = fetch(m < 16 ? c + 2 * m + 1 : c + 31);
                bool ink = (pixels >> (7 - (s & 7))) & 1;
                if ((attr & 0x80) && ((frameCounter >> 4) & 1))
                    ink = !ink;
                visible = ink;
                const uint8_t bright = (attr & 0x40) ? 0x08 : 0x00;
                return static_cast<uint8_t>(palBank | bright | (ink ? (attr & 0x07) : ((attr >> 3) & 0x07)));
            }
            case 1:
            {
                const uint8_t byte = ram[((vPage & 0xF8) << 14) | (gy << 8) | (gx >> 1)];
                const uint8_t nibble = (gx & 1) ? (byte & 0x0F) : (byte >> 4);
                visible = nibble != 0;
                return static_cast<uint8_t>(palBank | nibble);
            }
            case 2:
            {
                const uint8_t pixel = ram[((vPage & 0xF0) << 14) | (gy << 9) | gx];
                visible = pixel != 0;
                return pixel;
            }
            default:
            {
                // G_X_OFFS in TXT (GX-1): character pairs from (n + 3) >> 2, n = G_X_OFFS >> 2, the stream starting
                // 2 G_X_OFFS[1:0] hires pixels in; phase n & 3: 0 glyphs, 1 and 2 raw codes (1 in the previous
                // pair's attributes), 3 glyph then raw code
                const uint32_t n = set.gxOffs >> 2;
                const uint32_t phase = n & 3;
                const uint32_t s = 2 * wx + sub + 2 * (set.gxOffs & 3);
                const uint32_t pair = (((n + 3) >> 2) + (s >> 4)) & 0x3F;
                const uint32_t half = (s >> 3) & 1;
                const uint8_t* row = ram + (vPage << 14) + ((gy >> 3) & 0x3F) * 256;
                const uint8_t code = row[2 * pair + half];
                const uint8_t attr = row[128 + (phase == 1 ? (2 * pair + 126 + half) & 0x7F : 2 * pair + half)];
                const bool glyph = phase == 0 || (phase == 3 && half == 0);
                const uint8_t font = glyph ? ram[((vPage ^ 1) << 14) + code * 8 + (gy & 7)] : code;
                const bool on = (font >> (7 - (s & 7))) & 1;
                visible = on;
                return static_cast<uint8_t>(palBank | (on ? (attr & 0x0F) : (attr >> 4)));
            }
        }
    }

    uint8_t ReferenceDot(const TsConfState& ts, const TsConfEngine& engine, const uint8_t* ram, uint32_t frameCounter,
                         uint32_t dot, uint32_t line, uint32_t sub)
    {
        const TsConfLine& set = engine.Line(line);
        const uint8_t vConfig = set.vConfig;
        const bool text = (vConfig & 0x03) == 3;
        const uint8_t palBank = static_cast<uint8_t>((set.palSel & 0x0F) << 4);
        const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(vConfig);
        const uint8_t border = ts.regs[TsConfReg::Border];
        const uint8_t tsu = engine.TsuPixel(line, dot);
        const bool tsuVisible = (tsu & 0x0F) && !(vConfig & 0x10);
        uint8_t index;
        const bool inWindow = dot >= win.x0 && dot < static_cast<uint32_t>(win.x0 + win.w) && line >= win.y0 &&
                              line < static_cast<uint32_t>(win.y0 + win.h);
        if (!inWindow)
        {
            index = tsuVisible ? tsu : border;
        }
        else
        {
            const bool noGfx = vConfig & 0x20;
            bool gfxVisible = false;
            const uint8_t gfx = noGfx ? border : ReferenceGraphics(set, ram, frameCounter, dot - win.x0, sub, gfxVisible);
            gfxVisible = gfxVisible && !noGfx;
            if (vConfig & 0x08)
                index = gfxVisible ? gfx : (tsuVisible ? tsu : border);
            else
                index = tsuVisible ? tsu : (noGfx ? border : gfx);
        }
        return text ? static_cast<uint8_t>(palBank | (index & 0x0F)) : index;
    }

    /// The whole visible frame (720 x 288) as the reference draws it
    void ReferenceFrame(const TsConfState& ts, const TsConfEngine& engine, const uint8_t* ram, uint32_t frameCounter,
                        uint32_t* out)
    {
        for (uint32_t line = 32; line < 320; line++)
            for (uint32_t dot = 88; dot < 448; dot++)
            {
                const bool text = (engine.Line(line).vConfig & 0x03) == 3;
                uint32_t* px = out + (line - 32) * 720 + (dot - 88) * 2;
                px[0] = ScreenTSConf::CramToRgba(ts.cram[ReferenceDot(ts, engine, ram, frameCounter, dot, line, 0)]);
                px[1] = ScreenTSConf::CramToRgba(ts.cram[ReferenceDot(ts, engine, ram, frameCounter, dot, line, text ? 1 : 0)]);
            }
    }
}  // namespace

class ScreenTSConf_Test : public TsConfFixture
{
protected:
    ScreenTSConf* Screen() { return dynamic_cast<ScreenTSConf*>(_context->pScreen); }

    /// Run the engine through one whole frame (every line latches the
    /// current registers), draw it and read framebuffer pixel (x, y)
    uint32_t PixelAfterFrame(uint32_t x, uint32_t y)
    {
        TsConfEngine& engine = _decoder->GetEngine();
        engine.OnMachineFrameRollover(TsConfEngine::kFrameTacts);
        engine.CatchUp(TsConfEngine::kFrameTacts - 1);
        Screen()->InitRaster();
        Screen()->RenderFrameBatch();
        uint32_t* buffer = nullptr;
        size_t size = 0;
        Screen()->GetFramebufferData(&buffer, &size);
        return buffer[y * 720 + x];
    }

    /// Framebuffer x of raster dot `dot` (2 px per dot from dot 88), y of raster line
    static uint32_t Fx(uint32_t dot) { return (dot - 88) * 2; }
    static uint32_t Fy(uint32_t line) { return line - 32; }
};

/// VID-1: one 720x288 geometry; ZX mode at reset
TEST_F(ScreenTSConf_Test, VID1_Geometry)
{
    ASSERT_NE(Screen(), nullptr);
    Screen()->InitRaster();
    EXPECT_EQ(Screen()->GetVideoMode(), M_TSZX);
    uint32_t* buffer = nullptr;
    size_t size = 0;
    Screen()->GetFramebufferData(&buffer, &size);
    EXPECT_EQ(size, 720u * 288u * 4u);

    Reg(TsConfReg::VConfig, 0x03);
    EXPECT_EQ(Screen()->GetVideoMode(), M_TSTX);
}

/// hs §4.3: the no-VDAC PWM levels: ZX normal (0x10) and bright (0x18) channels
TEST_F(ScreenTSConf_Test, CramColors)
{
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x0000), 0xFF000000u);
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x0010), 0xFFAA0000u) << "blue, normal level: 2/3";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x6318), 0xFFFFFFFFu) << "bright white saturates";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x7FFF), 0xFFFFFFFFu);
}

/// VID-3: ZX palette index {PAL_SEL[3:0], BRIGHT, ink}
TEST_F(ScreenTSConf_Test, VID3_ZxPaletteIndex)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::PalSel, 0x02);
    ts.cram[0x29] = 0x7C00;                 // red
    Ram(5, 0x0000) = 0x80;                  // first pixel of the screen set
    Ram(5, 0x1800) = 0x41;                  // bright, ink 1, paper 0
    EXPECT_EQ(PixelAfterFrame(Fx(140), Fy(80)), ScreenTSConf::CramToRgba(0x7C00));
    EXPECT_EQ(PixelAfterFrame(Fx(140) + 2, Fy(80)), ScreenTSConf::CramToRgba(ts.cram[0x28])) << "paper";
}

/// VID-4: BORDER from #FE uses PAL_SEL (as latched at the line start); drawn outside the window
TEST_F(ScreenTSConf_Test, VID4_Border)
{
    Reg(TsConfReg::PalSel, 0x0A);
    _z80->tt = 300u << 8;  // the next line has latched it
    Out(0x00FE, 0x05);
    _decoder->GetState().cram[0xA5] = 0x03E0;  // green
    EXPECT_EQ(PixelAfterFrame(0, 0), ScreenTSConf::CramToRgba(0x03E0));
    EXPECT_EQ(PixelAfterFrame(Fx(139), Fy(100)), ScreenTSConf::CramToRgba(0x03E0)) << "left of the rres 0 window";
}

/// GFX-3: TXT - char + attr at row 0, font from V_PAGE ^ 1, 14 MHz pixels
TEST_F(ScreenTSConf_Test, GFX3_TextMode)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::VConfig, 0x03);          // TXT, rres 0
    Reg(TsConfReg::VPage, 0x10);
    Ram(0x10, 0x0000) = 'A';
    Ram(0x10, 0x0080) = 0x1E;               // paper 1, ink 14
    Ram(0x11, 'A' * 8 + 0) = 0x80;          // font: leftmost pixel of line 0
    ts.cram[0xFE] = 0x001F;
    ts.cram[0xF1] = 0x7C00;
    EXPECT_EQ(PixelAfterFrame(Fx(140), Fy(80)), ScreenTSConf::CramToRgba(0x001F)) << "ink {PAL_SEL, attr[3:0]}";
    EXPECT_EQ(PixelAfterFrame(Fx(140) + 1, Fy(80)), ScreenTSConf::CramToRgba(0x7C00)) << "second hires pixel: paper";
}

/// Mode 1 (16C): high nibble = left pixel
TEST_F(ScreenTSConf_Test, GFX1_SixteenColors)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::VConfig, 0x01);
    Reg(TsConfReg::VPage, 0x08);
    Reg(TsConfReg::PalSel, 0x03);
    Ram(0x08, 0x0000) = 0x12;
    ts.cram[0x31] = 0x0001;
    ts.cram[0x32] = 0x0002;
    EXPECT_EQ(PixelAfterFrame(Fx(140), Fy(80)), ScreenTSConf::CramToRgba(0x0001));
    EXPECT_EQ(PixelAfterFrame(Fx(141), Fy(80)), ScreenTSConf::CramToRgba(0x0002));
}

/// VID-5: ZX in rres 3 fills 360x288 from (88, 32); columns wrap at 32 bytes,
/// so graphics x 256 shows byte column 0 again
TEST_F(ScreenTSConf_Test, VID5_ZxInFullWindowWrapsColumns)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::VConfig, 0xC0);  // ZX, rres 3
    Ram(5, 0x0000) = 0x80;          // row 0, column 0: leftmost pixel set
    Ram(5, 0x1800) = 0x02;          // ink 2, paper 0
    ts.cram[0xF2] = 0x7C00;
    EXPECT_EQ(PixelAfterFrame(Fx(88), Fy(32)), ScreenTSConf::CramToRgba(0x7C00)) << "window origin";
    EXPECT_EQ(PixelAfterFrame(Fx(88 + 256), Fy(32)), ScreenTSConf::CramToRgba(0x7C00)) << "column wrap";
    EXPECT_EQ(PixelAfterFrame(Fx(89), Fy(32)), ScreenTSConf::CramToRgba(ts.cram[0xF0])) << "paper";
}

/// VID-9: ZX mode addresses with the low 8 bits of the 9-bit row counter ([V] video_mode.v:204-205 addr_zx_gfx =
/// {cnt_row[7:6], cnt_row[2:0], cnt_row[5:3], col}, addr_zx_atr = {110, cnt_row[7:3], col}; [U] drawers.cpp:104-105):
/// rows 192..255 read the attribute area as pixels, row 256 is row 0 again (TS-Conf audit, video row 21)
TEST_F(ScreenTSConf_Test, VID9_ZxRowsWrapAt256)
{
    TsConfState& ts = _decoder->GetState();
    std::memset(_memory->RAMPageAddress(5), 0, PAGE_SIZE);
    ts.cram[0xF2] = 0x7C00;
    ts.cram[0xF3] = 0x001F;

    // Row 200 = #C8: pixels at word {11, 000, 001, 0} = byte #1820, attributes at {110, 11001, 0} = byte #1B20
    Ram(5, 0x1820) = 0x80;
    Ram(5, 0x1B20) = 0x02;  // ink 2
    Reg(TsConfReg::GYOffsL, 200);
    EXPECT_EQ(PixelAfterFrame(Fx(140), Fy(80)), ScreenTSConf::CramToRgba(0x7C00)) << "row 200 at the window top";

    // Row 256: the counter's bit 8 is not an address bit
    Ram(5, 0x0000) = 0x80;
    Ram(5, 0x1800) = 0x03;  // ink 3
    Reg(TsConfReg::GYOffsL, 0x00);
    Reg(TsConfReg::GYOffsH, 0x01);
    EXPECT_EQ(PixelAfterFrame(Fx(140), Fy(80)), ScreenTSConf::CramToRgba(0x001F)) << "row 256 shows row 0";
}

/// VID-10: FLASH from a 5-bit frame counter, phase = bit 4: 16 frames as drawn, 16 with ink and paper swapped, for
/// attribute bit 7 only ([V] video_sync.v:202-209 flash = flash_ctr[4], +1 at frame_start_s; video_render.v:43;
/// [U] drawers.cpp:132 frame_counter & 0x10) (TS-Conf audit, video row 24)
TEST_F(ScreenTSConf_Test, VID10_FlashSwapsEvery16Frames)
{
    TsConfState& ts = _decoder->GetState();
    std::memset(_memory->RAMPageAddress(5), 0, PAGE_SIZE);
    Ram(5, 0x0000) = 0x80;  // pixel set at (0, 0)
    Ram(5, 0x1800) = 0x81;  // FLASH, ink 1, paper 0
    Ram(5, 0x0001) = 0x80;  // pixel set at (8, 0)
    Ram(5, 0x1801) = 0x01;  // no FLASH, ink 1
    ts.cram[0xF1] = 0x7C00;
    ts.cram[0xF0] = 0x03E0;
    const uint32_t ink = ScreenTSConf::CramToRgba(0x7C00);
    const uint32_t paper = ScreenTSConf::CramToRgba(0x03E0);

    struct Case
    {
        uint32_t frame;
        bool swapped;
    };
    for (const Case& c : {Case{15, false}, Case{16, true}, Case{31, true}, Case{32, false}})
    {
        SCOPED_TRACE(c.frame);
        _context->emulatorState.frame_counter = c.frame;
        EXPECT_EQ(PixelAfterFrame(Fx(140), Fy(80)), c.swapped ? paper : ink);
        uint32_t* buffer = nullptr;
        size_t size = 0;
        Screen()->GetFramebufferData(&buffer, &size);  // the same frame
        EXPECT_EQ(buffer[Fy(80) * 720 + Fx(148)], ink) << "attribute bit 7 clear: never swapped";
    }
}

/// AUTO-1: the screen report names the TS mode with its geometry, its format
/// and the RAM pages it reads
TEST_F(ScreenTSConf_Test, AUTO1_ScreenModeReport)
{
    Reg(TsConfReg::VConfig, 0x41);  // 16C, 320x200
    Reg(TsConfReg::VPage, 0x0A);
    ScreenState s = Screen()->DescribeScreenState();
    EXPECT_EQ(s.videoMode, "TS16 320x200");
    EXPECT_EQ(s.width, 320);
    EXPECT_EQ(s.height, 200);
    EXPECT_EQ(s.format.bpp, 4);
    ASSERT_EQ(s.activeRamPages.size(), 8u);
    EXPECT_EQ(s.activeRamPages[0], 0x08);

    Reg(TsConfReg::VConfig, 0x83);  // TXT, 320x240
    s = Screen()->DescribeScreenState();
    EXPECT_EQ(s.videoMode, "TSTX 320x240");
    EXPECT_EQ(s.format.textColumns, 80);
    EXPECT_EQ(s.format.textRows, 30);

    Reg(TsConfReg::VConfig, 0xC2);
    EXPECT_EQ(Screen()->DescribeScreenState().videoMode, "TS256 360x288");
    Reg(TsConfReg::VConfig, 0x00);
    EXPECT_EQ(Screen()->DescribeScreenState().videoMode, "TSZX 256x192");
}

/// TS-O2 equivalence gate: the span renderer against the per-dot reference
/// renderer (ReferenceFrame above, the renderer before TS-O2) over 128 random
/// setups - every mode x geometry x NOTSU / NOGFX / GFXOVR, random offsets,
/// PAL_SEL, BORDER, CRAM, RAM, T_CONFIG and sprite descriptors, so graphics,
/// TSU and border mix in every way - pixel for pixel, drawn whole and in
/// random chunks. The pinned hash (recorded with the old renderer itself)
/// keeps the reference honest. UNREALNG_RECORD_TSCONF_EQUIV=1 prints it.
/// Over the 50 ms budget (~0.7 s): 384 full-frame renders, a third of them by
/// the slow reference; the only exhaustive check of the renderer
TEST_F(ScreenTSConf_Test, TSO2_RendererMatchesTheReference)
{
    TsConfState& ts = _decoder->GetState();
    uint32_t seed = 0x12345678u;
    auto next = [&seed] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    uint8_t* ram = _memory->RAMBase();
    for (size_t i = 0; i < 4u * 1024 * 1024; i += 4)
    {
        const uint32_t v = next();
        std::memcpy(ram + i, &v, 4);
    }

    TsConfEngine& engine = _decoder->GetEngine();
    uint32_t* buffer = nullptr;
    size_t size = 0;
    const uint32_t frameEnd = TsConfEngine::kFrameTacts;
    auto frameHash = [&](uint64_t h) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(buffer);
        for (size_t i = 0; i < size; i++)
            h = (h ^ bytes[i]) * 0x100000001b3ULL;
        return h;
    };

    uint64_t hash = 0xcbf29ce484222325ULL;
    uint64_t refHash = 0xcbf29ce484222325ULL;
    for (uint32_t setup = 0; setup < 128; setup++)
    {
        // V_CONFIG: mode + geometry from the setup number, NOTSU / NOGFX / GFXOVR from it too
        const uint8_t vConfig = static_cast<uint8_t>((setup & 0x03) | (((setup >> 2) & 0x03) << 6) | (((setup >> 4) & 0x07) << 3));
        Reg(TsConfReg::VConfig, vConfig);
        Reg(TsConfReg::VPage, static_cast<uint8_t>(next()));
        Reg(TsConfReg::GXOffsL, static_cast<uint8_t>(next()));
        Reg(TsConfReg::GXOffsH, static_cast<uint8_t>(next()));
        Reg(TsConfReg::GYOffsL, static_cast<uint8_t>(next()));
        Reg(TsConfReg::GYOffsH, static_cast<uint8_t>(next()));
        Reg(TsConfReg::PalSel, static_cast<uint8_t>(next()));
        Reg(TsConfReg::Border, static_cast<uint8_t>(next()));
        Reg(TsConfReg::TConfig, static_cast<uint8_t>(next()));
        Reg(TsConfReg::TMapPage, static_cast<uint8_t>(next()));
        Reg(TsConfReg::T0GPage, static_cast<uint8_t>(next()));
        Reg(TsConfReg::T1GPage, static_cast<uint8_t>(next()));
        Reg(TsConfReg::SGPage, static_cast<uint8_t>(next()));
        for (uint8_t r = 0x40; r < 0x48; r++)
            Reg(r, static_cast<uint8_t>(next()));
        for (uint16_t& c : ts.cram)
            c = static_cast<uint16_t>(next());
        for (uint16_t& s : ts.sfile)
            s = static_cast<uint16_t>(next());

        engine.OnMachineFrameRollover(frameEnd);
        engine.CatchUp(frameEnd - 1);
        Screen()->InitRaster();
        Screen()->RenderFrameBatch();
        Screen()->GetFramebufferData(&buffer, &size);
        ASSERT_EQ(size, 720u * 288u * 4u);
        const uint64_t whole = frameHash(0xcbf29ce484222325ULL);
        std::vector<uint32_t> reference(720u * 288u);
        ReferenceFrame(ts, engine, ram, _context->emulatorState.frame_counter, reference.data());
        for (size_t i = 0; i < reference.size(); i++)
        {
            if (buffer[i] != reference[i])
            {
                ADD_FAILURE() << "setup " << setup << " vConfig 0x" << std::hex << int(vConfig) << ": pixel (" << std::dec
                              << i % 720 << ", " << i / 720 << ") 0x" << std::hex << buffer[i] << ", reference 0x"
                              << reference[i];
                break;
            }
        }
        std::memcpy(buffer, reference.data(), size);
        refHash = (refHash ^ frameHash(0xcbf29ce484222325ULL)) * 0x100000001b3ULL;

        std::memset(buffer, 0x5A, size);
        for (uint32_t t = 0; t < frameEnd;)
        {
            const uint32_t to = std::min(frameEnd - 1, t + next() % 900);
            Screen()->DrawRange(t, to);
            t = to + 1;
        }
        EXPECT_EQ(frameHash(0xcbf29ce484222325ULL), whole) << "setup " << setup << ": chunked draw differs";
        hash = (hash ^ whole) * 0x100000001b3ULL;
    }
    if (std::getenv("UNREALNG_RECORD_TSCONF_EQUIV"))
        std::printf("TSO2 hash 0x%016llXull, reference 0x%016llXull\n", static_cast<unsigned long long>(hash),
                    static_cast<unsigned long long>(refHash));
    else
        // Re-recorded 2026-10-05: G_X_OFFS in ZX / TXT follows the RTL's fetch order (GX1); the reference agrees pixel
        // for pixel (before: 2026-10-04, the random SFILE ending at its third LEAP, TSU2b)
        EXPECT_EQ(hash, 0x91937CB7D7649754ull) << "the renderer's output changed";
}

/// VDAC ([MISC] TS_VDAC, hs §0.1 / §4.3): with a video DAC, CRAM bit 15 set
/// sends the channel's bits through the DAC, clear gives the PWM-compatible
/// linear curve (0..24, then full); the 5-bit board's CPLD is the VDAC2 table
/// (VDAC2_CardTable); no VDAC keeps the 2-bit DAC + PWM average. STATUS
/// reports the build, the VDAC builds have BLT2, and the renderer follows the
/// setting
TEST_F(ScreenTSConf_Test, VDAC_CurvesStatusAndRender)
{
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x0010, 0), 0xFFAA0000u) << "no VDAC: unchanged";
    EXPECT_EQ(ScreenTSConf::CramToRgba(12 << 10, 3), 0xFF00007Fu) << "linear: 12 of 24";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x7FFF, 3), 0xFFFFFFFFu) << "linear saturates from 24";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x8000 | (16 << 10), 3), 0xFF000080u) << "5 bit: 16 << 3";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x8000 | (31 << 10), 3), 0xFF0000F8u) << "5 bit: 31 is 248 (the CPLD's {in, 3'b0})";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x8000 | (16 << 10), 1), 0xFF000091u) << "3 bit: code 4 of 7";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x8000 | 16, 2), 0xFF880000u) << "4 bit: code 8 of 15";

    EXPECT_EQ(In(0x00AF) & 0x07, 0) << "standard build";
    _context->config.ts_vdac = 3;
    _decoder->reset();
    EXPECT_EQ(In(0x00AF) & 0x07, 3) << "quartus_vdac";
    Reg(TsConfReg::VConfig, 0x41);  // 16C
    Reg(TsConfReg::VPage, 0x10);
    Reg(TsConfReg::PalSel, 0x00);
    Ram(0x10, 0) = 0x10;            // first dot: colour 1
    _decoder->GetState().cram[1] = static_cast<uint16_t>(0x8000 | (20 << 5));
    EXPECT_EQ(PixelAfterFrame(Fx(108), Fy(76)), ScreenTSConf::CramToRgba(0x8000 | (20 << 5), 3));
    _context->config.ts_vdac = 0;
}

/// D6 (vdac2-tdd.md §2.1): the VDAC2 build (STATUS 7) and the 5-bit VDAC build
/// (STATUS 3) show the Evo colors through the board's CPLD table exactly - the
/// two CPLDs have the same `lut` (vdac/vdac1/cpld/top.v, vdac/vdac2/cpld/top.v):
/// PAL_SEL = 1 is level << 3 (top 248), PAL_SEL = 0 is the linear table,
/// round(v * 255 / 24), full from 24. The 5-bit build used to scale 31 to 255
/// and truncate the linear curve (seven levels one low): TS-Conf audit, video row 44
TEST_F(ScreenTSConf_Test, VDAC2_CardTable)
{
    static constexpr uint8_t kCard[25] = {0,   10,  21,  31,  42,  53,  63,  74,  85,  95,  106, 117, 127,
                                          138, 149, 159, 170, 181, 191, 202, 213, 223, 234, 245, 255};
    for (uint8_t vdac : {uint8_t(3), uint8_t(7)})
    {
        SCOPED_TRACE(int(vdac));
        for (uint32_t v = 0; v < 32; v++)
        {
            const uint32_t linear = v < 25 ? kCard[v] : 255u;
            EXPECT_EQ(ScreenTSConf::CramToRgba(static_cast<uint16_t>(v), vdac), 0xFF000000u | (linear << 16))
                << "linear blue " << v;
            EXPECT_EQ(ScreenTSConf::CramToRgba(static_cast<uint16_t>(0x8000 | (v << 10)), vdac), 0xFF000000u | (v << 3))
                << "direct red " << v;
        }
        EXPECT_EQ(ScreenTSConf::CramToRgba(0x7FFF, vdac), 0xFFFFFFFFu) << "linear saturates";
        EXPECT_EQ(ScreenTSConf::CramToRgba(0xFFFF, vdac), 0xFFF8F8F8u) << "direct white is 248";
        EXPECT_EQ(ScreenTSConf::CramToRgba(11 << 5, vdac), 0xFF007500u) << "117 where a truncating curve gives 116";
    }
}

/// TIM-5: a DMA CRAM write lands at its dot. A RAM -> CRAM transfer of 200
/// words (all blue) starts at line 99; the picture uses CRAM 150, written
/// about 300 DRAM accesses later - inside line 99. The dots of line 99 before
/// that write stay red, the ones after it are blue, even within one engine
/// step (without the placement the whole step would show the final colour)
TEST_F(ScreenTSConf_Test, TIM5_DmaCramWriteLandsAtItsDot)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::VConfig, 0x41);   // 16C 320x200: lines 76..275, dots 108..427
    Reg(TsConfReg::VPage, 0x10);
    Reg(TsConfReg::PalSel, 0x09);    // bank 9: nibble 6 = CRAM 150
    for (uint16_t page = 0x10; page < 0x18; page++)
        std::memset(_memory->RAMPageAddress(page), 0x66, PAGE_SIZE);
    // CRAM all red, written as a program does (the FM window at #4000): the
    // palette cache follows port and DMA writes
    Reg(TsConfReg::FMaps, 0x14);
    for (uint32_t i = 0; i < 256; i++)
    {
        Poke(static_cast<uint16_t>(0x4000 + i * 2), 0x00);
        Poke(static_cast<uint16_t>(0x4000 + i * 2 + 1), 0x7C);
    }
    Reg(TsConfReg::FMaps, 0x00);
    ASSERT_EQ(ts.cram[150], 0x7C00);
    for (uint32_t w = 0; w < 200; w++)
    {
        Ram(0x10 + 0x10, w * 2) = 0x1F;  // page #20: blue words
        Ram(0x10 + 0x10, w * 2 + 1) = 0x00;
    }

    TsConfEngine& engine = _decoder->GetEngine();
    engine.OnMachineFrameRollover(TsConfEngine::kFrameTacts);
    Screen()->InitRaster();
    Screen()->ResetPrevTstate();
    auto runTo = [&](uint32_t t) {
        _z80->t = t;
        engine.CatchUp(t);
        Screen()->UpdateScreen();
    };
    runTo(99 * TsConfEngine::kLineTacts);
    Reg(TsConfReg::DmaSAl, 0x00);
    Reg(TsConfReg::DmaSAh, 0x00);
    Reg(TsConfReg::DmaSAx, 0x20);
    Reg(TsConfReg::DmaDAl, 0x00);
    Reg(TsConfReg::DmaDAh, 0x00);
    Reg(TsConfReg::DmaDAx, 0x00);
    Reg(TsConfReg::DmaLen, 199);     // 200 words, one block
    Reg(TsConfReg::DmaNum, 0);
    Reg(TsConfReg::DmaCtrl, 0x8C);   // RAM -> CRAM
    // One long step (the CPU's steps draw the beam every instruction anyway;
    // within a step the write must still land at its own dot)
    runTo(102 * TsConfEngine::kLineTacts);

    uint32_t* buffer = nullptr;
    size_t size = 0;
    Screen()->GetFramebufferData(&buffer, &size);
    auto at = [&](uint32_t dot, uint32_t line) { return buffer[Fy(line) * 720 + Fx(dot)]; };
    const uint32_t red = ScreenTSConf::CramToRgba(0x7C00);
    const uint32_t blue = ScreenTSConf::CramToRgba(0x001F);
    EXPECT_EQ(at(120, 98), red);
    EXPECT_EQ(at(420, 98), red);
    EXPECT_EQ(at(120, 99), red) << "line 99 before the write";
    EXPECT_EQ(at(420, 99), blue) << "line 99 after the write";
    EXPECT_EQ(at(120, 100), blue);
}

/// GEOM-1: the frame's working window is the V_CONFIG graphics window in frame pixels, and the rendered
/// picture agrees: the pixel inside each corner is the graphics, the pixel just outside is the border.
/// One 720x288 frame for every mode (2 px per raster dot, 1 px per line), so a 256x192 window is
/// 512x192 pixels; the table's "screen = the whole frame" for TS-Conf was not the picture
TEST_F(ScreenTSConf_Test, GEOM1_WorkingWindowFollowsVConfig)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::PalSel, 0x0A);
    _z80->tt = 300u << 8;  // #FE takes the PAL_SEL latched at the line start: the next line's
    Out(0x00FE, 0x06);
    // The border: green. The fixture's RAM is tagged (page 5 is all 0x05), so the ZX ink is color 5 and
    // the paper 0: the border takes color 6, and nothing in the window is green
    ts.cram[0xA6] = 0x03E0;
    const uint32_t border = ScreenTSConf::CramToRgba(0x03E0);

    struct Case
    {
        uint8_t vConfig;
        PictureRect expected;
    };
    // Windows of hardware-spec §4.1, in frame pixels: x = (dot - 88) * 2, y = line - 32, width = dots * 2
    const Case cases[] = {
        {0x00, {104, 48, 512, 192}},  // 256x192
        {0x40, {40, 44, 640, 200}},   // 320x200
        {0x80, {40, 24, 640, 240}},   // 320x240
        {0xC0, {0, 0, 720, 288}},     // 360x288: the whole frame
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(testing::Message() << "V_CONFIG " << int(c.vConfig));
        Reg(TsConfReg::VConfig, c.vConfig);
        PixelAfterFrame(0, 0);  // a whole frame with these registers
        const PictureRect w = Screen()->WorkingWindow();
        EXPECT_EQ(w.x, c.expected.x);
        EXPECT_EQ(w.y, c.expected.y);
        EXPECT_EQ(w.width, c.expected.width);
        EXPECT_EQ(w.height, c.expected.height);

        uint32_t* buffer = nullptr;
        size_t size = 0;
        Screen()->GetFramebufferData(&buffer, &size);
        auto at = [&](int x, int y) { return buffer[y * 720 + x]; };
        const int right = w.x + w.width - 1;
        const int bottom = w.y + w.height - 1;
        EXPECT_NE(at(w.x, w.y), border) << "top-left corner is graphics";
        EXPECT_NE(at(right, bottom), border) << "bottom-right corner is graphics";
        if (w.x > 0)
        {
            EXPECT_EQ(at(w.x - 1, w.y), border) << "left of the window is border";
            EXPECT_EQ(at(right + 1, w.y), border) << "right of the window is border";
        }
        if (w.y > 0)
        {
            EXPECT_EQ(at(w.x, w.y - 1), border) << "above the window is border";
            EXPECT_EQ(at(w.x, bottom + 1), border) << "below the window is border";
        }
    }
}

/// A program that changes the video mode mid-frame (zifi.spg: a 256C header, a TXT list, a 256C status bar, every
/// frame) keeps what the beam already drew. All TS modes share one 720x288 frame and the hardware has no
/// framebuffer to clear, so a V_CONFIG mode change must not wipe the lines above it (it did: only the last
/// segment survived, a black screenshot with a status bar at the bottom)
TEST_F(ScreenTSConf_Test, VID6_ModeChangeMidFrameKeepsTheDrawnLines)
{
    TsConfState& ts = _decoder->GetState();
    Reg(TsConfReg::PalSel, 0x0A);
    _z80->tt = 300u << 8;  // #FE takes the PAL_SEL latched at the line start: the next line's
    Out(0x00FE, 0x06);
    ts.cram[0xA6] = 0x03E0;  // the border: green, nothing else in the picture is
    const uint32_t border = ScreenTSConf::CramToRgba(0x03E0);
    Reg(TsConfReg::VConfig, 0x00);

    TsConfEngine& engine = _decoder->GetEngine();
    engine.OnMachineFrameRollover(TsConfEngine::kFrameTacts);
    Screen()->InitRaster();
    Screen()->ResetPrevTstate();
    auto runTo = [&](uint32_t line) {
        const uint32_t t = line * TsConfEngine::kLineTacts;
        _z80->t = t;
        engine.CatchUp(t);
        Screen()->UpdateScreen();
    };
    runTo(60);
    Reg(TsConfReg::VConfig, 0x01);  // ZX -> 16C
    runTo(90);
    Reg(TsConfReg::VConfig, 0x03);  // 16C -> TXT
    runTo(120);
    Reg(TsConfReg::VConfig, 0x02);  // TXT -> 256C
    runTo(150);

    uint32_t* buffer = nullptr;
    size_t size = 0;
    Screen()->GetFramebufferData(&buffer, &size);
    auto at = [&](uint32_t dot, uint32_t line) { return buffer[Fy(line) * 720 + Fx(dot)]; };
    EXPECT_EQ(at(100, 40), border) << "a line drawn in the first mode";
    EXPECT_EQ(at(100, 70), border) << "a line drawn in the second mode";
    EXPECT_EQ(at(100, 100), border) << "a line drawn in the third mode";
    EXPECT_EQ(at(100, 130), border) << "a line of the last mode";
}

/// GEOM-2: with T_CONFIG[0] the TSU works in the whole 360x288 window and its sprites and tiles show over the
/// border. The picture then is that whole window, not the V_CONFIG graphics window (area=screen cut the sprites
/// that sit on the border). Nothing to show beyond the V_CONFIG window when no TSU layer is on, or NOTSU hides it
TEST_F(ScreenTSConf_Test, GEOM2_WorkingWindowIncludesTheTsuWindow)
{
    const PictureRect small{104, 48, 512, 192};  // V_CONFIG 256x192
    const PictureRect whole{0, 0, 720, 288};
    struct Case
    {
        uint8_t vConfig;
        uint8_t tConfig;
        PictureRect expected;
        const char* why;
    };
    const Case cases[] = {
        {0x00, 0x00, small, "no TSU layer, window bit off"},
        {0x00, 0x01, small, "window bit on but no layer enabled: nothing to show outside"},
        {0x00, 0x80, small, "sprites on, window bit off: the TSU stays in the graphics window"},
        {0x00, 0x81, whole, "sprites on, 360x288 TS window"},
        {0x00, 0x21, whole, "tile layer 0 on, 360x288 TS window"},
        {0x00, 0x41, whole, "tile layer 1 on, 360x288 TS window"},
        {0x10, 0x81, small, "NOTSU hides the TSU"},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.why);
        Reg(TsConfReg::VConfig, c.vConfig);
        Reg(TsConfReg::TConfig, c.tConfig);
        PixelAfterFrame(0, 0);
        const PictureRect w = Screen()->WorkingWindow();
        EXPECT_EQ(w.x, c.expected.x);
        EXPECT_EQ(w.y, c.expected.y);
        EXPECT_EQ(w.width, c.expected.width);
        EXPECT_EQ(w.height, c.expected.height);
    }
}

/// GX-1: G_X_OFFS in ZX and TXT mode as the RTL draws it. The offset loads the DRAM column counter (cstart =
/// G_X_OFFS >> 2, video_mode.v x_offs_mode) and the fetch type follows the counter, so it is not a pixel scroll:
/// ZX scrolls 8 x G_X_OFFS[6:2] + G_X_OFFS[1:0] pixels and swaps pixels and attributes when G_X_OFFS[2] is odd,
/// TXT scrolls by character pairs and shows raw codes for a nonzero G_X_OFFS[3:2] (TS-Conf audit, video rows
/// 28-29). The reference lines come from the real Verilog run in tools/machines/tsconf/rtl-sim (Verilator):
/// window line 9 with 8 border dots on each side, the memory filled as its harness does
TEST_F(ScreenTSConf_Test, GX1_GxOffsMatchesTheRtl)
{
    // The harness's memory images (rtl-sim/harness.cpp FillZx / FillTxt)
    for (int y = 0; y < 192; y++)
        for (int c = 0; c < 32; c++)
            Ram(0x05, static_cast<uint16_t>(((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | c)) =
                static_cast<uint8_t>(0x81 | ((c & 0x1F) << 1) | ((y & 1) << 6));
    for (int r = 0; r < 24; r++)
        for (int c = 0; c < 32; c++)
        {
            const int ink = c & 7;
            const int paper = (ink + 1 + (c >> 3)) & 7;
            Ram(0x05, static_cast<uint16_t>(0x1800 + r * 32 + c)) = static_cast<uint8_t>(((r & 1) << 6) | (paper << 3) | ink);
        }
    for (int r = 0; r < 64; r++)
        for (int c = 0; c < 128; c++)
        {
            const int ink = c & 15;
            const int paper = (ink + 1 + ((c >> 4) & 7)) & 15;
            Ram(0x10, static_cast<uint16_t>(r * 256 + c)) = static_cast<uint8_t>(0x80 | (c & 0x7F));
            Ram(0x10, static_cast<uint16_t>(r * 256 + 128 + c)) = static_cast<uint8_t>((paper << 4) | ink);
        }
    for (int ch = 0; ch < 256; ch++)
        for (int l = 0; l < 8; l++)
            Ram(0x11, static_cast<uint16_t>(ch * 8 + l)) = static_cast<uint8_t>(ch * 0x1D + 0x35 + l * 0x40);

    // A distinct color per CRAM index, so the framebuffer gives the index back
    TsConfState& ts = _decoder->GetState();
    std::map<uint32_t, int> indexOf;
    for (int i = 0; i < 256; i++)
    {
        ts.cram[i] = static_cast<uint16_t>(((i >> 4) << 10) | ((i & 15) << 5));
        indexOf[ScreenTSConf::CramToRgba(ts.cram[i])] = i;
    }
    ASSERT_EQ(indexOf.size(), 256u);
    Reg(TsConfReg::Border, 0xEE);
    Reg(TsConfReg::PalSel, 0x00);

    int lines = 0;
    for (const char* name : {"machines/tsconf/rtl-sim/zx-gxoffs.txt", "machines/tsconf/rtl-sim/txt-gxoffs.txt"})
    {
        std::ifstream file(TestPathHelper::GetTestDataPath(name));
        ASSERT_TRUE(file.good()) << name;
        std::string text;
        while (std::getline(file, text))
        {
            if (text.empty() || text[0] == '#')
                continue;
            std::istringstream in(text);
            unsigned vConfig = 0, gx = 0, perDot = 0;
            in >> std::hex >> vConfig >> std::dec >> gx >> perDot;
            std::vector<int> expected;
            for (unsigned v; in >> std::hex >> v;)
                expected.push_back(static_cast<int>(v));
            SCOPED_TRACE(std::string(name) + " V_CONFIG " + std::to_string(vConfig) + " G_X_OFFS " + std::to_string(gx));

            Reg(TsConfReg::VConfig, static_cast<uint8_t>(vConfig));
            Reg(TsConfReg::VPage, (vConfig & 3) == 3 ? 0x10 : 0x05);
            Reg(TsConfReg::GXOffsL, static_cast<uint8_t>(gx));
            Reg(TsConfReg::GXOffsH, static_cast<uint8_t>(gx >> 8));
            const TsConfGeometry::Window& win = TsConfGeometry::WindowOf(static_cast<uint8_t>(vConfig));
            const uint32_t y = Fy(win.y0 + 9);
            PixelAfterFrame(0, 0);
            uint32_t* buffer = nullptr;
            size_t size = 0;
            Screen()->GetFramebufferData(&buffer, &size);

            std::vector<int> actual;
            for (uint32_t dot = win.x0 - 8u; dot < win.x0 + win.w + 8u; dot++)
                for (uint32_t p = 0; p < perDot; p++)
                    actual.push_back(indexOf.count(buffer[y * 720 + Fx(dot) + p]) ? indexOf[buffer[y * 720 + Fx(dot) + p]] : -1);
            ASSERT_EQ(actual.size(), expected.size());
            size_t first = 0;
            while (first < actual.size() && actual[first] == expected[first])
                first++;
            EXPECT_EQ(first, actual.size()) << "first difference at index " << first << ": "
                                            << (first < actual.size() ? actual[first] : 0) << " vs the RTL's "
                                            << (first < actual.size() ? expected[first] : 0);
            lines++;
        }
    }
    EXPECT_EQ(lines, 174);
}
