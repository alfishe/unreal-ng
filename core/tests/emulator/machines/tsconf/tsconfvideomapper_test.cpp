// TS-Conf video debug mapping (PLAN #42 phase 5; tsconfvideomapper.h): the
// graphics layer of every mode - layout, pixel sources, the pixels a byte
// feeds, text cells - checked against what ScreenTSConf actually draws.

#include "tsconffixture.h"

#include <cstring>
#include <vector>

#include "emulator/state/devicestate.h"
#include "emulator/video/map/videomapservice.h"
#include "emulator/video/tsconf/screentsconf.h"
#include "emulator/video/tsconf/tsconfvideomapper.h"

using namespace videomap;

class TsConfVideoMapper_Test : public TsConfFixture
{
protected:
    ScreenTSConf* Screen() { return dynamic_cast<ScreenTSConf*>(_context->pScreen); }

    /// The engine through a whole frame with the current registers, then drawn
    void Frame()
    {
        TsConfEngine& engine = _decoder->GetEngine();
        engine.OnMachineFrameRollover(TsConfEngine::kFrameTacts);
        engine.CatchUp(TsConfEngine::kFrameTacts - 1);
        Screen()->InitRaster();
        Screen()->RenderFrameBatch();
    }

    void Noise(uint32_t seed)
    {
        uint8_t* ram = _memory->RAMBase();
        for (size_t i = 0; i < 4u * 1024 * 1024; i++)
        {
            seed = seed * 1103515245u + 12345u;
            ram[i] = static_cast<uint8_t>(seed >> 16);
        }
        TsConfState& ts = _decoder->GetState();
        for (uint16_t& c : ts.cram)
        {
            seed = seed * 1103515245u + 12345u;
            c = static_cast<uint16_t>(seed >> 8);
        }
    }
};

/// MAP-1: layout per mode - one layer in 14 MHz pixels over the V_CONFIG window
TEST_F(TsConfVideoMapper_Test, MAP1_Layout)
{
    VideoMapService service(_context);
    Reg(TsConfReg::VConfig, 0x41);  // 16C 320x200
    Frame();
    VideoLayout layout = service.Layout();
    EXPECT_TRUE(layout.mapped);
    EXPECT_EQ(layout.family, "tsconf");
    ASSERT_EQ(layout.layers.size(), 1u);
    EXPECT_EQ(layout.layers[0].id, "ts16");
    EXPECT_EQ(layout.layers[0].surface.width, 640);
    EXPECT_EQ(layout.layers[0].surface.height, 200);
    EXPECT_EQ(layout.layers[0].window.firstLine, 76);
    EXPECT_EQ(layout.layers[0].window.firstT, 54);
    EXPECT_EQ(layout.layers[0].window.dotsPerT, 4);
    EXPECT_EQ(layout.fb.surfaceLeft, 40);
    EXPECT_EQ(layout.fb.surfaceTop, 44);

    Reg(TsConfReg::VConfig, 0x83);  // TXT 320x240
    Frame();
    layout = service.Layout();
    EXPECT_EQ(layout.layers[0].id, "tstx");
    EXPECT_EQ(layout.layers[0].surface.textColumns, 80);
    EXPECT_EQ(layout.layers[0].surface.textRows, 30);
}

/// MAP-2: the worked example of tsconfvideomapper.h (16C, V_PAGE #10)
TEST_F(TsConfVideoMapper_Test, MAP2_SixteenColourSources)
{
    Reg(TsConfReg::VConfig, 0x41);
    Reg(TsConfReg::VPage, 0x10);
    Reg(TsConfReg::PalSel, 0x00);
    Ram(0x10, 0x0001) = 0x5A;
    _decoder->GetState().cram[0x0A] = 0x7C00;
    Frame();
    const PixelSources p = VideoMapService(_context).SourcesAt(0, 6, 0);
    ASSERT_TRUE(p.valid);
    ASSERT_EQ(p.contribution.sources.size(), 2u);
    const SourceRef& byte = p.contribution.sources[0];
    EXPECT_EQ(byte.space, Space::Ram);
    EXPECT_EQ(byte.page, 0x10);
    EXPECT_EQ(byte.offset, 1u);
    EXPECT_EQ(byte.bitMask, 0x0F) << "dot 3: the low nibble";
    EXPECT_EQ(p.contribution.colourIndex, 0x0A);
    const SourceRef& cram = p.contribution.sources[1];
    EXPECT_EQ(cram.space, Space::Palette);
    EXPECT_EQ(cram.offset, 0x14u);
    EXPECT_EQ(cram.width, 2);
    EXPECT_EQ(p.finalRgb, ScreenTSConf::CramToRgba(0x7C00));
    EXPECT_TRUE(p.renderedKnown);
    EXPECT_EQ(p.renderedRgb, p.finalRgb);
}

