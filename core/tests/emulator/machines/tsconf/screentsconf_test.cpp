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
