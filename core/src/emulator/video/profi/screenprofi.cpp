#include "screenprofi.h"

#include "emulator/memory/memory.h"
#include "emulator/ports/models/profiboard.h"
#include "emulator/video/profi/profigeometry.h"

/// region <Constructors / Destructors>

ScreenProfi::ScreenProfi(EmulatorContext* context, Memory* memory) : _context(context), _memory(memory)
{
}

/// endregion </Constructors / Destructors>

// =============================================================================
// PROFI HI-RES MODE RENDERING
// =============================================================================

/// @brief Profi 512x240 hi-res renderer (DFFD.7), one call per T-state.
///
/// Fetch (UnrealSpeccy drawers.cpp draw_profi, ZXMAK2 ProfiRenderer, Xpeccy video.c - all agree):
///   bitmap page 4 (6 when 7FFD.3), attribute page 0x38 (0x3A); the attribute byte sits at the
///   same offset as its pixel byte (one attribute per 8x1 pixels, "hi-colour");
///   offset = ZX line layout of the line v (0..239) + column c (0..31), and within each 16-pixel
///   cell the FIRST byte is read at +0x2000, the second at +0x0000.
/// Attribute: b7 paper bright, b6 ink bright, b5:3 paper GRB, b2:0 ink GRB; no flash. Colours go
///   through the 16-entry palette (profiPalette, index {bright,G,R,B}).
/// Timing: same 312 x 224T beam as the standard mode. The 512 px paper is drawn at 4 px/T inside the
///   128 T paper window (T 24..151 of the line); the 240 lines start 24 lines above the standard paper.
/// Border: shown through the palette with the INVERTED index (ZXMAK2 ProfiRenderer, Xpeccy nextbrd ^= 7).
void ScreenProfi::Draw(uint32_t tstate, const RasterDescriptor& rd, FramebufferDescriptor& framebuffer,
                        uint8_t borderColor)
{
    using namespace ProfiGeometry;
    constexpr uint32_t TSTATES_PER_LINE = kTStatesPerLine;
    constexpr uint32_t VSYNC_VBLANK_LINES = kVSyncVBlankLines;
    constexpr uint32_t VISIBLE_LINES = kVisibleLines;
    constexpr uint32_t PAPER_START_T = kPaperStartT;    // 48 px left border at 2 px/T
    constexpr uint32_t PAPER_END_T = kPaperEndT;        // 512 px at 4 px/T
    constexpr uint32_t VISIBLE_END_T = kVisibleEndT;    // right border, then blanking
    constexpr uint32_t SCREEN_LINES = kScreenLines;

    if (framebuffer.memoryBuffer == nullptr)
        return;

    const uint32_t line = tstate / TSTATES_PER_LINE;
    const uint32_t tInLine = tstate % TSTATES_PER_LINE;

    if (line < VSYNC_VBLANK_LINES || line >= VSYNC_VBLANK_LINES + VISIBLE_LINES)
        return;
    if (tInLine >= VISIBLE_END_T)
        return;

    uint32_t* framebufferARGB = reinterpret_cast<uint32_t*>(framebuffer.memoryBuffer);
    const uint32_t fbRow = line - VSYNC_VBLANK_LINES;
    const uint32_t rowOffset = fbRow * rd.fullFrameWidth;

    EmulatorState& state = _context->emulatorState;

    // Palette entry -> ABGR (9-bit GGGRRRBBB: 3-bit G, 3-bit R, 3-bit B - the extra blue LSB
    // comes from #FE.D7 latched at the time of the palette write, see Port_Palette_Out)
    auto paletteColor = [&state](uint8_t index) -> uint32_t { return PaletteColour(state.profi.palette[index & 0x0F]); };

    const uint32_t border = paletteColor(static_cast<uint8_t>(~borderColor) & 0x07);
    const bool inScreenRow = (fbRow >= rd.screenOffsetTop) && (fbRow < rd.screenOffsetTop + SCREEN_LINES);

    if (!inScreenRow || tInLine < PAPER_START_T || tInLine >= PAPER_END_T)
    {
        // Border: 2 px/T on the sides, 4 px/T across the paper window on border rows
        uint32_t x;
        uint32_t count;
        if (tInLine < PAPER_START_T)
        {
            x = 2 * tInLine;
            count = 2;
        }
        else if (tInLine >= PAPER_END_T)
        {
            x = rd.screenOffsetLeft + rd.screenWidth + 2 * (tInLine - PAPER_END_T);
            count = 2;
        }
        else
        {
            x = rd.screenOffsetLeft + 4 * (tInLine - PAPER_START_T);
            count = 4;
        }

        for (uint32_t k = 0; k < count && x + k < rd.fullFrameWidth; ++k)
            framebufferARGB[rowOffset + x + k] = border;
        return;
    }

    // Paper: two T-states per pixel byte, four pixels per T-state
    const uint32_t t = tInLine - PAPER_START_T;              // 0..127
    const uint32_t byteIndex = t / 2;                        // 0..63, two bytes per 16-px cell
    const uint32_t half = t % 2;                             // which nibble of the byte
    const uint32_t v = fbRow - rd.screenOffsetTop;           // 0..239

    const uint16_t pixelPage = PixelPage(state.p7FFD);
    const uint16_t attrPage = AttrPage(state.p7FFD, static_cast<uint16_t>(_memory->GetRamMask()));
    const uint32_t byteOffset = ByteOffset(v, byteIndex);

    const uint8_t pixels = _memory->RAMPageAddress(pixelPage)[byteOffset];
    uint8_t attr = _memory->RAMPageAddress(attrPage)[byteOffset];

    if (ProfiMonochromeHires(_context->config))
    {
        // Profi v3, or a v5 without palette chips: attribute page unused, ink = border colour, paper = its inverse
        attr = MonochromeAttr(state.pFE);
    }

    const uint32_t ink = paletteColor(AttrColourIndex(attr, true));
    const uint32_t paper = paletteColor(AttrColourIndex(attr, false));

    const uint32_t x = rd.screenOffsetLeft + 8 * byteIndex + 4 * half;
    const uint32_t shift = 4 * half;
    for (uint32_t k = 0; k < 4; ++k)
        framebufferARGB[rowOffset + x + k] = ((pixels >> (7 - shift - k)) & 1) ? ink : paper;
}

void ScreenProfi::DrawRange(uint32_t from, uint32_t to, const RasterDescriptor& rd, FramebufferDescriptor& framebuffer,
                            uint8_t borderColor)
{
    for (uint32_t t = from; t <= to; ++t)
        Draw(t, rd, framebuffer, borderColor);
}
