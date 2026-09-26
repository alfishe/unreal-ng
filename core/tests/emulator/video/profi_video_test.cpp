// ZX Profi video tests - standard 256x192 mode and the 512x240 hi-res renderer.
//
// Covers the design test plan section 10.2 (docs/inprogress/2026-09-21-profi/technical-design.md)
// and closes the reconciliation-report verification gap (2026-09-25-profi-reconciliation.md
// section 7): until now only the boot-splash mode assertion and the decoder-level
// PaletteWrite tests exercised this area - ScreenProfi::Draw itself had no unit test.
//
// Scope notes:
//  - Pixel-level expectations are asserted against memory encodings (page/offset/attribute
//    layout, palette entry packing) at computed beam positions, NOT against full-frame golden
//    images, so a future Q3 hi-res raster change (design section 12 Q3, 192 T/line option)
//    only touches the beam helpers below, not every assertion.
//  - The standard-mode flash PHASE is intentionally not asserted: the per-t-state ZX LUT path
//    has no flash-phase swap for any model on master (only M_PMC inverts on _vid.flash), so a
//    Profi-specific expectation would either lock in that project-wide trait or break when it
//    gets fixed. The hi-res "no flash" behaviour (attribute b7 = paper bright) IS asserted.
//  - Palette WRITE decoding (index from previous #FE, DS80 gating) is covered by
//    portdecoder_profi_test.cpp; here the palette entries are programmed through the same
//    ports and the RENDERER's 9-bit GGGRRRBBB -> ABGR packing is what gets verified.

#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/profifixture.h"
#include "emulator/video/screen.h"
#include "emulator/video/zx/screenzx.h"

#include <cstring>

/// region <Helpers>

// Hi-res fetch constants (design section 7.2, ScreenProfi::Draw)
constexpr uint16_t kHiresPixelPage = 4;      // 6 when 7FFD.3
constexpr uint16_t kHiresAttrPage = 0x38;    // 0x3A when 7FFD.3
constexpr uint32_t kHiresPageBytes = 0x4000;

/// Spec offset of a hi-res pixel/attribute byte (design section 7.2):
/// A[13:0] = { ~h3, v[7:6], v[2:0], v[5:3], h[8:4] } - the ZX line layout of
/// row v plus column cell, with the FIRST byte of each 16-px pair at +0x2000
/// and the second at +0x0000 (byteIndex 0..63 = two bytes per 16-px cell)
constexpr uint32_t HiresByteOffset(uint32_t v, uint32_t byteIndex)
{
    const uint32_t cell = byteIndex / 2;
    const uint32_t offset = ((v & 0x07) << 8) | ((v & 0x38) << 2) | ((v & 0xC0) << 5) | cell;
    return offset | ((byteIndex & 1) ? 0x0000 : 0x2000);
}

