// The pixel inspector of the Next picture (core/src/emulator/video/next/nextpixelinspector.h, design-layer-capture.md section 3.5): the winner,
// the colour and per layer why it lost or did not paint and where its pixel came from. A random sweep checks the recomputed sources against
// the recorded planes of the renderer (the inspector cannot invent a source that does not reproduce the pixel); hand-built scenes check the
// words and the addresses; a machine with a copper list checks that the register state of the line SEGMENT is what is recomputed from.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/goldentext.h"
#include "_helpers/nextreporthelper.h"
#include "_helpers/nextscene.h"
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextreportquery.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/next/nextlayerimage.h"
#include "emulator/video/next/nextpixelinspector.h"
#include "emulator/video/next/screennext.h"

using namespace NextLayer;
using NextReportHelper::Path;

namespace
{
/// One scene of the renderer with the record of every line it draws: Draw(y) renders line y into the frame and appends its segment
class SceneBase : public ::testing::Test
{
protected:
    static constexpr unsigned kPages = 16;
    std::vector<uint8_t> _ram = std::vector<uint8_t>(kPages * 0x4000, 0);
    NextVideoRegs _regs;
    NextSprites _sprites;
    NextVideoInputs _in;
    Frame _frame;
    std::vector<uint32_t> _line = std::vector<uint32_t>(NextVideoRenderer::kWidth);

    void SetUp() override
    {
        _regs.Reset();
        _sprites.Reset();
        _in.ram = _ram.data();
        _in.ramPages = kPages;
        _in.regs = &_regs;
        _in.sprites = &_sprites;
        _in.nr[0x14] = 0xE3;
        _in.nr[0x4A] = 0xE3;
        _in.nr[0x12] = 8;
        _in.nr[0x4B] = 0x00;  // the sprite transparency index
        _in.nr[0x4C] = 0x0F;  // the tilemap transparency index
        _frame.Allocate();
        _frame.BeginFrame();
    }
    uint8_t* Page(unsigned p) { return _ram.data() + static_cast<size_t>(p) * 0x4000; }
    void Draw(unsigned y)
    {
        const LineSink sink = _frame.Line(y);
        _in.layerOut = &sink;
        NextVideoRenderer::RecordSegment(_frame, y, 0, NextLayer::kWidth, _in);
        NextVideoRenderer::RenderLine(_in, y, _line.data());
        _in.layerOut = nullptr;
    }
    InspectEnv Env() const
    {
        InspectEnv env;
        env.ram = _ram.data();
        env.ramPages = kPages;
        env.regs = &_regs;
        env.sprites = &_sprites;
        return env;
    }
    StateNode Inspect(unsigned x, unsigned y, unsigned sx = 0) { return InspectPixel(_frame, Env(), x, y, sx); }
};

class NextPixel_Test : public SceneBase
{
protected:
    /// Attributes of one sprite through the engine's own port-side API: x, y grid, pattern, 8-bit
    void Sprite(unsigned slot, unsigned x, unsigned y, unsigned pattern)
    {
        _sprites.WriteSlotSelect(static_cast<uint8_t>(slot));
        _sprites.WriteAttribute(static_cast<uint8_t>(x));
        _sprites.WriteAttribute(static_cast<uint8_t>(y));
        _sprites.WriteAttribute(static_cast<uint8_t>((x >> 8) & 1));
        _sprites.WriteAttribute(static_cast<uint8_t>(0x80 | pattern));
    }
    void PatternByte(unsigned offset, uint8_t value)
    {
        _sprites.WriteSlotSelect(0);
        for (unsigned i = 0; i < offset; i++)
            _sprites.WritePattern(0);
        _sprites.WritePattern(value);
    }
};
}  // namespace

