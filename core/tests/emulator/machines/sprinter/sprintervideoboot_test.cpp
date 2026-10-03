// Sprinter Sp2000 BIOS 3.04 screens through ScreenSprinter against the MAME
// references (Sprinter roadmap S2 acceptance ACC-1; test-plan R-2).
//
// testdata/machines/sprinter/reference/logo.png is MAME's 736x288 native screen
// at the end of frame 60 (the logo at full brightness, palette sum 302 548):
// the same window, the same frame origin and the same palette rule (R, G, B in
// video RAM) as ScreenSprinter. The test renders that frame without the turbo
// mode (pixels are asserted) and compares every pixel.
//
// One difference is MAME's, not the machine's: MAME draws pen numbers into its
// bitmap and turns them into colours when the frame is shown, with the palette
// of that moment. The BIOS fades the logo palette one step in every frame's
// interrupt (line 271, after the logo), so MAME's frame 60 shows the logo with
// the palette after frame 60's fade, while the beam drew it - and ScreenSprinter
// draws it - with the palette before it: those pixels differ by one fade step
// (two levels per channel). Drawn again with the state at the frame end (what
// MAME's bitmap means), the frame equals logo.png exactly.
//
// Boot-bound: 60 frames of BIOS POST and logo (the first 57 in the turbo mode,
// nothing is looked at there; frames 58-60 rendered at full fidelity).

#include <emulator/cpu/core.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/ports/models/portdecoder_sprinter.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "3rdparty/lodepng/lodepng.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/stringhelper.h"
#include "emulator/video/sprinter/screensprinter.h"
#include "emulator/video/sprinter/sprintervideoram.h"
#include "pch.h"
#include "sprinterfixture.h"
#include "stdafx.h"

namespace
{
std::string ReferencePng(const char* name)
{
    return (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "sprinter" / "reference" / name).string();
}

/// The framebuffer (RGBA 0xAABBGGRR) against an RGBA PNG of the same size
struct ImageDiff
{
    size_t pixels = 0;
    size_t differing = 0;
    uint32_t maxChannelDelta = 0;
    uint32_t firstX = 0, firstY = 0;
};

ImageDiff Compare(const FramebufferDescriptor& fb, const std::vector<unsigned char>& png, unsigned width, unsigned height)
{
    ImageDiff d;
    if (fb.width != width || fb.height != height)
        return d;
    const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
    for (unsigned y = 0; y < height; y++)
    {
        for (unsigned x = 0; x < width; x++)
        {
            const uint32_t ours = pixels[y * width + x];
            const unsigned char* ref = &png[(static_cast<size_t>(y) * width + x) * 4];
            uint32_t delta = 0;
            for (unsigned c = 0; c < 3; c++)
            {
                const int a = static_cast<int>((ours >> (8 * c)) & 0xFF);
                const int b = ref[c];
                delta = std::max(delta, static_cast<uint32_t>(a > b ? a - b : b - a));
            }
            d.pixels++;
            if (delta != 0)
            {
                if (d.differing++ == 0)
                {
                    d.firstX = x;
                    d.firstY = y;
                }
                d.maxChannelDelta = std::max(d.maxChannelDelta, delta);
            }
        }
    }
    return d;
}
}  // namespace

class SprinterVideoBoot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void SetUp() override
    {
        if (!SprinterFixture::Rom304Available())
            GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";

        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);

        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-video", "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC
        ASSERT_TRUE(SprinterFixture::SelectBios(_context, "sp2k-3.04.rom"));  // pinned to 3.04 (shipped default: 3.07 BETA 1)
        _context->config.sprinter.fast_start = 1;
        _emulator->Reset();
    }

    void TearDown() override
    {
        _emulator.reset();
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
    }

    /// Whole frames to the end of frame `frame` (1-based, MAME's numbering)
    void RunToEndOfFrame(uint64_t frame)
    {
        while (_context->emulatorState.frame_counter < frame)
            EmulatorTestHelper::RunFramesFast(_emulator.get(), 1);
    }

    uint32_t PaletteSum() const
    {
        const uint8_t* vram = _decoder->GetVideoRam().Data();
        uint32_t sum = 0;
        for (uint32_t line = 0; line < 256; line++)
            for (uint32_t offset = 0x3E0; offset < 0x400; offset++)
                sum += vram[line * 1024 + offset];
        return sum;
    }

    /// The rendered frame as a PNG in scratch (for a failure report)
    std::string SaveFrame(const char* name)
    {
        const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
        std::vector<unsigned char> rgba(static_cast<size_t>(fb.width) * fb.height * 4);
        const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
        for (size_t i = 0; i < static_cast<size_t>(fb.width) * fb.height; i++)
        {
            rgba[i * 4 + 0] = static_cast<unsigned char>(pixels[i]);
            rgba[i * 4 + 1] = static_cast<unsigned char>(pixels[i] >> 8);
            rgba[i * 4 + 2] = static_cast<unsigned char>(pixels[i] >> 16);
            rgba[i * 4 + 3] = 0xFF;
        }
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
        lodepng::encode(path, rgba, fb.width, fb.height);
        return path;
    }
};