/// 9-bit GGGRRRBBB palette entry -> ABGR8888, packed the way ScreenProfi::Draw does
/// (3-bit channels scaled by 255/7, blue in bits 2:0 carrying the extra #FE.D7 LSB)
constexpr uint32_t PaletteABGR(uint16_t entry)
{
    const uint32_t g = ((entry >> 6) & 0x07) * 255 / 7;
    const uint32_t r = ((entry >> 3) & 0x07) * 255 / 7;
    const uint32_t b = (entry & 0x07) * 255 / 7;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

/// region </Helpers>

class ProfiVideo_Test : public ProfiMachineFixture
{
protected:
    ScreenZX* Screen()
    {
        return dynamic_cast<ScreenZX*>(_context->pScreen);
    }

    EmulatorState& State()
    {
        return _context->emulatorState;
    }

    /// Apply the detected mode. Fixture SetUp leaves the renderer at the ScreenZX
    /// constructor default (M_ZX48); InitRaster routes MM_PROFI through
    /// DetectModeProfi (DFFD.7 -> M_PROFIHR) and allocates the framebuffer.
    void ApplyMode()
    {
        Screen()->InitRaster();
    }

    /// DS80 on: Port_DFFD re-runs InitRaster on the bit change itself
    void EnterHiRes()
    {
        WritePort(0xDFFD, 0x80);
    }

    static constexpr uint32_t kTstatesPerLine = 224;

    /// Beam t-state of a framebuffer row / in-line T-state for a mode's raster
    /// (framebuffer row 0 sits vSyncLines + vBlankLines below the frame start)
    uint32_t BeamT(VideoModeEnum mode, uint32_t fbRow, uint32_t tInLine)
    {
        const RasterDescriptor& rd = Screen()->rasterDescriptors[mode];
        return (rd.vSyncLines + rd.vBlankLines + fbRow) * kTstatesPerLine + tInLine;
    }

    /// Hi-res paper window: 512 px at 4 px/T drawn inside the standard 128 T paper
    /// window, paper row v at framebuffer row screenOffsetTop + v (design section 7.4)
    uint32_t HiresPaperT(uint32_t v, uint32_t t)
    {
        const RasterDescriptor& rd = Screen()->rasterDescriptors[M_PROFIHR];
        return BeamT(M_PROFIHR, rd.screenOffsetTop + v, rd.screenOffsetLeft / 2 + t);
    }

    /// Hi-res attribute byte -> palette indices (design section 7.2):
    /// b7 paper bright, b6 ink bright, b5:3 paper GRB, b2:0 ink GRB - no flash
    static constexpr uint8_t HiresInkIndex(uint8_t attr)
    {
        return static_cast<uint8_t>((attr & 0x07) | ((attr & 0x40) >> 3));
    }

    static constexpr uint8_t HiresPaperIndex(uint8_t attr)
    {
        return static_cast<uint8_t>(((attr >> 3) & 0x07) | ((attr & 0x80) >> 4));
    }

    uint32_t PaletteColor(uint8_t index) const
    {
        return PaletteABGR(_context->emulatorState.profiPalette[index & 0x0F]);
    }

    uint8_t* HiresPixelPage()
    {
        return _memory->RAMPageAddress(kHiresPixelPage);
    }

    uint8_t* HiresAttrPage()
    {
        return _memory->RAMPageAddress(kHiresAttrPage);
    }

    /// Sweep one full beam line through the renderer (both hi-res and the ZX LUT
    /// path self-guard the blank regions past the visible window)
    void DrawLine(VideoModeEnum mode, uint32_t fbRow)
    {
        const uint32_t start = BeamT(mode, fbRow, 0);
        for (uint32_t t = start; t < start + kTstatesPerLine; ++t)
            Screen()->Draw(t);
    }

    /// ZX bitmap offset of row y, column cell (the classic thirds layout)
    static constexpr uint32_t ZxOffset(uint32_t y, uint32_t cell)
    {
        return ((y & 0x07) << 8) | ((y & 0x38) << 2) | ((y & 0xC0) << 5) | cell;
    }
};

/// region <Standard mode (M_PROFI, 256x192 through the ZX LUT path)>

/// @brief Pixel/attribute decode, side borders and the paper window position on the
///        Profi 312-line raster (design section 7.1: bit-identical to a stock Spectrum
///        screen, discrete-logic ULA with a 1 T border latch)
TEST_F(ProfiVideo_Test, Standard_PixelsAttrsBorderThroughZXPath)
{
    ApplyMode();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFI);

    auto& fb = Screen()->GetFramebufferDescriptor();
    ASSERT_EQ(fb.width, 352u);
    ASSERT_EQ(fb.height, 288u);
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    // zxY = 0, cell 0: 4 ink pixels then 4 paper; attr 0x47 = bright white on black.
    // The fixture's tag pattern would bleed through the rest of the row - zero it
    uint8_t* p5 = _memory->RAMPageAddress(5);
    std::memset(p5, 0, 0x4000);
    p5[ZxOffset(0, 0)] = 0xF0;
    p5[0x1800] = 0x47;

    WritePort(0x00FE, 0x02);  // red border

    const uint32_t row = 48;  // vSync+vBlank 24 + screenOffsetTop 48 -> zxY 0
    DrawLine(M_PROFI, row);

    const uint32_t ink = Screen()->TransformZXSpectrumColorsToRGBA(0x47, true);
    const uint32_t paper = Screen()->TransformZXSpectrumColorsToRGBA(0x47, false);
    const uint32_t border = Screen()->TransformZXSpectrumColorsToRGBA(0x02, true);

    EXPECT_EQ(At(row, 0), border) << "left border";
    EXPECT_EQ(At(row, 47), border);
    EXPECT_EQ(At(row, 48), ink) << "pixel bits 7..4";
    EXPECT_EQ(At(row, 51), ink);
    EXPECT_EQ(At(row, 52), paper) << "pixel bits 3..0";
    EXPECT_EQ(At(row, 55), paper);
    EXPECT_EQ(At(row, 303), paper) << "rest of the row stays paper";
    EXPECT_EQ(At(row, 304), border) << "right border";
    EXPECT_EQ(At(row, 351), border);
}