TEST_F(NextPixel_Test, TheUlaPaperAndTheBorderNameTheirBytes)
{
    Page(5)[0x0000] = 0x80;  // the leftmost pixel of the first paper row: ink
    Page(5)[0x1800] = 0x4A;  // bright, paper 1, ink 2
    _in.border = 3;
    Draw(32);
    Draw(0);
    StateNode paper = Inspect(32, 32);  // the grid pixel 32 is the first of the paper (a pixel is two sub-pixels)
    EXPECT_EQ(Path(paper, "ok"), "on");
    EXPECT_EQ(Path(paper, "winner.layer"), "ula");
    EXPECT_EQ(Path(paper, "layers.ula.wins"), "on");
    EXPECT_EQ(Path(paper, "layers.ula.source.kind"), "paper");
    EXPECT_EQ(Path(paper, "layers.ula.source.ram_page"), "5");
    EXPECT_EQ(Path(paper, "layers.ula.source.bitmap_offset"), "0x0000");
    EXPECT_EQ(Path(paper, "layers.ula.source.bitmap_byte"), "0x80");
    EXPECT_EQ(Path(paper, "layers.ula.source.attribute_offset"), "0x1800");
    EXPECT_EQ(Path(paper, "layers.ula.source.attribute_byte"), "0x4A");
    EXPECT_EQ(Path(paper, "layers.ula.source.ink_pixel"), "on");
    EXPECT_EQ(Path(paper, "layers.ula.source.palette_index"), "10") << "ink 2, bright";
    EXPECT_EQ(Path(paper, "colour.rgb9"), Path(paper, "layers.ula.rgb9"));
    // the next grid pixel is paper (bit 6 of the same byte is clear)
    EXPECT_EQ(Path(Inspect(33, 32), "layers.ula.source.ink_pixel"), "off");
    EXPECT_EQ(Path(Inspect(33, 32), "layers.ula.source.palette_index"), std::to_string(16 + 1 + 8)) << "paper 1, bright";
    // the border: port #FE bits 2:0 are the colour
    StateNode border = Inspect(0, 0);
    EXPECT_EQ(Path(border, "winner.layer"), "border");
    EXPECT_EQ(Path(border, "layers.ula.source.kind"), "border");
    EXPECT_EQ(Path(border, "layers.ula.source.colour"), "3");
    EXPECT_EQ(Path(border, "layers.ula.source.palette_index"), "19");
    // the layers that are off say so
    EXPECT_EQ(Path(paper, "layers.layer2.why"), "Layer 2 disabled (port #123B bit 1 / NR #69 bit 7)");
    EXPECT_EQ(Path(paper, "layers.tilemap.why"), "tilemap disabled (NR #6B bit 7)");
    EXPECT_EQ(Path(paper, "layers.sprites.why"), "sprites disabled (NR #15 bit 0)");
    EXPECT_EQ(Path(paper, "layers.layer2.opaque"), "off");
}

TEST_F(NextPixel_Test, Layer2NamesItsByteAndWhyItIsClippedOrCovered)
{
    _in.layer2Enable = true;
    Page(8)[(40 - 32) * 256 + (50 - 32)] = 0x77;  // the Layer 2 pixel under the grid pixel (50, 40): row 8, column 18 of the 256 x 192 screen
    Draw(40);
    StateNode p = Inspect(50, 40);
    EXPECT_EQ(Path(p, "winner.layer"), "layer2") << "SLU with no sprites: Layer 2 is above the ULA";
    EXPECT_EQ(Path(p, "layers.layer2.source.kind"), "bitmap");
    EXPECT_EQ(Path(p, "layers.layer2.source.resolution"), "256x192x8");
    EXPECT_EQ(Path(p, "layers.layer2.source.ram_page16"), "8");
    EXPECT_EQ(Path(p, "layers.layer2.source.offset"), "0x0812");
    EXPECT_EQ(Path(p, "layers.layer2.source.byte"), "0x77");
    EXPECT_EQ(Path(p, "layers.layer2.source.palette_index"), "119");
    EXPECT_EQ(Path(p, "layers.layer2.why"), "wins (layer order SLU)");
    EXPECT_EQ(Path(p, "layers.ula.why"), "layer order SLU: layer2 above ula");
    // the clip window of Layer 2 (NR #18): x 100-200
    _regs.WriteClipControl(0x01);
    for (uint8_t v : {100, 200, 0, 191})
        _regs.WriteClip(0, v);
    _frame.BeginFrame();
    Draw(40);
    p = Inspect(50, 40);
    EXPECT_EQ(Path(p, "winner.layer"), "ula");
    EXPECT_EQ(Path(p, "layers.layer2.opaque"), "off");
    EXPECT_EQ(Path(p, "layers.layer2.why"), "clipped (Layer 2 clip window NR #18)");
    EXPECT_EQ(Path(p, "layers.layer2.source.kind"), "clipped");
    // outside the 256 x 192 paper the mode draws nothing
    _regs.WriteClipControl(0x01);
    for (uint8_t v : {0, 255, 0, 191})
        _regs.WriteClip(0, v);
    _frame.BeginFrame();
    Draw(10);
    EXPECT_EQ(Path(Inspect(50, 10), "layers.layer2.why"), "outside the area this layer draws");
    // a pixel in the global transparency colour is not opaque
    _in.nr[0x14] = static_cast<uint8_t>(_regs.PaletteEntry(1, 0x77) >> 1);
    _frame.BeginFrame();
    Draw(40);
    p = Inspect(50, 40);
    EXPECT_EQ(Path(p, "layers.layer2.opaque"), "off");
    EXPECT_NE(Path(p, "layers.layer2.why").find("global transparency colour NR #14"), std::string::npos) << Path(p, "layers.layer2.why");
}

