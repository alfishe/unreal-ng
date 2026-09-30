// Video debug translation (PLAN #42 phase 1; video-debug-translation design
// §11): for every rendered mode, the mapper's description of the picture must
// agree with what the renderer draws.
//
//   1. Colour agreement - for sampled pixels over random video memory and
//      palettes, the colour the mapper derives from its sources equals the
//      framebuffer pixel.
//   2. Round trip - every memory source of a pixel lists that pixel in
//      PixelsFor.
//   3. Flip - flipping exactly the bits a source names changes that pixel as
//      the mapper predicts and leaves a pixel it does not feed alone.
//   4. Beam geometry - the corners of the layer window, +-1 T.
//
// One test per mode; each builds the machine, renders a few frames (~10-30 ms).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/video/map/videomapservice.h"
#include "emulator/video/videofamily.h"
#include "emulator/video/zx/screenzx.h"

using namespace videomap;

namespace
{
struct ModeCase
{
    const char* name;
    MEM_MODEL model;
    uint32_t ramKB;
    uint32_t frame;
    uint8_t eff7 = 0;
    int ff77 = -1;      // ATM FF77 mode, -1: untouched
    uint8_t dffd = 0;   // Profi hi-res
    VideoModeEnum expected = M_NUL;
};

std::ostream& operator<<(std::ostream& os, const ModeCase& c)
{
    return os << c.name;
}

/// Deterministic bytes (xorshift)
struct Rng
{
    uint32_t s = 0x12345678;
    uint32_t Next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

class VideoMapService_Test : public ::testing::TestWithParam<ModeCase>
{
protected:
    std::unique_ptr<EmulatorContext> _context;
    std::unique_ptr<Core> _core;
    ScreenZX* _screen = nullptr;

    void SetUp() override
    {
        const ModeCase& c = GetParam();
        _context = std::make_unique<EmulatorContext>(LoggerLevel::LogError);
        _context->config.mem_model = c.model;
        _context->config.ramsize = c.ramKB;
        _context->config.frame = c.frame;
        _context->config.t_line = 224;
        _core = std::make_unique<Core>(_context.get());
        ASSERT_TRUE(_core->Init());
        _screen = dynamic_cast<ScreenZX*>(_context->pScreen);
        ASSERT_NE(_screen, nullptr);

        EmulatorState& state = _context->emulatorState;
        state.pEFF7 = c.eff7;
        if (c.ff77 >= 0)
        {
            state.pFF77 = static_cast<uint8_t>((c.ff77 & 0x07) | 0x20);
            state.aFF77 = 0x0100;
        }
        state.pDFFD = c.dffd;
        _screen->InitRaster();
        ASSERT_EQ(_screen->GetVideoMode(), c.expected);

        // Random video memory and palettes, so colours differ from pixel to pixel
        Rng rng;
        Memory* memory = _context->pMemory;
        const uint32_t pages = std::min<uint32_t>(c.ramKB / 16, 64);
        for (uint32_t p = 0; p < pages; ++p)
        {
            uint8_t* page = memory->RAMPageAddress(static_cast<uint16_t>(p));
            for (uint32_t i = 0; page && i < 0x4000; ++i)
                page[i] = static_cast<uint8_t>(rng.Next());
        }
        for (uint32_t i = 0; i < 16; ++i)
        {
            state.atmPalette[i] = 0xFF000000u | (rng.Next() & 0x00FFFFFFu);
            state.profiPalette[i] = static_cast<uint16_t>(rng.Next() & 0x1FF);
        }
    }

    void RenderFrame()
    {
        const VideoLayout layout = VideoMapService(_context.get()).Layout();
        const uint32_t frame = static_cast<uint32_t>(layout.tstatesPerLine) * layout.lines;
        _screen->DrawPeriod(0, frame - 1);
    }

    uint32_t Rendered(const VideoLayout& layout, uint32_t x, uint32_t y)
    {
        FramebufferDescriptor& fb = _screen->GetFramebufferDescriptor();
        return reinterpret_cast<const uint32_t*>(fb.memoryBuffer)[(layout.fb.surfaceTop + y) * fb.width + layout.fb.surfaceLeft + x];
    }