/// @brief 7FFD bit 3 switches the standard screen between RAM pages 5 and 7
///        (SetActiveScreen); the row drawn after the switch must come from page 7
TEST_F(ProfiVideo_Test, Standard_ShadowScreenAt7FFDBit3)
{
    ApplyMode();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFI);

    auto& fb = Screen()->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    // Different rows for the two screens: the ZX LUT path latches pixel+attr bytes
    // per cell, so redrawing the SAME cell would keep the previously latched page.
    // Attribute row 0 (0x1800) covers zxY 0..7 - both probed rows live there
    uint8_t* p5 = _memory->RAMPageAddress(5);
    uint8_t* p7 = _memory->RAMPageAddress(7);
    std::memset(p5, 0, 0x4000);
    std::memset(p7, 0, 0x4000);
    p5[ZxOffset(1, 0)] = 0x00;   // zxY 1: blank on the normal screen
    p5[ZxOffset(2, 0)] = 0x00;   // zxY 2: blank there too (discriminating case)
    p5[0x1800] = 0x47;
    p7[ZxOffset(2, 0)] = 0xFF;   // zxY 2: solid on the shadow screen
    p7[0x1800] = 0x47;

    const uint32_t ink = Screen()->TransformZXSpectrumColorsToRGBA(0x47, true);
    const uint32_t paper = Screen()->TransformZXSpectrumColorsToRGBA(0x47, false);

    DrawLine(M_PROFI, 49);  // zxY 1 through page 5
    EXPECT_EQ(At(49, 48), paper) << "normal screen renders page 5";

    WritePort(0x7FFD, 0x08);  // bit 3 -> shadow screen
    DrawLine(M_PROFI, 50);    // zxY 2 through page 7
    EXPECT_EQ(At(50, 48), ink) << "shadow screen renders page 7";
    EXPECT_EQ(At(50, 55), ink);
}

/// endregion </Standard mode>

/// region <Mode detect and geometry>

/// @brief DFFD.7 toggles M_PROFI <-> M_PROFIHR with the geometry switch, and both
///        modes keep the corpus-consensus 312-line x 224 T = 69888 T frame (design
///        section 7.4: no emulator changes the frame in hi-res - the Q3 decision)
TEST_F(ProfiVideo_Test, ModeDetect_DFFD7SwitchesModeGeometryAndFrameTiming)
{
    ApplyMode();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFI);
    EXPECT_EQ(Screen::GetVideoModeName(Screen()->GetVideoMode()), "PROFI");
    EXPECT_EQ(Screen()->GetFramebufferDescriptor().width, 352u);

    EnterHiRes();
    EXPECT_EQ(Screen()->GetVideoMode(), M_PROFIHR);
    EXPECT_EQ(Screen::GetVideoModeName(Screen()->GetVideoMode()), "PROFIHR");

    auto& fb = Screen()->GetFramebufferDescriptor();
    EXPECT_EQ(fb.width, 608u);
    EXPECT_EQ(fb.height, 288u);

    const RasterDescriptor& rd = Screen()->rasterDescriptors[M_PROFIHR];
    EXPECT_EQ(rd.screenWidth, 512u);
    EXPECT_EQ(rd.screenHeight, 240u);
    EXPECT_EQ(rd.screenOffsetLeft, 48u);
    EXPECT_EQ(rd.screenOffsetTop, 24u) << "240 lines centred: paper starts 24 lines above the ZX one";

    // Frame timing invariant both ways: hi-res keeps the standard beam and frame
    EXPECT_EQ(Screen()->GetTstatesPerLine(), 224u);
    EXPECT_EQ(Screen()->GetMaxFrameTiming(), 69888u);
    EXPECT_EQ((rd.vSyncLines + rd.vBlankLines + rd.fullFrameHeight) * kTstatesPerLine, 69888u);

    // Screen window fits into the 608x288 storage
    EXPECT_LE(uint32_t(rd.screenOffsetLeft) + rd.screenWidth, rd.fullFrameWidth);
    EXPECT_LE(uint32_t(rd.screenOffsetTop) + rd.screenHeight, rd.fullFrameHeight);

    WritePort(0xDFFD, 0x00);
    EXPECT_EQ(Screen()->GetVideoMode(), M_PROFI);
    EXPECT_EQ(Screen()->GetFramebufferDescriptor().width, 352u);
    EXPECT_EQ(Screen()->GetMaxFrameTiming(), 69888u);
}

/// endregion </Mode detect and geometry>

/// region <Hi-res fetch: byte pairs, line layout, extents>