TEST_F(NextPixel_Test, TheTilemapNamesTheTileItsDefinitionByteAndWhyItIsTransparentOrBelowTheUla)
{
    _in.nr[0x6B] = 0x80;  // on, 40 columns, 16-bit map entries
    _in.nr[0x6E] = 0x20;  // map at #2000 of bank 5
    _in.nr[0x6F] = 0x30;  // tile definitions at #3000
    Page(5)[0x2000 + (3 * 40 + 2) * 2] = 5;     // the tile at column 2, row 3: tile 5
    Page(5)[0x2000 + (3 * 40 + 2) * 2 + 1] = 0x00;  // attribute: palette offset 0, no mirror, ULA below
    Page(5)[0x3000 + 5 * 32 + 2 * 4 + 1] = 0x3C;    // row 2 of tile 5, bytes 2-3 of the row: the pixel x 2 is the high nibble of byte 1 -> 3
    Draw(26);
    StateNode p = Inspect(18, 26);  // column 2 (x 16-23), row 3 (y 24-31), tile pixel (2, 2)
    EXPECT_EQ(Path(p, "layers.tilemap.source.kind"), "tile");
    EXPECT_EQ(Path(p, "layers.tilemap.source.column"), "2");
    EXPECT_EQ(Path(p, "layers.tilemap.source.row"), "3");
    EXPECT_EQ(Path(p, "layers.tilemap.source.map_bank"), "5");
    EXPECT_EQ(Path(p, "layers.tilemap.source.map_offset"), "0x20F4");
    EXPECT_EQ(Path(p, "layers.tilemap.source.tile"), "5");
    EXPECT_EQ(Path(p, "layers.tilemap.source.definition_offset"), "0x30A9");
    EXPECT_EQ(Path(p, "layers.tilemap.source.pixel_value"), "3");
    EXPECT_EQ(Path(p, "layers.tilemap.source.palette_index"), "3");
    EXPECT_EQ(Path(p, "layers.tilemap.opaque"), "on");
    EXPECT_EQ(Path(p, "winner.layer"), "tilemap");
    // the transparent index: the pixel is not painted and the word says which index
    Page(5)[0x3000 + 5 * 32 + 2 * 4 + 1] = 0xFC;
    _frame.BeginFrame();
    Draw(26);
    p = Inspect(18, 26);
    EXPECT_EQ(Path(p, "layers.tilemap.opaque"), "off");
    EXPECT_EQ(Path(p, "layers.tilemap.why"), "transparent (tile pixel index 15 is the tilemap transparent index NR #4C)");
    EXPECT_EQ(Path(p, "winner.layer"), "border") << "what is below shows";
    // attribute bit 0: the ULA is above this tile
    Page(5)[0x3000 + 5 * 32 + 2 * 4 + 1] = 0x3C;
    Page(5)[0x2000 + (3 * 40 + 2) * 2 + 1] = 0x01;
    _frame.BeginFrame();
    Draw(26);
    p = Inspect(18, 26);
    EXPECT_EQ(Path(p, "layers.tilemap.opaque"), "on");
    EXPECT_EQ(Path(p, "layers.tilemap.why"), "below the ULA (tile attribute bit 0, or 512 mode)");
    EXPECT_EQ(Path(p, "layers.tilemap.source.below_ula"), "on");
    EXPECT_EQ(Path(p, "winner.layer"), "border");
    // the tilemap clip window (NR #1B): x 100-159 (in pairs of pixels)
    _regs.WriteClipControl(0x08);
    for (uint8_t v : {50, 79, 0, 255})
        _regs.WriteClip(3, v);
    _frame.BeginFrame();
    Draw(26);
    EXPECT_EQ(Path(Inspect(18, 26), "layers.tilemap.why"), "clipped (tilemap clip window NR #1B)");
}

