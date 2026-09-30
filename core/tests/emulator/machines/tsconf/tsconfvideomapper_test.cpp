// TS-Conf video debug mapping (PLAN #42 phase 5; tsconfvideomapper.h): the
// graphics layer of every mode - layout, pixel sources, the pixels a byte
// feeds, text cells - checked against what ScreenTSConf actually draws.

#include "tsconffixture.h"

#include <cstring>
#include <vector>

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