/// @brief Within each 16-pixel cell the FIRST byte is fetched at +0x2000 and the
///        second at +0x0000 (design section 7.2, A13 = ~h3) - the swapped order
///        that makes a naive linear fill render half-width garbage
TEST_F(ProfiVideo_Test, Hires_BytePairOrdering_FirstByteOfPairAt0x2000)
{
    ApplyMode();
    EnterHiRes();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFIHR);

    std::memset(HiresPixelPage(), 0, kHiresPageBytes);
    std::memset(HiresAttrPage(), 0, kHiresPageBytes);
    HiresAttrPage()[HiresByteOffset(0, 0)] = 0x47;  // bright white ink on black
    HiresAttrPage()[HiresByteOffset(0, 1)] = 0x47;
    HiresPixelPage()[HiresByteOffset(0, 0)] = 0xF0;  // first byte: ink, ink, paper, paper
    HiresPixelPage()[HiresByteOffset(0, 1)] = 0x0F;  // second byte: paper, paper, ink, ink

    for (uint32_t t = 0; t < 16; ++t)
        Screen()->Draw(HiresPaperT(0, t));

    auto& fb = Screen()->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t col) -> uint32_t& { return px[24 * fb.width + col]; };  // paper row v=0

    const uint32_t ink = PaletteColor(0x0F);
    const uint32_t paper = PaletteColor(0x00);
    for (uint32_t k = 0; k < 4; ++k)
    {
        EXPECT_EQ(At(48 + k), ink) << "first byte high nibble, px " << k;
        EXPECT_EQ(At(52 + k), paper) << "first byte low nibble, px " << k;
        EXPECT_EQ(At(56 + k), paper) << "second byte high nibble, px " << k;
        EXPECT_EQ(At(60 + k), ink) << "second byte low nibble, px " << k;
    }
}

/// @brief The row offset is the ZX line layout extended to 240 lines: v[7:6] picks
///        the third (0x0000/0x0800/0x1000/0x1800), v[2:0] the +0x100 block and
///        v[5:3] the +0x20 step. The hi-res screen has 3 full thirds plus the
///        extra 48 rows in the fourth - rows 192..239 must address 0x1800-based
///        offsets, not wrap back into the first third
TEST_F(ProfiVideo_Test, Hires_ZXLineLayout_AllThirdsIncludingExtraRows)
{
    ApplyMode();
    EnterHiRes();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFIHR);

    std::memset(HiresPixelPage(), 0, kHiresPageBytes);
    std::memset(HiresAttrPage(), 0, kHiresPageBytes);

    // One 16-px cell lit per probed row: first byte solid ink, second blank - a
    // wrong offset formula reads the zeroed memory and renders all paper
    const uint32_t rows[] = {0, 1, 8, 9, 63, 64, 65, 127, 128, 129, 191, 192, 193, 238, 239};
    for (uint32_t v : rows)
    {
        HiresPixelPage()[HiresByteOffset(v, 0)] = 0xFF;
        HiresAttrPage()[HiresByteOffset(v, 0)] = 0x47;
        HiresAttrPage()[HiresByteOffset(v, 1)] = 0x47;
    }

    auto& fb = Screen()->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    const uint32_t ink = PaletteColor(0x0F);
    const uint32_t paper = PaletteColor(0x00);

    for (uint32_t v : rows)
    {
        SCOPED_TRACE(testing::Message() << "hi-res row " << v);
        for (uint32_t t = 0; t < 16; ++t)
            Screen()->Draw(HiresPaperT(v, t));

        const uint32_t row = 24 + v;  // screenOffsetTop + v
        for (uint32_t k = 0; k < 8; ++k)
        {
            EXPECT_EQ(px[row * fb.width + 48 + k], ink) << "first byte of the cell";
            EXPECT_EQ(px[row * fb.width + 56 + k], paper) << "second byte stays blank";
        }
    }
}