TEST_F(NextPixel_Test, ASpriteNamesItsNumberAndPatternByteAndTheOthersWhyTheyAreNotThere)
{
    _in.nr[0x15] = 0x01;  // sprites on, SLU
    Sprite(3, 100, 80, 0);
    PatternByte(7 * 16 + 5, 0x22);  // pattern 0, row 7, column 5
    Draw(87);
    StateNode p = Inspect(105, 87);
    EXPECT_EQ(Path(p, "winner.layer"), "sprite");
    EXPECT_EQ(Path(p, "layers.sprites.source.kind"), "sprite");
    EXPECT_EQ(Path(p, "layers.sprites.source.sprite"), "3");
    EXPECT_EQ(Path(p, "layers.sprites.source.pattern_offset"), "0x0075");
    EXPECT_EQ(Path(p, "layers.sprites.source.pattern_byte"), "0x22");
    EXPECT_EQ(Path(p, "layers.sprites.source.palette_index"), "34");
    EXPECT_EQ(Path(p, "layers.sprites.wins"), "on");
    EXPECT_EQ(Path(p, "layers.ula.why"), "layer order SLU: sprite above ula");
    // a pixel of the same sprite whose pattern byte is the transparent index
    p = Inspect(106, 87);
    EXPECT_EQ(Path(p, "layers.sprites.opaque"), "off");
    EXPECT_EQ(Path(p, "layers.sprites.why"), "transparent (sprite pattern byte equals the sprite transparency index NR #4B)");
    EXPECT_EQ(Path(p, "layers.sprites.source.sprite"), "3");
    // nothing covers this one
    EXPECT_EQ(Path(Inspect(200, 87), "layers.sprites.why"), "no sprite here");
}

TEST_F(NextPixel_Test, LayerOrderPriorityStencilAndBlendAreNamed)
{
    _in.layer2Enable = true;
    _in.nr[0x15] = 0x01 | (1 << 2);  // sprites on, LSU
    Sprite(0, 60, 50, 0);
    PatternByte(0, 0x41);
    Page(8)[(50 - 32) * 256 + (60 - 32)] = 0x21;
    Draw(50);
    StateNode p = Inspect(60, 50);
    EXPECT_EQ(Path(p, "layer_order"), "LSU");
    EXPECT_EQ(Path(p, "winner.layer"), "layer2");
    EXPECT_EQ(Path(p, "layers.sprites.why"), "layer order LSU: layer2 above sprites");
    EXPECT_EQ(Path(p, "layers.ula.why"), "layer order LSU: layer2 above ula");
    // a Layer 2 priority colour is above everything, whatever the order
    _in.nr[0x15] = 0x01;  // SLU: the sprite would win
    _frame.BeginFrame();
    Draw(50);
    EXPECT_EQ(Path(Inspect(60, 50), "winner.layer"), "sprite");
    _regs.WritePaletteControl(0x10);  // select Layer 2 palette 1 (NR #43 bits 6:4 = 1)
    _regs.WritePaletteIndex(0x21);
    _regs.WritePaletteValue9(0xFF);
    _regs.WritePaletteValue9(0x80);  // priority
    _regs.WritePaletteControl(0);
    _frame.BeginFrame();
    Draw(50);
    p = Inspect(60, 50);
    EXPECT_EQ(Path(p, "winner.layer"), "layer2");
    EXPECT_EQ(Path(p, "layers.sprites.why"), "Layer 2 priority colour is above everything");
    // blend (layer order 110): the result is the mix
    _in.nr[0x15] = 0x01 | (6 << 2);
    _in.nr[0x68] = 0x00;
    _frame.BeginFrame();
    Draw(50);
    p = Inspect(60, 50);
    EXPECT_EQ(Path(p, "layers.layer2.opaque"), "on");
    EXPECT_NE(Path(p, "winner.layer").find("blend"), std::string::npos) << Path(p, "winner.layer");
    EXPECT_NE(Path(p, "layers.layer2.why").find("mixed into the blend result"), std::string::npos) << Path(p, "layers.layer2.why");
}