// ACC-1: the BIOS 3.04 logo frame (frame 60) equals MAME's logo.png pixel for pixel.
// Boot-bound (60 frames of real ROM, the last three rendered without the turbo mode)
TEST_F(SprinterVideoBoot_Test, Bios304_LogoFrameMatchesMame)
{
    std::vector<unsigned char> png;
    unsigned width = 0, height = 0;
    ASSERT_EQ(lodepng::decode(png, width, height, ReferencePng("logo.png")), 0u) << ReferencePng("logo.png");
    ASSERT_EQ(width, 736u);
    ASSERT_EQ(height, 288u);

    _emulator->EnableTurboMode();
    RunToEndOfFrame(57);
    _emulator->DisableTurboMode();  // frames 58-60 are rendered in full
    RunToEndOfFrame(60);
    ASSERT_EQ(PaletteSum(), 302548u) << "not the frame of logo.png (palette.csv)";

    const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
    ASSERT_EQ(fb.videoMode, M_SPRINTER);

    // The beam's frame: the logo one fade step brighter than MAME shows it (see the top)
    const ImageDiff beam = Compare(fb, png, width, height);
    EXPECT_EQ(beam.pixels, static_cast<size_t>(736 * 288));
    EXPECT_LE(beam.maxChannelDelta, 2u) << "more than the one fade step; our frame: " << SaveFrame("sprinter-logo.png");
    EXPECT_LT(beam.differing, 736u * 72u) << "only the logo's 128x72 x 2 pixels fade";

    // MAME's meaning of a frame: the same pens with the palette at the frame end
    _context->pScreen->RenderFrameBatch();
    const ImageDiff end = Compare(fb, png, width, height);
    EXPECT_EQ(end.differing, 0u) << "max channel delta " << end.maxChannelDelta << ", first at (" << end.firstX << ", "
                                 << end.firstY << "); our frame: " << SaveFrame("sprinter-logo-end.png");
}

// The palette byte order on the real BIOS (hardware-reference §4.5): the logo's ellipse
// is blue. BIOS 3.04 keeps the logo palette as B, G, R (LOGPAL, SETUP #8C00) and its
// function #A4 writes it to video RAM reversed, so video RAM holds R, G, B
TEST_F(SprinterVideoBoot_Test, Bios304_LogoIsBlue)
{
    _emulator->EnableTurboMode();
    RunToEndOfFrame(57);
    _emulator->DisableTurboMode();
    RunToEndOfFrame(60);

    // LOGPAL entry 11 = #AD,#08,#08 (B, G, R): video RAM row 11, column #3E0 holds it
    // reversed, #08,#08,#AD less the fade steps so far (two levels a frame from frame 59)
    const SprinterVideoRam& vram = _decoder->GetVideoRam();
    EXPECT_LE(vram.Read(11 * 1024 + 0x3E0), 0x08) << "red";
    EXPECT_GE(vram.Read(11 * 1024 + 0x3E2), 0xA0) << "blue";

    // A pixel inside the ellipse, left of the letters (logo.png): blue dominates
    const FramebufferDescriptor& fb = _context->pScreen->GetFramebufferDescriptor();
    const uint32_t pixel = reinterpret_cast<const uint32_t*>(fb.memoryBuffer)[40 * fb.width + 60];
    const uint32_t r = pixel & 0xFF, b = (pixel >> 16) & 0xFF;
    EXPECT_GT(b, r + 64) << StringHelper::Format("pixel %08X", pixel);
}