/// @brief Full-frame extents: 512 lit columns across all 240 paper rows inside a
///        608x288 framebuffer, 48-px side borders and 24-row top/bottom borders in
///        the inverted-index palette colour (design sections 7.2/7.5)
TEST_F(ProfiVideo_Test, Hires_FrameExtents_512Columns240Rows)
{
    ApplyMode();
    EnterHiRes();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFIHR);

    std::memset(HiresPixelPage(), 0xFF, kHiresPageBytes);  // every pixel ink
    std::memset(HiresAttrPage(), 0x47, kHiresPageBytes);   // bright white on black
    WritePort(0x00FE, 0x02);                               // border 2 -> inverted index 5

    Screen()->RenderFrameBatch();

    auto& fb = Screen()->GetFramebufferDescriptor();
    ASSERT_EQ(fb.width, 608u);
    ASSERT_EQ(fb.height, 288u);
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t { return px[row * fb.width + col]; };

    const uint32_t ink = PaletteColor(0x0F);
    const uint32_t border = PaletteColor(0x05);  // ~2 & 7

    // Every paper row: ink from col 48 through 559, border on both sides
    for (uint32_t row = 24; row < 24 + 240; ++row)
    {
        ASSERT_EQ(At(row, 47), border) << "left border, row " << row;
        ASSERT_EQ(At(row, 48), ink) << "paper starts at col 48, row " << row;
        ASSERT_EQ(At(row, 300), ink) << "paper middle, row " << row;
        ASSERT_EQ(At(row, 559), ink) << "paper ends at col 559, row " << row;
        ASSERT_EQ(At(row, 560), border) << "right border, row " << row;
        ASSERT_EQ(At(row, 607), border) << "row " << row;
    }

    // Full-width scans: top/bottom border rows and sample paper rows
    for (uint32_t row : {0u, 23u, 264u, 287u})
        for (uint32_t col = 0; col < fb.width; ++col)
            ASSERT_EQ(At(row, col), border) << "border row " << row << " col " << col;

    for (uint32_t row : {24u, 143u, 263u})
    {
        for (uint32_t col = 0; col < 48; ++col)
            ASSERT_EQ(At(row, col), border) << "left border scan, row " << row;
        for (uint32_t col = 48; col < 560; ++col)
            ASSERT_EQ(At(row, col), ink) << "paper scan, row " << row << " col " << col;
        for (uint32_t col = 560; col < fb.width; ++col)
            ASSERT_EQ(At(row, col), border) << "right border scan, row " << row;
    }
}

/// endregion </Hi-res fetch: byte pairs, line layout, extents>

/// region <Hi-res attribute encoding>

/// @brief Attribute bits: b2:0 ink GRB, b5:3 paper GRB, b6 INK bright, b7 PAPER
///        bright - b7 is NOT flash (design section 7.2). On the ZX screen b7 swaps
///        ink/paper on a 16-frame phase; here it must only add 8 to the paper index
TEST_F(ProfiVideo_Test, Hires_AttributeBrightBits_A7IsPaperBrightNotFlash)
{
    ApplyMode();
    EnterHiRes();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFIHR);

    std::memset(HiresPixelPage(), 0, kHiresPageBytes);
    std::memset(HiresAttrPage(), 0, kHiresPageBytes);

    // Five consecutive cells of paper row 0 (two byte offsets per cell)
    const uint8_t cellAttrs[] = {0x02, 0x42, 0x10, 0x90, 0x80};
    for (uint32_t cell = 0; cell < 5; ++cell)
    {
        const bool allInk = cell < 2;
        HiresPixelPage()[HiresByteOffset(0, 2 * cell)] = allInk ? 0xFF : 0x00;
        HiresPixelPage()[HiresByteOffset(0, 2 * cell + 1)] = allInk ? 0xFF : 0x00;
        HiresAttrPage()[HiresByteOffset(0, 2 * cell)] = cellAttrs[cell];
        HiresAttrPage()[HiresByteOffset(0, 2 * cell + 1)] = cellAttrs[cell];
    }

    auto& fb = Screen()->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);

    for (uint32_t t = 0; t < 80; ++t)
        Screen()->Draw(HiresPaperT(0, t));

    for (uint32_t cell = 0; cell < 5; ++cell)
    {
        SCOPED_TRACE(testing::Message() << "cell attr 0x" << std::hex << int(cellAttrs[cell]));
        // Cells 0..1 hold all-ink pixel bytes -> ink colour; cells 2..4 all-paper -> paper colour
        const uint8_t index = (cell < 2) ? HiresInkIndex(cellAttrs[cell]) : HiresPaperIndex(cellAttrs[cell]);
        for (uint32_t k = 0; k < 16; ++k)
            EXPECT_EQ(px[24 * fb.width + 48 + 16 * cell + k], PaletteColor(index)) << "px " << k;
    }

    // Even with the flash phase forced on, b7 keeps meaning paper bright: the
    // attr-0x80 cell (byteIndex 8..9 = t 16..19) stays bright-black paper
    // instead of swapping to ink
    Screen()->_vid.flash = 1;
    for (uint32_t t = 16; t < 20; ++t)
        Screen()->Draw(HiresPaperT(0, t));
    for (uint32_t k = 0; k < 16; ++k)
        EXPECT_EQ(px[24 * fb.width + 48 + 64 + k], PaletteColor(0x08)) << "no flash swap, px " << k;
    Screen()->_vid.flash = 0;
}