TEST_F(NextPixel_Test, BadAndMissingInputsAreRefused)
{
    Draw(40);
    EXPECT_EQ(Path(Inspect(320, 0), "ok"), "off");
    EXPECT_EQ(Path(Inspect(320, 0), "status"), "bad-parameter");
    EXPECT_EQ(Path(Inspect(0, 256), "ok"), "off");
    EXPECT_EQ(Path(Inspect(0, 41), "status"), "no-frame") << "a line that was not drawn has no segment";
    NextPixelQuery query;
    std::string error;
    EXPECT_TRUE(NextPixelQueryFromStrings("319", "255", "1", query, error));
    EXPECT_EQ(query.x, 319u);
    EXPECT_EQ(query.y, 255u);
    EXPECT_EQ(query.sub, 1u);
    EXPECT_TRUE(NextPixelQueryFromStrings("0x10", "#20", "", query, error));
    EXPECT_EQ(query.x, 16u);
    EXPECT_EQ(query.y, 32u);
    EXPECT_EQ(query.sub, 0u);
    for (const char* bad : {"320", "-1", "zz", ""})
        EXPECT_FALSE(NextPixelQueryFromStrings(bad, "0", "", query, error)) << bad;
    EXPECT_FALSE(NextPixelQueryFromStrings("0", "256", "", query, error));
    EXPECT_FALSE(NextPixelQueryFromStrings("0", "0", "2", query, error));
    EXPECT_NE(error.find("sx must be 0 or 1"), std::string::npos) << error;
    EXPECT_FALSE(NextPixelQueryFromStrings("", "5", "", query, error));
    EXPECT_NE(error.find("x and y are required"), std::string::npos) << error;
}

class NextPixelRandom_Test : public ::testing::Test
{
protected:
    NextRandomScene _scene;
    Frame _frame;
    std::vector<uint32_t> _line = std::vector<uint32_t>(NextVideoRenderer::kWidth);
    void SetUp() override { _frame.Allocate(); }
};

