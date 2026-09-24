#include "screenprofi.h"

#include "emulator/memory/memory.h"

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
    constexpr uint32_t TSTATES_PER_LINE = 224;
    constexpr uint32_t VSYNC_VBLANK_LINES = 24;
    constexpr uint32_t VISIBLE_LINES = 288;
    constexpr uint32_t PAPER_START_T = 24;    // 48 px left border at 2 px/T
    constexpr uint32_t PAPER_TSTATES = 128;   // 512 px at 4 px/T
    constexpr uint32_t PAPER_END_T = PAPER_START_T + PAPER_TSTATES;
    constexpr uint32_t VISIBLE_END_T = PAPER_END_T + 24;  // right border, then blanking
    constexpr uint32_t SCREEN_LINES = 240;

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

    // Palette entry -> ABGR (raw GGGRRRBB: 3-bit G, 3-bit R, 2-bit B)
    auto paletteColor = [&state](uint8_t index) -> uint32_t
    {
        const uint8_t raw = state.profiPalette[index & 0x0F];
        const uint32_t g = ((raw >> 5) & 0x07) * 255 / 7;
        const uint32_t r = ((raw >> 2) & 0x07) * 255 / 7;
        const uint32_t b = (raw & 0x03) * 255 / 3;
        return 0xFF000000u | (b << 16) | (g << 8) | r;
    };

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
    const uint32_t cell = byteIndex / 2;                     // column 0..31
    const uint32_t half = t % 2;                             // which nibble of the byte
    const uint32_t v = fbRow - rd.screenOffsetTop;           // 0..239

    const bool shadow = (state.p7FFD & 0x08) != 0;
    const uint16_t pixelPage = shadow ? 6 : 4;
    const uint16_t attrPage = static_cast<uint16_t>((shadow ? 0x3A : 0x38) & _memory->GetRamMask());

    const uint32_t offset = ((v & 0x07) << 8) | ((v & 0x38) << 2) | ((v & 0xC0) << 5) | cell;
    const uint32_t byteOffset = offset | ((byteIndex & 1) ? 0x0000 : 0x2000);

    const uint8_t pixels = _memory->RAMPageAddress(pixelPage)[byteOffset];
    uint8_t attr = _memory->RAMPageAddress(attrPage)[byteOffset];

    if (_context->config.profi_monochrome)
    {
        // Profi 3.xx / ProfiMonochrome: attribute page unused, ink = border colour, paper = its inverse
        attr = static_cast<uint8_t>((state.pFE & 0x07) | (((state.pFE & 0x07) ^ 0x07) << 3));
    }

    const uint32_t ink = paletteColor(static_cast<uint8_t>((attr & 0x07) | ((attr & 0x40) >> 3)));
    const uint32_t paper = paletteColor(static_cast<uint8_t>(((attr >> 3) & 0x07) | ((attr & 0x80) >> 4)));

    const uint32_t x = rd.screenOffsetLeft + 8 * byteIndex + 4 * half;
    const uint32_t shift = 4 * half;
    for (uint32_t k = 0; k < 4; ++k)
        framebufferARGB[rowOffset + x + k] = ((pixels >> (7 - shift - k)) & 1) ? ink : paper;
}