/// endregion </Hi-res attribute encoding>

/// region <Hi-res shadow screen and monochrome variant>

/// @brief 7FFD bit 3 moves the hi-res fetch to bitmap page 6 / attribute page 0x3A
///        (design section 7.2 table); ScreenProfi re-reads the latch on every call,
///        so the switch applies to the very next drawn cell
TEST_F(ProfiVideo_Test, Hires_ShadowScreen_7FFDBit3SelectsPages6And3A)
{
    ApplyMode();
    EnterHiRes();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFIHR);

    std::memset(HiresPixelPage(), 0, kHiresPageBytes);          // page 4: blank
    std::memset(HiresAttrPage(), 0, kHiresPageBytes);           // page 0x38: black attrs
    uint8_t* p6 = _memory->RAMPageAddress(6);
    uint8_t* p3A = _memory->RAMPageAddress(0x3A);
    std::memset(p6, 0xFF, kHiresPageBytes);                     // page 6: solid ink
    std::memset(p3A, 0x47, kHiresPageBytes);                    // page 0x3A: white-on-black

    auto& fb = Screen()->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);

    for (uint32_t t = 0; t < 8; ++t)
        Screen()->Draw(HiresPaperT(0, t));
    for (uint32_t k = 0; k < 8; ++k)
        EXPECT_EQ(px[24 * fb.width + 48 + k], PaletteColor(0x00)) << "normal screen, px " << k;

    WritePort(0x7FFD, 0x08);  // bit 3 -> pages 6 / 0x3A
    for (uint32_t t = 0; t < 8; ++t)
        Screen()->Draw(HiresPaperT(0, t));
    for (uint32_t k = 0; k < 8; ++k)
        EXPECT_EQ(px[24 * fb.width + 48 + k], PaletteColor(0x0F)) << "shadow screen, px " << k;

    WritePort(0x7FFD, 0x00);  // back to pages 4 / 0x38
    for (uint32_t t = 0; t < 8; ++t)
        Screen()->Draw(HiresPaperT(0, t));
    for (uint32_t k = 0; k < 8; ++k)
        EXPECT_EQ(px[24 * fb.width + 48 + k], PaletteColor(0x00)) << "normal screen again, px " << k;
}

/// @brief Profi 3.xx monochrome variant (config.profi_monochrome): the attribute
///        page is unused; ink = palette[pFE low 3 bits], paper = palette[inverse]
TEST_F(ProfiVideo_Test, Hires_MonochromeVariant_IgnoresAttributePage)
{
    ApplyMode();
    EnterHiRes();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFIHR);
    _context->config.profi_monochrome = 1;

    std::memset(HiresPixelPage(), 0, kHiresPageBytes);
    std::memset(HiresAttrPage(), 0, kHiresPageBytes);  // colour mode would render all-black
    HiresPixelPage()[HiresByteOffset(0, 0)] = 0xFF;    // cell 0 ink, cell 1 blank

    WritePort(0x00FE, 0x02);  // pFE = 0x02 -> synthesised attr 0x2A: ink 2, paper 5

    auto& fb = Screen()->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);

    for (uint32_t t = 0; t < 16; ++t)
        Screen()->Draw(HiresPaperT(0, t));

    for (uint32_t k = 0; k < 8; ++k)
    {
        EXPECT_EQ(px[24 * fb.width + 48 + k], PaletteColor(0x02)) << "monochrome ink, px " << k;
        EXPECT_EQ(px[24 * fb.width + 56 + k], PaletteColor(0x05)) << "monochrome paper, px " << k;
    }
}

/// endregion </Hi-res shadow screen and monochrome variant>

/// region <Hi-res border>

