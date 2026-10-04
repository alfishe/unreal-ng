// TS-Conf video (TSConf implementation-plan phase 3 VID items, GFX-3 for TXT;
// hardware-spec §4): geometry, CRAM colors, the ZX palette index, border, TXT.

#include "tsconffixture.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

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
                const uint32_t y = gy & 0xFF;
                const uint32_t x = gx & 0xFF;
                const uint8_t* page = ram + (vPage << 14);
                const uint8_t pixels = page[((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | (x >> 3)];
                const uint8_t attr = page[0x1800 + (y >> 3) * 32 + (x >> 3)];
                bool ink = (pixels >> (7 - (x & 7))) & 1;
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
                const uint32_t px = (gx * 2 + sub) & 0x3FF;
                const uint8_t* row = ram + (vPage << 14) + ((gy >> 3) & 0x3F) * 256;
                const uint8_t code = row[(px >> 3) & 0x7F];
                const uint8_t attr = row[128 + ((px >> 3) & 0x7F)];
                const uint8_t font = ram[((vPage ^ 1) << 14) + code * 8 + (gy & 7)];
                const bool on = (font >> (7 - (px & 7))) & 1;
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

/// VID-4: BORDER from #FE uses PAL_SEL; drawn outside the window
TEST_F(ScreenTSConf_Test, VID4_Border)
{
    Reg(TsConfReg::PalSel, 0x0A);
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
        EXPECT_EQ(hash, 0xD8C26F86D5C26DB1ull) << "the renderer's output changed";
}

/// VDAC ([MISC] TS_VDAC, hs §0.1 / §4.3): with a video DAC, CRAM bit 15 set
/// sends the channel's bits through the DAC (3 / 4 / 5 bit, full scale 255),
/// clear gives the PWM-compatible linear curve (0..24, then full); no VDAC
/// keeps the 2-bit DAC + PWM average. STATUS reports the build, the VDAC
/// builds have BLT2, and the renderer follows the setting
TEST_F(ScreenTSConf_Test, VDAC_CurvesStatusAndRender)
{
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x0010, 0), 0xFFAA0000u) << "no VDAC: unchanged";
    EXPECT_EQ(ScreenTSConf::CramToRgba(12 << 10, 3), 0xFF00007Fu) << "linear: 12 of 24";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x7FFF, 3), 0xFFFFFFFFu) << "linear saturates from 24";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x8000 | (16 << 10), 3), 0xFF000083u) << "5 bit: 16 of 31";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x8000 | (31 << 10), 3), 0xFF0000FFu) << "5 bit: 31 is full";
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

/// D6 (vdac2-tdd.md §2.1): the VDAC2 build (STATUS 7) shows the Evo colors
/// through the card's CPLD table exactly: PAL_SEL = 1 is level << 3 (top 248),
/// PAL_SEL = 0 is the card's linear table, round(v * 255 / 24), full from 24
TEST_F(ScreenTSConf_Test, VDAC2_CardTable)
{
    static constexpr uint8_t kCard[25] = {0,   10,  21,  31,  42,  53,  63,  74,  85,  95,  106, 117, 127,
                                          138, 149, 159, 170, 181, 191, 202, 213, 223, 234, 245, 255};
    for (uint32_t v = 0; v < 32; v++)
    {
        const uint32_t linear = v < 25 ? kCard[v] : 255u;
        EXPECT_EQ(ScreenTSConf::CramToRgba(static_cast<uint16_t>(v), 7), 0xFF000000u | (linear << 16))
            << "linear blue " << v;
        EXPECT_EQ(ScreenTSConf::CramToRgba(static_cast<uint16_t>(0x8000 | (v << 10)), 7), 0xFF000000u | (v << 3))
            << "direct red " << v;
    }
    EXPECT_EQ(ScreenTSConf::CramToRgba(0x7FFF, 7), 0xFFFFFFFFu) << "linear saturates";
    EXPECT_EQ(ScreenTSConf::CramToRgba(0xFFFF, 7), 0xFFF8F8F8u) << "direct white is 248";
    EXPECT_EQ(ScreenTSConf::CramToRgba(11 << 5, 7), 0xFF007500u) << "117 where the truncating curve gives 116";
    EXPECT_EQ(ScreenTSConf::CramToRgba(11 << 5, 3), 0xFF007400u) << "the 5-bit VDAC build keeps its curve";
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