/// MAP-3: the mapper's colour is the rendered one - every mode x geometry with
/// random RAM, CRAM, offsets and PAL_SEL (TSU off: the graphics layer is the picture)
TEST_F(TsConfVideoMapper_Test, MAP3_ColourMatchesTheRenderer)
{
    Noise(7);
    VideoMapService service(_context);
    uint32_t seed = 99;
    auto next = [&seed] { return seed = seed * 1103515245u + 12345u, seed >> 8; };
    for (uint8_t geometry = 0; geometry < 4; geometry++)
    {
        for (uint8_t mode = 0; mode < 4; mode++)
        {
            SCOPED_TRACE(testing::Message() << "mode " << int(mode) << " geometry " << int(geometry));
            Reg(TsConfReg::VConfig, static_cast<uint8_t>((geometry << 6) | mode));
            Reg(TsConfReg::VPage, static_cast<uint8_t>(next()));
            Reg(TsConfReg::GXOffsL, static_cast<uint8_t>(next()));
            Reg(TsConfReg::GXOffsH, static_cast<uint8_t>(next() & 1));
            Reg(TsConfReg::GYOffsL, static_cast<uint8_t>(next()));
            Reg(TsConfReg::PalSel, static_cast<uint8_t>(next()));
            Reg(TsConfReg::TConfig, 0x00);
            Frame();
            const VideoLayout layout = service.Layout();
            ASSERT_EQ(layout.layers.size(), 1u);
            const SurfaceDesc& surface = layout.layers[0].surface;
            for (int sample = 0; sample < 400; sample++)
            {
                const uint32_t x = next() % surface.width;
                const uint32_t y = next() % surface.height;
                const PixelSources p = service.SourcesAt(0, x, y);
                ASSERT_TRUE(p.valid) << x << "," << y;
                ASSERT_TRUE(p.renderedKnown);
                ASSERT_EQ(p.finalRgb, p.renderedRgb) << "pixel " << x << "," << y;
            }
        }
    }
}

/// MAP-4: PixelsFor is the inverse of SourcesAt - the first (memory) source of
/// a pixel feeds an area that covers that pixel
TEST_F(TsConfVideoMapper_Test, MAP4_PixelsForCoversTheSourcePixel)
{
    Noise(11);
    VideoMapService service(_context);
    uint32_t seed = 5;
    auto next = [&seed] { return seed = seed * 1103515245u + 12345u, seed >> 8; };
    for (uint8_t vConfig : {uint8_t(0x00), uint8_t(0x41), uint8_t(0x82), uint8_t(0xC3), uint8_t(0x43)})
    {
        SCOPED_TRACE(int(vConfig));
        Reg(TsConfReg::VConfig, vConfig);
        Reg(TsConfReg::VPage, static_cast<uint8_t>(0x20 + (next() & 0x0F)));
        Reg(TsConfReg::GXOffsL, static_cast<uint8_t>(next()));
        Reg(TsConfReg::GYOffsL, static_cast<uint8_t>(next()));
        Frame();
        const SurfaceDesc surface = service.Layout().layers[0].surface;
        for (int sample = 0; sample < 60; sample++)
        {
            const uint32_t x = next() % surface.width;
            const uint32_t y = next() % surface.height;
            const PixelSources p = service.SourcesAt(0, x, y);
            ASSERT_TRUE(p.valid);
            for (size_t i = 0; i + 1 < p.contribution.sources.size(); i++)  // every memory source (not CRAM)
            {
                const std::vector<SurfaceArea> areas = service.PixelsFor(p.contribution.sources[i]);
                bool covered = false;
                for (const SurfaceArea& a : areas)
                    covered = covered || (x >= a.x && x < static_cast<uint32_t>(a.x + a.width) && y >= a.y &&
                                          y < static_cast<uint32_t>(a.y + a.height));
                ASSERT_TRUE(covered) << "pixel " << x << "," << y << " source " << i;
            }
        }
    }
}