/// @brief The border renders through the 16-entry palette with the INVERTED colour
///        index (design section 7.5: palette[~border & 7], ZXMAK2/Xpeccy consensus),
///        on all four edges of the 608x288 storage
TEST_F(ProfiVideo_Test, Hires_BorderInvertedPaletteIndex_AllFourEdges)
{
    ApplyMode();
    EnterHiRes();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFIHR);

    std::memset(HiresPixelPage(), 0, kHiresPageBytes);
    std::memset(HiresAttrPage(), 0, kHiresPageBytes);  // paper renders black

    // FE value 0x0A: border latch = 2 (inverted index 5) AND pFE = 0x0A, so the
    // next palette write lands in entry 5 (index = pFE ^ 0x0F). The palette OUT
    // carries the colour in A15:A8 of an A0=0 address, so the #FE latch sees the
    // same cycle and re-latches the DATA byte - pass 0x0A to keep border 2
    WritePort(0x00FE, 0x0A);
    WritePort(0x1F7E, 0x0A);  // colour = ~0x1F = 0xE0 -> entry 0x1C0: pure green
    ASSERT_EQ(State().profiPalette[5], 0x1C0);
    const uint32_t green = PaletteABGR(0x1C0);  // 0xFF00FF00 (ABGR: G = 255)

    auto& fb = Screen()->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    auto At = [&](uint32_t row, uint32_t col) -> uint32_t& { return px[row * fb.width + col]; };

    // Top border row, left/right edges of a paper row, bottom border row
    DrawLine(M_PROFIHR, 0);
    DrawLine(M_PROFIHR, 24);   // paper row v = 0
    DrawLine(M_PROFIHR, 287);

    for (uint32_t col : {0u, 47u, 300u, 560u, 607u})
    {
        EXPECT_EQ(At(0, col), green) << "top border, col " << col;
        EXPECT_EQ(At(287, col), green) << "bottom border, col " << col;
    }
    for (uint32_t col : {0u, 47u, 560u, 607u})
        EXPECT_EQ(At(24, col), green) << "side border on a paper row, col " << col;
    EXPECT_EQ(At(24, 48), PaletteColor(0x00)) << "paper itself stays black";
    EXPECT_EQ(At(24, 559), PaletteColor(0x00));

    // A new FE colour moves the inverted index: 0 -> palette[7]
    WritePort(0x00FE, 0x00);
    DrawLine(M_PROFIHR, 286);
    for (uint32_t col : {0u, 300u, 607u})
        EXPECT_EQ(At(286, col), PaletteColor(0x07)) << "border follows the FE latch";
}

/// endregion </Hi-res border>

/// region <Hi-res palette encoding>

/// @brief Palette entries programmed through the real ports (OUT (xx7E) with the
///        index taken from the previous #FE value) render through the 9-bit
///        GGGRRRBBB packing: 3-bit channels scaled to 255/7, the extra blue LSB
///        latched from #FE bit 7 shifting blue by one step (design section 7.3)
TEST_F(ProfiVideo_Test, Hires_Palette9BitEncoding_ThroughRealPortWrites)
{
    ApplyMode();
    EnterHiRes();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFIHR);

    std::memset(HiresPixelPage(), 0, kHiresPageBytes);
    std::memset(HiresAttrPage(), 0, kHiresPageBytes);

    // Entry 0x0A = pFE 0x05 ^ 0x0F; ink index 0x0A needs attr ink 2 + b6 bright
    HiresPixelPage()[HiresByteOffset(0, 0)] = 0xFF;
    HiresAttrPage()[HiresByteOffset(0, 0)] = 0x42;
    // Default-palette contrast cell: attr 0x02 -> ink index 2 (Karabas reset level 4)
    HiresPixelPage()[HiresByteOffset(0, 2)] = 0xFF;
    HiresAttrPage()[HiresByteOffset(0, 2)] = 0x02;

    auto& fb = Screen()->GetFramebufferDescriptor();
    auto* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);

    // colour = ~0xE2 = 0x1D -> entry = 0x1D << 1 | 0 = 0x3A: G=0, R=7 (255), B=2 (72)
    WritePort(0x00FE, 0x05);
    WritePort(0xE27E, 0x00);
    ASSERT_EQ(State().profiPalette[0x0A], 0x3A);

    for (uint32_t t = 0; t < 8; ++t)
        Screen()->Draw(HiresPaperT(0, t));
    for (uint32_t t = 4; t < 8; ++t)
        Screen()->Draw(HiresPaperT(0, t));  // byteIndex 2..3: the default-palette cell at x 64..79

    EXPECT_EQ(px[24 * fb.width + 48], 0xFF4800FFu) << "R=255 full scale, B=2 -> 72 (0x48)";
    EXPECT_EQ(px[24 * fb.width + 64], 0xFF000091u) << "default entry 0x020: R=4 -> 145 (0x91)";

    // Same index rewritten with #FE bit 7 set: entry 0x3B adds the blue LSB -> B=3 (109)
    WritePort(0x00FE, 0x85);
    WritePort(0xE27E, 0x00);
    ASSERT_EQ(State().profiPalette[0x0A], 0x3B);

    for (uint32_t t = 0; t < 8; ++t)
        Screen()->Draw(HiresPaperT(0, t));
    EXPECT_EQ(px[24 * fb.width + 48], 0xFF6D00FFu) << "extra blue LSB shifts B 72 -> 109 (0x6D)";
}