// The inspector's recomputed sources must reproduce the recorded pixel: the palette entry behind the recomputed index is the colour of the plane,
// the recomputed opacity agrees with the record, the winner is the record's, and the composed colour recomputed from the record is the one the
// compositor drew. A source that was wrong could not pass this over 25 layer configurations of random memory
TEST_F(NextPixelRandom_Test, TheRecomputedSourcesReproduceTheRecordedPlanesAndTheComposedColour)
{
    InspectEnv env;
    env.ram = _scene.ram.data();
    env.ramPages = NextRandomScene::kPages;
    env.regs = &_scene.regs;
    env.sprites = &_scene.sprites;
    static const char* const kLayers[4] = {"ula", "layer2", "tilemap", "sprites"};
    unsigned checked = 0, indexed = 0;
    for (const NextSceneConfig& config : NextRandomScene::Configs())
    {
        _scene.Apply(config);
        _frame.BeginFrame();
        for (unsigned line = 0; line < NextRandomScene::Lines().size(); line += 3)
        {
            const unsigned y = NextRandomScene::Lines()[line];
            const LineSink sink = _frame.Line(y);
            _scene.in.layerOut = &sink;
            NextVideoRenderer::RecordSegment(_frame, y, 0, kWidth, _scene.in);
            NextVideoRenderer::RenderLine(_scene.in, y, _line.data());
            _scene.in.layerOut = nullptr;
            for (unsigned x = (y % 5) + 7; x < 320; x += 53)  // ~6 samples a line: the inspector builds a report tree per pixel
            {
                const unsigned sx = (x + y) & 1;
                const unsigned sub = x * 2 + sx;
                const size_t at = static_cast<size_t>(y) * kWidth + sub;
                const StateNode p = InspectPixel(_frame, env, x, y, sx);
                ASSERT_EQ(Path(p, "ok"), "on") << config.name;
                ASSERT_EQ(Path(p, "winner.layer"), IdName(_frame.ids[at])) << config.name << " " << x << "," << y;
                ASSERT_EQ(NextVideoRenderer::Rgba(ComposedColour(_frame, y, sub)), _line[sub]) << config.name << " " << x << "," << y << "." << sx;
                for (unsigned l = 0; l < 4; l++)
                {
                    const Pixel plane = _frame.plane[l][at];
                    const std::string base = std::string("layers.") + kLayers[l];
                    ASSERT_EQ(Path(p, base + ".opaque"), plane.opaque ? "on" : "off") << config.name << " " << kLayers[l] << " " << x << "," << y;
                    const std::string index = Path(p, base + ".source.palette_index");
                    if (plane.opaque && !index.empty())
                    {
                        const unsigned palette = std::stoul(Path(p, base + ".source.palette"));
                        const unsigned entry = _scene.regs.PaletteEntry(palette, std::stoul(index)) & 0x1FF;
                        ASSERT_EQ(entry, static_cast<unsigned>(plane.colour & 0x1FF))
                            << config.name << " " << kLayers[l] << " at " << x << "," << y << "." << sx << ": palette " << palette << " index " << index;
                        indexed++;
                    }
                    else if (plane.opaque)
                    {
                        // a painted pixel without a recomputed index: only the ULA border of a hi-res / full-ink screen may lack one
                        ASSERT_EQ(l, kUla) << config.name << " " << kLayers[l] << " at " << x << "," << y;
                    }
                    if (l == kSprites && plane.opaque)
                    {
                        ASSERT_EQ(Path(p, base + ".source.sprite"), std::to_string(_frame.info[at])) << config.name << " " << x << "," << y;
                    }
                    checked++;
                }
            }
        }
    }
    EXPECT_GT(checked, 2500u);
    EXPECT_GT(indexed, 500u) << "the sweep reproduces plenty of painted pixels from their recomputed source";
}

// region <on a machine>

class NextPixelMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        _context->pFeatureManager->setFeature(Features::kScreenHQ, true);
        _context->pMemory->UpdateFeatureCache();
        _context->pMemory->DirectWriteToZ80Memory(0xC000, 0xF3);  // DI; JR $
        _context->pMemory->DirectWriteToZ80Memory(0xC001, 0x18);
        _context->pMemory->DirectWriteToZ80Memory(0xC002, 0xFE);
        _context->pCore->GetZ80()->pc = 0xC000;
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    void Nr(uint8_t reg, uint8_t value)
    {
        Out(0x243B, reg);
        Out(0x253B, value);
    }
    void Frames(unsigned n)
    {
        for (unsigned i = 0; i < n; i++)
            _emulator->RunFrame(true);
    }
    uint8_t* Page(unsigned p) { return _context->pMemory->RAMPageAddress(p); }
    StateNode Pixel(unsigned x, unsigned y, unsigned sx = 0)
    {
        NextPixelQuery query;
        query.x = x;
        query.y = y;
        query.sub = sx;
        return DeviceState::NextPixel(_context, query);
    }
};

TEST_F(NextPixelMachine_Test, RefusalsAndTheOtherMachines)
{
    ScreenNext* screen = NextScreen(_context);
    ASSERT_NE(screen, nullptr);
    screen->SetLayerCapture(true);
    StateNode p = Pixel(10, 10);
    EXPECT_EQ(Path(p, "available"), "on");
    EXPECT_EQ(Path(p, "ok"), "off");
    EXPECT_EQ(Path(p, "status"), "no-frame") << "on, but no frame has run";
    screen->SetLayerCapture(false);
    EXPECT_EQ(Path(Pixel(10, 10), "status"), "layer-capture-off");
    Emulator* other = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(other, nullptr);
    NextPixelQuery query;
    EXPECT_EQ(Path(DeviceState::NextPixel(other->GetContext(), query), "available"), "off");
    EmulatorTestHelper::CleanupEmulator(other);
}