/// MAP-5: text cells - code and attribute at V_PAGE, 128 + column; the border
TEST_F(TsConfVideoMapper_Test, MAP5_TextCellsAndBorder)
{
    Reg(TsConfReg::VConfig, 0x03);  // TXT 256x192: 64 x 24 cells
    Reg(TsConfReg::VPage, 0x10);
    Ram(0x10, 0x0000 + 5) = 'H';
    Ram(0x10, 0x0080 + 5) = 0x1E;
    Frame();
    VideoMapService service(_context);
    uint16_t columns = 0, rows = 0;
    std::vector<TextCell> cells;
    ASSERT_TRUE(service.Text(0, columns, rows, cells));
    EXPECT_EQ(columns, 64);
    EXPECT_EQ(rows, 24);
    EXPECT_EQ(cells[5].code, 'H');
    EXPECT_EQ(cells[5].attr, 0x1E);
    EXPECT_EQ(cells[5].codeSource.page, 0x10);
    EXPECT_EQ(cells[5].attrSource.offset, 0x85u);

    LayerContribution border;
    TsConfVideoMapper().BorderSources(service.State(), border);
    EXPECT_EQ(border.layer, "border");
    EXPECT_EQ(border.colourIndex, _decoder->GetState().regs[TsConfReg::Border]);
}

namespace
{
    /// Random TSU setup: tiles and sprites on, random SFILE (sizes, positions, LEAP), map / graphics pages
    void RandomTsu(TsConfFixture& f, PortDecoder_TSConf* decoder, uint32_t& seed, bool fullWindow)
    {
        auto next = [&seed] { return seed = seed * 1103515245u + 12345u, seed >> 8; };
        TsConfState& ts = decoder->GetState();
        for (uint16_t& w : ts.sfile)
            w = static_cast<uint16_t>(next());
        (void)f;
        decoder->WriteRegister(TsConfReg::TMapPage, static_cast<uint8_t>(next()));
        decoder->WriteRegister(TsConfReg::T0GPage, static_cast<uint8_t>(next()));
        decoder->WriteRegister(TsConfReg::T1GPage, static_cast<uint8_t>(next()));
        decoder->WriteRegister(TsConfReg::SGPage, static_cast<uint8_t>(next()));
        for (uint8_t r = 0x40; r < 0x48; r++)
            decoder->WriteRegister(r, static_cast<uint8_t>(next()));
        decoder->WriteRegister(TsConfReg::TConfig, static_cast<uint8_t>(0xE0 | (next() & 0x0C) | (fullWindow ? 1 : 0)));
    }
}

/// MAP-6: the TSU layer's colour is the rendered one wherever the TSU shows
/// (NOTSU and GFXOVR off: an opaque TSU pixel wins), in the graphics window
/// and in the full 360x288 TS window (T_CONFIG[0], its own framebuffer origin)
TEST_F(TsConfVideoMapper_Test, MAP6_TsuColourMatchesTheRenderer)
{
    Noise(21);
    VideoMapService service(_context);
    uint32_t seed = 3;
    auto next = [&seed] { return seed = seed * 1103515245u + 12345u, seed >> 8; };
    for (uint8_t vConfig : {uint8_t(0x41), uint8_t(0x82), uint8_t(0x00), uint8_t(0xC1)})
    {
        for (bool full : {false, true})
        {
            SCOPED_TRACE(testing::Message() << "vConfig " << int(vConfig) << " full " << full);
            Reg(TsConfReg::VConfig, vConfig);
            RandomTsu(*this, _decoder, seed, full);
            Frame();
            const VideoLayout layout = service.Layout();
            ASSERT_EQ(layout.layers.size(), 2u);
            EXPECT_EQ(layout.layers[1].id, "tsu");
            const SurfaceDesc& surface = layout.layers[1].surface;
            int opaque = 0;
            for (int sample = 0; sample < 600; sample++)
            {
                const uint32_t x = next() % surface.width;
                const uint32_t y = next() % surface.height;
                const PixelSources p = service.SourcesAt(1, x, y);
                if (!p.valid)
                    continue;
                opaque++;
                ASSERT_TRUE(p.renderedKnown);
                ASSERT_EQ(p.finalRgb, p.renderedRgb) << "pixel " << x << "," << y << " " << p.contribution.layer;
            }
            EXPECT_GT(opaque, 50) << "the random TSU draws most pixels";
        }
    }
}