/// endregion </Hi-res palette encoding>

/// region <Mode flip mid-frame>

/// @brief A DFFD.7 flip takes effect at the port write, not at the frame boundary:
///        rows drawn after the flip render through the NEW mode. The switch also
///        reallocates the framebuffer (608x288 <-> 352x288) and re-clears it, so
///        rows above the flip point show the clear colour - the same documented
///        behaviour FramebufferStaysValidAcrossHiResSwitches locks in for the GUI
///        consumers (design section 10.2 "mid-frame change")
TEST_F(ProfiVideo_Test, ModeFlip_MidFrameSwitchesRendererAtPortWrite)
{
    ApplyMode();
    EnterHiRes();
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFIHR);

    // Hi-res pattern: solid bright-white ink on every paper row
    std::memset(HiresPixelPage(), 0xFF, kHiresPageBytes);
    std::memset(HiresAttrPage(), 0x47, kHiresPageBytes);

    auto& fb = Screen()->GetFramebufferDescriptor();
    uint32_t* px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);

    // Beam lines 24..99 = hi-res framebuffer rows 0..75; paper rows 24..75 are
    // among them (paper starts at row 24)
    for (uint32_t line = 24; line < 100; ++line)
        DrawLine(M_PROFIHR, line);
    EXPECT_EQ(px[25 * fb.width + 100], PaletteColor(0x0F)) << "row above the flip renders hi-res ink";

    // Mid-frame flip to standard: the buffer is reallocated to 352x288 and
    // re-cleared, so the pixel pointer must be re-taken after every flip
    WritePort(0xDFFD, 0x00);
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFI);
    ASSERT_EQ(fb.width, 352u);
    ASSERT_EQ(fb.height, 288u);
    px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);
    EXPECT_EQ(px[0], 0xFF000000u) << "rows above the flip show the switch clear, not hi-res leftovers";

    // Standard pattern: zxY = 72 (framebuffer row 120, below the flip), 4 ink +
    // 4 paper pixels with attr 0x47 (attribute row 9), red border
    uint8_t* p5 = _memory->RAMPageAddress(5);
    p5[ZxOffset(72, 0)] = 0xF0;
    p5[0x1800 + 9 * 32] = 0x47;
    WritePort(0x00FE, 0x02);

    // The rest of the frame runs through the ZX LUT path
    for (uint32_t t = 100 * kTstatesPerLine; t < 69888; ++t)
        Screen()->Draw(t);

    const uint32_t ink = Screen()->TransformZXSpectrumColorsToRGBA(0x47, true);
    const uint32_t paper = Screen()->TransformZXSpectrumColorsToRGBA(0x47, false);
    const uint32_t border = Screen()->TransformZXSpectrumColorsToRGBA(0x02, true);
    EXPECT_EQ(px[120 * fb.width + 48], ink) << "standard ink below the flip";
    EXPECT_EQ(px[120 * fb.width + 51], ink);
    EXPECT_EQ(px[120 * fb.width + 52], paper) << "standard paper below the flip";
    EXPECT_EQ(px[120 * fb.width + 0], border) << "standard border below the flip";

    // Flip back mid-frame: 608x288 again, and the remaining hi-res paper rows
    // (up to row 263, v = 239 - including the rows past 192 that only hi-res has)
    // render through ScreenProfi again
    WritePort(0xDFFD, 0x80);
    ASSERT_EQ(Screen()->GetVideoMode(), M_PROFIHR);
    ASSERT_EQ(fb.width, 608u);
    px = reinterpret_cast<uint32_t*>(fb.memoryBuffer);

    for (uint32_t t = 200 * kTstatesPerLine; t < 69888; ++t)
        Screen()->Draw(t);

    EXPECT_EQ(px[250 * fb.width + 100], PaletteColor(0x0F)) << "hi-res row v=226 below the second flip";
    EXPECT_EQ(px[263 * fb.width + 559], PaletteColor(0x0F)) << "last hi-res paper row";
    EXPECT_EQ(px[264 * fb.width + 100], PaletteColor(0x05)) << "bottom border, inverted index of FE colour 2";
}

/// endregion </Mode flip mid-frame>