    std::vector<std::pair<uint32_t, uint32_t>> Samples(const LayerDesc& layer, size_t count)
    {
        const uint32_t w = layer.surface.width, h = layer.surface.height;
        std::vector<std::pair<uint32_t, uint32_t>> points = {{0, 0}, {w - 1, 0}, {0, h - 1}, {w - 1, h - 1}};
        Rng rng;
        rng.s = 0xC0FFEE;
        while (points.size() < count)
            points.push_back({rng.Next() % w, rng.Next() % h});
        return points;
    }

    static bool Covers(const std::vector<SurfaceArea>& areas, uint32_t x, uint32_t y)
    {
        for (const SurfaceArea& a : areas)
            if (x >= a.x && x < static_cast<uint32_t>(a.x + a.width) && y >= a.y && y < static_cast<uint32_t>(a.y + a.height))
                return true;
        return false;
    }
};
} // namespace

TEST_P(VideoMapService_Test, MapperColourAgreesWithTheRenderer)
{
    RenderFrame();
    VideoMapService service(_context.get());
    const VideoLayout layout = service.Layout();
    ASSERT_TRUE(layout.mapped);
    ASSERT_EQ(layout.layers.size(), 1u);

    for (const auto& [x, y] : Samples(layout.layers[0], 160))
    {
        const PixelSources p = service.SourcesAt(0, x, y);
        ASSERT_TRUE(p.valid) << x << "," << y;
        ASSERT_TRUE(p.renderedKnown) << x << "," << y;
        EXPECT_EQ(p.finalRgb, p.renderedRgb) << layout.layers[0].id << " pixel " << x << "," << y;
    }
}

TEST_P(VideoMapService_Test, EveryMemorySourceListsItsPixel)
{
    VideoMapService service(_context.get());
    const VideoLayout layout = service.Layout();
    for (const auto& [x, y] : Samples(layout.layers[0], 160))
    {
        const PixelSources p = service.SourcesAt(0, x, y);
        ASSERT_TRUE(p.valid);
        for (const SourceRef& ref : p.contribution.sources)
        {
            if (ref.space != Space::Ram)
                continue;
            EXPECT_TRUE(Covers(service.PixelsFor(ref), x, y))
                << layout.layers[0].id << " pixel " << x << "," << y << " source page " << ref.page << " offset 0x"
                << std::hex << ref.offset;
        }
    }
}

TEST_P(VideoMapService_Test, FlippingASourceChangesItsPixelOnly)
{
    RenderFrame();
    VideoMapService service(_context.get());
    const VideoLayout layout = service.Layout();
    const LayerDesc& layer = layout.layers[0];
    Memory* memory = _context->pMemory;

    int checked = 0;
    for (const auto& [x, y] : Samples(layer, 24))
    {
        const PixelSources before = service.SourcesAt(0, x, y);
        const SourceRef& ref = before.contribution.sources.front();
        ASSERT_EQ(ref.space, Space::Ram);
        const std::vector<SurfaceArea> fed = service.PixelsFor(ref);

        // A pixel on the same line that this byte does not feed
        uint32_t qx = (x + layer.surface.width / 2) % layer.surface.width;
        while (Covers(fed, qx, y))
            qx = (qx + 8) % layer.surface.width;
        const uint32_t otherBefore = Rendered(layout, qx, y);

        uint8_t* byte = memory->RAMPageAddress(ref.page) + ref.offset;
        const uint8_t original = *byte;
        *byte = static_cast<uint8_t>(original ^ ref.bitMask);
        RenderFrame();

        const PixelSources after = service.SourcesAt(0, x, y);
        EXPECT_EQ(after.finalRgb, Rendered(layout, x, y)) << layer.id << " pixel " << x << "," << y << " after the flip";
        if (after.finalRgb != before.finalRgb)
        {
            EXPECT_NE(Rendered(layout, x, y), before.renderedRgb) << layer.id << " pixel " << x << "," << y;
            checked++;
        }
        EXPECT_EQ(Rendered(layout, qx, y), otherBefore) << layer.id << ": pixel " << qx << "," << y << " is not fed by it";

        *byte = original;
        RenderFrame();
        if (checked >= 4)
            break;
    }
    EXPECT_GT(checked, 0) << "no sampled flip changed a colour";
}

TEST_P(VideoMapService_Test, BeamWindowCorners)
{
    VideoMapService service(_context.get());
    const VideoLayout layout = service.Layout();
    const LayerWindow& w = layout.layers[0].window;
    const uint32_t tpl = layout.tstatesPerLine;

    const BeamInfo first = service.BeamAt(w.firstLine * tpl + w.firstT);
    ASSERT_TRUE(first.inLayer);
    EXPECT_EQ(first.layer, layout.layers[0].id);
    EXPECT_EQ(first.x, 0u);
    EXPECT_EQ(first.y, 0u);
    EXPECT_EQ(first.xEnd, static_cast<uint32_t>(w.dotsPerT - 1));
    EXPECT_FALSE(service.BeamAt(w.firstLine * tpl + w.firstT - 1).inLayer) << "one T left of the window";
    EXPECT_FALSE(service.BeamAt((w.firstLine - 1) * tpl + w.firstT).inLayer) << "one line above the window";

    const uint32_t lastLine = w.firstLine + w.lineCount - 1;
    const uint32_t lastT = w.firstT + w.tCount - 1;
    const BeamInfo last = service.BeamAt(lastLine * tpl + lastT);
    ASSERT_TRUE(last.inLayer);
    EXPECT_EQ(last.x, static_cast<uint32_t>((w.tCount - 1) * w.dotsPerT));
    EXPECT_EQ(last.xEnd + 1, static_cast<uint32_t>(layout.layers[0].surface.width));
    EXPECT_EQ(last.y + 1, static_cast<uint32_t>(layout.layers[0].surface.height));
    EXPECT_FALSE(service.BeamAt(lastLine * tpl + lastT + 1).inLayer) << "one T right of the window";

    // The beam pixel and the layer pixel describe the same point
    const PixelSources atBeam = service.SourcesAtBeam(lastLine * tpl + lastT);
    const PixelSources atPixel = service.SourcesAt(0, last.x, last.y);
    EXPECT_EQ(atBeam.finalRgb, atPixel.finalRgb);
}

INSTANTIATE_TEST_SUITE_P(Modes, VideoMapService_Test,
                         ::testing::Values(ModeCase{"zx48", MM_SPECTRUM48, 48, 69888, 0, -1, 0, M_ZX48},
                                           ModeCase{"pentagon", MM_PENTAGON, 128, 71680, 0, -1, 0, M_PENTAGON128K},
                                           ModeCase{"p384", MM_PENTAGON, 128, 71680, EFF7_384, -1, 0, M_P384},
                                           ModeCase{"p16", MM_PENTAGON, 128, 71680, EFF7_4BPP, -1, 0, M_P16},
                                           ModeCase{"pmc", MM_PENTAGON, 128, 71680, EFF7_HWMC, -1, 0, M_PMC},
                                           ModeCase{"atm16", MM_ATM710, 1024, 69888, 0, FF77_16, 0, M_ATM16},
                                           ModeCase{"atmhr", MM_ATM710, 1024, 69888, 0, FF77_MC, 0, M_ATMHR},
                                           ModeCase{"atmtx", MM_ATM710, 1024, 69888, 0, FF77_TX, 0, M_ATMTX},
                                           ModeCase{"atmtl", MM_ATM3, 1024, 69888, 0, FF77_TL, 0, M_ATMTL},
                                           ModeCase{"profihr", MM_PROFI, 1024, 69888, 0, -1, 0x80, M_PROFIHR}),
                         [](const ::testing::TestParamInfo<ModeCase>& info) { return std::string(info.param.name); });

/// Text layers give the grid exactly (design §6: OCR reads text modes, not the bitmap)
TEST(VideoMapServiceText_Test, AtmTextGridMatchesTheSources)
{
    EmulatorContext context(LoggerLevel::LogError);
    context.config.mem_model = MM_ATM710;
    context.config.ramsize = 1024;
    context.config.frame = 69888;
    context.config.t_line = 224;
    Core core(&context);
    ASSERT_TRUE(core.Init());
    context.emulatorState.pFF77 = FF77_TX | 0x20;
    context.emulatorState.aFF77 = 0x0100;
    context.pScreen->InitRaster();
    ASSERT_EQ(context.pScreen->GetVideoMode(), M_ATMTX);

    VideoMapService service(&context);
    const PixelSources p = service.SourcesAt(0, 3 * 8, 2 * 8);  // cell (3, 2)
    ASSERT_TRUE(p.valid);
    const SourceRef code = p.contribution.sources[0];
    ASSERT_EQ(code.role, SourceRole::CharCode);
    context.pMemory->RAMPageAddress(code.page)[code.offset] = 'Q';

    uint16_t columns = 0, rows = 0;
    std::vector<TextCell> cells;
    ASSERT_TRUE(service.Text(0, columns, rows, cells));
    EXPECT_EQ(columns, 80);
    EXPECT_EQ(rows, 25);
    EXPECT_EQ(cells[2 * 80 + 3].code, 'Q');
    EXPECT_TRUE(cells[2 * 80 + 3].codeSource == code);

    EXPECT_FALSE(VideoMapService(&context).Text(1, columns, rows, cells)) << "no second layer";
}

/// Z80 addresses resolve through the current paging; ROM feeds nothing
TEST(VideoMapServiceZ80_Test, Spectrum48ScreenAddresses)
{
    EmulatorContext context(LoggerLevel::LogError);
    context.config.mem_model = MM_SPECTRUM48;
    context.config.ramsize = 48;
    context.config.frame = 69888;
    context.config.t_line = 224;
    Core core(&context);
    ASSERT_TRUE(core.Init());
    context.pScreen->InitRaster();

    VideoMapService service(&context);
    const std::vector<SurfaceArea> pixel = service.PixelsForZ80(0x4000);
    ASSERT_EQ(pixel.size(), 1u);
    EXPECT_EQ(pixel[0].layer, "zx");
    EXPECT_EQ(pixel[0].width, 8);
    EXPECT_EQ(pixel[0].height, 1);

    const std::vector<SurfaceArea> attr = service.PixelsForZ80(0x5800 + 33);  // row 1, column 1
    ASSERT_EQ(attr.size(), 1u);
    EXPECT_EQ(attr[0].x, 8);
    EXPECT_EQ(attr[0].y, 8);
    EXPECT_EQ(attr[0].height, 8);

    EXPECT_TRUE(service.PixelsForZ80(0x0000).empty()) << "ROM";
    EXPECT_TRUE(service.PixelsForZ80(0x8000).empty()) << "not screen memory";

    const PixelSources p = service.SourcesAt(0, 0, 0);
    ASSERT_EQ(p.z80.size(), 1u);
    EXPECT_EQ(p.z80[0], 0x4000);
}

/// One family switch for the renderer and the mappers
TEST(VideoFamily_Test, NullModeHasNoMapper)
{
    EXPECT_EQ(FamilyOf(M_NUL), VideoFamily::None);
    EXPECT_EQ(FamilyOf(M_ATMTL), VideoFamily::Atm);
    EXPECT_EQ(FamilyOf(M_PROFIHR), VideoFamily::Profi);
    EXPECT_EQ(FamilyOf(M_PMC), VideoFamily::Alco);
    EXPECT_EQ(FamilyOf(M_TS16), VideoFamily::Zx) << "drawn as ZX until TSConf has its renderer, so described as ZX";
    VideoState s;
    s.mode = M_NUL;
    EXPECT_FALSE(VideoMapService::MapperFor(VideoFamily::None).Layout(s).mapped);
}