/// MAP-7: a known sprite and a known tile name their descriptor / map word,
/// graphics byte and CRAM cell; PixelsFor of the SFILE word covers the sprite
TEST_F(TsConfVideoMapper_Test, MAP7_TsuSourcesOfASpriteAndATile)
{
    VideoMapService service(_context);
    TsConfState& ts = _decoder->GetState();
    std::memset(ts.sfile, 0, sizeof(ts.sfile));
    for (uint16_t page = 0x20; page < 0x40; page++)
        std::memset(_memory->RAMPageAddress(page), 0, PAGE_SIZE);
    Reg(TsConfReg::VConfig, 0x41);    // 16C 320x200: window at dot 108, line 76
    Reg(TsConfReg::SGPage, 0x20);
    Reg(TsConfReg::TMapPage, 0x30);
    Reg(TsConfReg::T0GPage, 0x28);
    // Sprite 5: at (10, 20), 8x8, tile 1 (bitmap x 8..15), palette 3; its graphics all colour 7
    ts.sfile[5 * 3] = static_cast<uint16_t>(0x2000 | 20);
    ts.sfile[5 * 3 + 1] = 10;
    ts.sfile[5 * 3 + 2] = static_cast<uint16_t>((3 << 12) | 1);
    for (uint32_t row = 0; row < 8; row++)
        for (uint32_t b = 0; b < 4; b++)
            Ram(0x20, row * 256 + 4 + b) = 0x77;
    // Tile layer 0: map entry (row 0, column 3) = tile 2, palette 1; tile graphics colour 5
    Ram(0x30, 0 * 256 + 0 * 128 + 3 * 2) = 2;
    Ram(0x30, 0 * 256 + 0 * 128 + 3 * 2 + 1) = 0x10;
    for (uint32_t row = 0; row < 8; row++)
        for (uint32_t b = 0; b < 4; b++)
            Ram(0x28, row * 256 + 8 + b) = 0x55;
    Reg(TsConfReg::TConfig, 0xA0);   // sprites + tile layer 0
    Frame();

    // Sprite pixel: TS window x 12 (surface 24), line 22
    PixelSources p = service.SourcesAt(1, 24, 22);
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.contribution.layer, "tsu.s0");
    ASSERT_EQ(p.contribution.sources.size(), 5u);
    EXPECT_EQ(p.contribution.sources[0].space, Space::SpriteRam);
    EXPECT_EQ(p.contribution.sources[0].offset, 5u * 6u);
    EXPECT_EQ(p.contribution.sources[2].offset, 5u * 6u + 4u);
    EXPECT_EQ(p.contribution.sources[3].role, SourceRole::SpriteGraphic);
    EXPECT_EQ(p.contribution.sources[3].page, 0x20);
    EXPECT_EQ(p.contribution.sources[3].offset, 2u * 256u + 5u) << "bitmap x 10 of row 2";
    EXPECT_EQ(p.contribution.colourIndex, 0x37);
    EXPECT_EQ(p.contribution.sources[4].offset, 0x37u * 2u);

    const std::vector<SurfaceArea> sprite = service.PixelsFor({Space::SpriteRam, 0, 0, 5u * 6u, 2, 0xFFFF, SourceRole::SpriteDescriptor});
    uint32_t pixels = 0;
    for (const SurfaceArea& a : sprite)
        pixels += static_cast<uint32_t>(a.width) * a.height;
    EXPECT_EQ(pixels, 8u * 8u * 2u) << "8x8 dots, 2 surface pixels each";

    // Tile pixel: column 3 = TS x 24..31, line 0 (no offsets)
    p = service.SourcesAt(1, 2 * 26, 0);
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.contribution.layer, "tsu.t0");
    EXPECT_EQ(p.contribution.sources[0].role, SourceRole::TileDescriptor);
    EXPECT_EQ(p.contribution.sources[0].page, 0x30);
    EXPECT_EQ(p.contribution.sources[0].offset, 6u);
    EXPECT_EQ(p.contribution.sources[0].width, 2);
    EXPECT_EQ(p.contribution.sources[1].page, 0x28);
    EXPECT_EQ(p.contribution.sources[1].offset, 9u) << "tile 2: bitmap x 16..23, x 18 of row 0";
    EXPECT_EQ(p.contribution.colourIndex, 0x15);
    EXPECT_FALSE(service.SourcesAt(1, 2 * 100, 150).valid) << "transparent";
}