// A copper write in the middle of a line changes the Layer 2 bank: the pixels before and after it come from different pages, and the inspector
// recomputes each from the registers of its own line segment - the end-of-frame registers would name the wrong page for one of them
TEST_F(NextPixelMachine_Test, ThePixelsOfACopperLineAreRecomputedFromTheirOwnSegment)
{
    ScreenNext* screen = NextScreen(_context);
    screen->SetLayerCapture(true);
    Out(0x123B, 0x02);  // Layer 2 on
    Nr(0x70, 0x10);     // 320 x 256
    Nr(0x12, 8);
    // the 320 x 256 bitmap is 80K: from bank 8 it spans the 16K pages 8-12, from bank 9 pages 9-13; column 193 is in the fourth of them
    for (unsigned i = 0; i < 0x4000; i++)
    {
        Page(11)[i] = 0x11;
        Page(12)[i] = 0x22;
    }
    // WAIT line 60 hpos 20; MOVE NR #12 <- 9; WAIT line 120 hpos 30; MOVE NR #12 <- 8
    Nr(0x61, 0);
    Nr(0x62, 0);
    for (uint16_t w : {uint16_t(0x8000 | (20 << 9) | 60), uint16_t(0x1209), uint16_t(0x8000 | (30 << 9) | 120), uint16_t(0x1208), uint16_t(0xFFFF)})
    {
        Nr(0x60, static_cast<uint8_t>(w >> 8));
        Nr(0x60, static_cast<uint8_t>(w & 0xFF));
    }
    Nr(0x62, 0xC0);
    Frames(4);
    std::shared_ptr<const Frame> frame = screen->LastLayerFrame();
    ASSERT_NE(frame, nullptr);
    // the first line with two segments, and where the second begins
    unsigned line = 0;
    for (; line < kHeight && frame->lineSegments[line] < 2; line++)
    {
    }
    ASSERT_LT(line, kHeight) << "the copper cut no line";
    const Segment& second = frame->segments[frame->lineFirst[line] + 1];
    const unsigned boundary = second.fromX;  // sub-pixels
    ASSERT_GT(boundary, 4u);
    const StateNode before = Pixel((boundary - 2) / 2, line, (boundary - 2) & 1);
    const StateNode after = Pixel(boundary / 2, line, boundary & 1);
    EXPECT_EQ(Path(before, "layers.layer2.source.bank"), "8");
    EXPECT_EQ(Path(after, "layers.layer2.source.bank"), "9") << "the register state of the segment, not the end of the frame (8 again)";
    EXPECT_EQ(Path(before, "layers.layer2.source.ram_page16"), "11");
    EXPECT_EQ(Path(after, "layers.layer2.source.ram_page16"), "12");
    EXPECT_EQ(Path(before, "layers.layer2.source.byte"), "0x11");
    EXPECT_EQ(Path(after, "layers.layer2.source.byte"), "0x22");
    EXPECT_EQ(Path(before, "pixel.segment_to_x"), std::to_string(boundary));
    EXPECT_EQ(Path(after, "pixel.segment_from_x"), std::to_string(boundary));
    EXPECT_EQ(_ports->Board().Stored(0x12), 8) << "at the end of the frame the register is back to 8";
}

// The text of the report on a fixed machine: a golden file, so a change of the words is a visible diff
TEST_F(NextPixelMachine_Test, ReportGolden)
{
    ScreenNext* screen = NextScreen(_context);
    screen->SetLayerCapture(true);
    std::memset(Page(5), 0x00, 0x1800);
    Page(5)[0] = 0x80;
    std::memset(Page(5) + 0x1800, 0x38, 768);  // paper 7, ink 0
    Page(5)[0x1800] = 0x4A;
    Nr(0x15, 0x01);
    Out(0x303B, 0);
    for (uint8_t b : {uint8_t(32), uint8_t(32), uint8_t(0), uint8_t(0x80)})
        Out(0x57, b);
    Out(0x303B, 0);
    for (unsigned i = 0; i < 17; i++)
        Out(0x5B, i == 16 ? uint8_t(0x22) : uint8_t(0));  // pattern 0: row 1, column 0 = #22
    Frames(3);
    const StateNode p = Pixel(32, 33);
    ASSERT_EQ(Path(p, "ok"), "on") << DeviceState::ToText(p);
    GoldenText::Expect("next", "pixel", DeviceState::ToText(Pixel(32, 33)));
    GoldenText::Expect("next", "pixel-ula", DeviceState::ToText(Pixel(32, 32)));
}

// endregion