/// MAP-8: PixelsFor of each TSU source covers the pixel it came from
TEST_F(TsConfVideoMapper_Test, MAP8_TsuPixelsForCoversTheSourcePixel)
{
    Noise(33);
    VideoMapService service(_context);
    uint32_t seed = 17;
    auto next = [&seed] { return seed = seed * 1103515245u + 12345u, seed >> 8; };
    Reg(TsConfReg::VConfig, 0x41);
    RandomTsu(*this, _decoder, seed, false);
    Frame();
    const SurfaceDesc surface = service.Layout().layers[1].surface;
    int checked = 0;
    for (int sample = 0; sample < 200 && checked < 12; sample++)
    {
        const uint32_t x = next() % surface.width;
        const uint32_t y = next() % surface.height;
        const PixelSources p = service.SourcesAt(1, x, y);
        if (!p.valid)
            continue;
        checked++;
        for (size_t i = 0; i + 1 < p.contribution.sources.size(); i++)
        {
            bool covered = false;
            for (const SurfaceArea& a : service.PixelsFor(p.contribution.sources[i]))
                covered = covered || (a.layer == "tsu" && x >= a.x && x < static_cast<uint32_t>(a.x + a.width) && y >= a.y &&
                                      y < static_cast<uint32_t>(a.y + a.height));
            ASSERT_TRUE(covered) << "pixel " << x << "," << y << " source " << i << " " << p.contribution.layer;
        }
    }
    EXPECT_EQ(checked, 12);
}

/// MAP-9: a CRAM cell feeds every pixel drawn with its index, in both layers;
/// the same question on the automation surfaces (DeviceState::VideoAddressIn)
TEST_F(TsConfVideoMapper_Test, MAP9_PaletteCellAndSpaces)
{
    Noise(45);
    VideoMapService service(_context);
    uint32_t seed = 23;
    auto next = [&seed] { return seed = seed * 1103515245u + 12345u, seed >> 8; };
    Reg(TsConfReg::VConfig, 0x41);
    RandomTsu(*this, _decoder, seed, false);
    Frame();
    const VideoLayout layout = service.Layout();
    for (size_t layer = 0; layer < 2; layer++)
    {
        int checked = 0;
        for (int sample = 0; sample < 200 && checked < 6; sample++)
        {
            const uint32_t x = next() % layout.layers[layer].surface.width;
            const uint32_t y = next() % layout.layers[layer].surface.height;
            const PixelSources p = service.SourcesAt(layer, x, y);
            if (!p.valid)
                continue;
            checked++;
            const SourceRef& cram = p.contribution.sources.back();
            ASSERT_EQ(cram.space, Space::Palette);
            bool covered = false;
            for (const SurfaceArea& a : service.PixelsFor(cram))
                covered = covered || (a.layer == layout.layers[layer].id && x >= a.x && x < static_cast<uint32_t>(a.x + a.width) &&
                                      y >= a.y && y < static_cast<uint32_t>(a.y + a.height));
            ASSERT_TRUE(covered) << "layer " << layer << " pixel " << x << "," << y;
        }
        EXPECT_EQ(checked, 6) << "layer " << layer;
    }

    StateNode palette = DeviceState::VideoAddressIn(_context, "palette", 0, 0x20);
    EXPECT_TRUE(palette["available"].b);
    EXPECT_EQ(palette["space"].s, "palette");
    StateNode sfile = DeviceState::VideoAddressIn(_context, "sprite_ram", 0, 6);
    EXPECT_TRUE(sfile["available"].b);
    StateNode bad = DeviceState::VideoAddressIn(_context, "vram", 0, 0);
    EXPECT_FALSE(bad["available"].b);
}
