#include "screenatm.h"

#include "atmfont.h"
#include "emulator/memory/memory.h"

/// region <Constructors / Destructors>

ScreenAtm::ScreenAtm(EmulatorContext* context, Memory* memory) : _context(context), _memory(memory)
{
}

/// endregion </Constructors / Destructors>

// =============================================================================
// ATM EXTENDED MODE RENDERING
// =============================================================================

/// ATM extended modes, ported from the reference renderer (other/unrealspeccy):
/// dxr_atm0.cpp (EGA), dxr_atm2.cpp (HW Multicolor), dxr_atm6.cpp (Text);
/// ZX-Evo Text Linear follows ZXMAK2 EvoTxtRenderer.cs. All modes share
/// ZX-compatible timing: 224 T-states/line, 312 lines/frame
/// (maxFrameTiming == config.frame == 69888). Video planes are LINEAR 8KB
/// with 40 bytes per line (8000 bytes per plane) - NOT the ZX 32-byte stride
/// - except TL, which reads a dedicated page of linear 64-byte text rows.
void ScreenAtm::Draw(uint32_t tstate, VideoModeEnum mode, const RasterDescriptor& rd, FramebufferDescriptor& framebuffer)
{
    constexpr uint32_t TSTATES_PER_LINE = 224;
    constexpr uint32_t VSYNC_VBLANK_LINES = 24;  // 16 vSync + 8 vBlank before the visible area
    constexpr uint32_t VISIBLE_LINES = 288;
    constexpr uint32_t SCREEN_LINES = 200;
    constexpr uint32_t BYTES_PER_LINE = 40;
    // Screen T-window inside a line (160 T). 320-px modes render 2 px/T
    // (cols 64..383 of the 448-wide frame); 640-px modes double the pixel
    // clock inside the same window (4 px/T, cols 32..671 of the 704-wide frame)
    constexpr uint32_t SCREEN_START_T = 32;
    constexpr uint32_t SCREEN_END_T = SCREEN_START_T + 160;

    if (framebuffer.memoryBuffer == nullptr)
        return;

    uint32_t line = tstate / TSTATES_PER_LINE;
    uint32_t tInLine = tstate % TSTATES_PER_LINE;

    // Outside the visible framebuffer area (vertical sync/blank)
    if (line < VSYNC_VBLANK_LINES || line >= VSYNC_VBLANK_LINES + VISIBLE_LINES)
        return;

    uint32_t* framebufferARGB = reinterpret_cast<uint32_t*>(framebuffer.memoryBuffer);
    const uint32_t fbRow = line - VSYNC_VBLANK_LINES;           // 0..287
    const uint32_t rowOffset = fbRow * rd.fullFrameWidth;

    EmulatorState& state = _context->emulatorState;

    // Border goes through the 16-cell #FF palette RAM like every ATM color:
    // the 4-bit index is the FE border color plus the FE A3 bright bit
    // (xpeccy vidDrawATM* -> vid_dot_full(vid, brdcol & 0x0f)). Cells default
    // to the standard ZX colors, so a machine whose software never touches
    // #FF shows stock colors (xpeccy vid_reset / zx_set_pal preset).
    const uint32_t borderColor =
        state.atmPalette[(state.border_attr & 0x07) | ((state.atmBorderBright & 1) << 3)];
    const bool inScreenRow = (fbRow >= rd.screenOffsetTop) &&
                             (fbRow < rd.screenOffsetTop + SCREEN_LINES);

    // Border rows: fill by beam scan so every framebuffer column of the row
    // is covered exactly once per line (2 px/T for 448-wide, ~3 px/T for 704)
    if (!inScreenRow)
    {
        const uint32_t x0 = tInLine * rd.fullFrameWidth / TSTATES_PER_LINE;
        const uint32_t x1 = (tInLine + 1) * rd.fullFrameWidth / TSTATES_PER_LINE;
        for (uint32_t x = x0; x < x1; ++x)
            framebufferARGB[rowOffset + x] = borderColor;
        return;
    }

    // Side borders on screen rows: pixel clock derived from this mode's own
    // raster geometry (screenOffsetLeft px painted over SCREEN_START_T
    // T-states), not a hard-coded 2 px/T. 320-wide modes (M_ATM16) are
    // 64px/32T = 2 px/T; 704-wide hires modes (M_ATMHR/TX/TL) are
    // 32px/32T = 1 px/T. Using 2 px/T unconditionally used to overreach into
    // the first screen columns for hires modes.
    const uint32_t pxPerT = rd.screenOffsetLeft / SCREEN_START_T;
    if (tInLine < SCREEN_START_T)
    {
        const uint32_t x = pxPerT * tInLine;
        for (uint32_t k = 0; k < pxPerT; ++k)
            if (x + k < rd.fullFrameWidth)
                framebufferARGB[rowOffset + x + k] = borderColor;
        return;
    }
    if (tInLine >= SCREEN_END_T)
    {
        const uint32_t x = rd.screenOffsetLeft + rd.screenWidth + pxPerT * (tInLine - SCREEN_END_T);
        for (uint32_t k = 0; k < pxPerT; ++k)
            if (x + k < rd.fullFrameWidth)
                framebufferARGB[rowOffset + x + k] = borderColor;
        return;
    }

    // --- Screen window [32, 192) ---
    const uint32_t t = tInLine - SCREEN_START_T;         // 0..159
    const uint32_t screenY = fbRow - rd.screenOffsetTop; // 0..199

    // Video pages: 7FFD bit 3 selects the video page (7/5); plane pairs live
    // in the page 4 below it (3/1) - the reference's -4*PAGE / +0 / +0x2000
    // plane offsets relative to the video page.
    uint8_t videoPage = (state.p7FFD & 0x08) ? 7 : 5;
    uint8_t altPage = videoPage - 4;
    uint8_t* vp = _memory->RAMPageAddress(videoPage);
    uint8_t* ap = _memory->RAMPageAddress(altPage);
    const uint32_t offset = screenY * BYTES_PER_LINE; // linear plane base for this line

    if (mode == M_ATM16)
    {
        // EGA 16-color 320x200. Each plane byte holds TWO adjacent pixels as
        // two 4-bit ZX-palette indices, bit-interleaved ZX-attribute-style
        // (reference draw.cpp p4bpp_tables + dxr_atm0.cpp line_atm0_16):
        //   first (left)  pixel color = rt = {b6, b2, b1, b0}
        //   second (right) pixel color = lf = {b7, b5, b4, b3}
        // Plane k serves the pixel pair (8j+2k, 8j+2k+1) of byte group j:
        // ega0 = ap+0, ega1 = vp+0, ega2 = ap+0x2000, ega3 = vp+0x2000.
        // Colors go through the 16-cell #FF palette RAM: the 4-bit index
        // carries the bright flag in bit 3 (xpeccy vidDrawATMega ->
        // vid_dot_full(pal[col])) - _rgbaColors would expect full ULA
        // attribute bytes (brightness = bit 6) and silently drop it.
        const uint32_t j = t / 4;  // byte group 0..39
        const uint32_t q = t % 4;  // plane 0..3
        uint8_t* plane = (q & 1) ? vp : ap;
        const uint8_t bt = plane[((q >> 1) << 13) + offset + j];
        const uint32_t col = rd.screenOffsetLeft + 8 * j + 2 * q;
        framebufferARGB[rowOffset + col] = state.atmPalette[(bt & 0x07) | (bt & 0x40 ? 0x08 : 0x00)];
        framebufferARGB[rowOffset + col + 1] = state.atmPalette[((bt >> 3) & 0x07) | (bt & 0x80 ? 0x08 : 0x00)];
        return;
    }

    if (mode == M_ATMHR)
    {
        // Hardware Multicolor 640x200: 1bpp pixel planes + ZX-attr planes.
        // Pixel bytes alternate planes every 8 px: even byte groups read
        // plane+0, odd groups plane+0x2000 (reference dxr_atm2.cpp
        // line_atm2_8/16: h0 byte j -> px 16j..16j+7, h1 byte j -> px
        // 16j+8..15; ZXMAK2 Atm640Renderer: +0x2000*((y*80+x)&1)). Attr
        // planes pair same-parity (h2 with h0, h3 with h1). Bits are
        // MSB-first: bit 7 is the leftmost pixel of the byte.
        const uint32_t n = t / 2;      // pixel byte group 0..79 (8 px each)
        const uint32_t half = t % 2;   // 0: bits 7..4, 1: bits 3..0
        const bool fromP0 = (n % 2 == 0);
        const uint8_t* pixPlane = fromP0 ? vp : vp + 0x2000;
        const uint8_t* attrPlane = fromP0 ? ap : ap + 0x2000;
        const uint8_t pix = pixPlane[offset + n / 2];
        const uint8_t attr = attrPlane[offset + n / 2];
        // ATM attribute decode (xpeccy vidATMDoubleDot, shared by HWM / TX /
        // TL): bit 6 = ink bright, bit 7 = PAPER bright - there is no flash.
        // _rgbaFlashColors would misread bit 7 as the flash/paper-swap flag.
        const uint32_t ink = state.atmPalette[(attr & 0x07) | ((attr & 0x40) >> 3)];
        const uint32_t paper = state.atmPalette[((attr & 0x38) >> 3) | ((attr & 0x80) >> 4)];
        const uint32_t col = rd.screenOffsetLeft + 8 * n + 4 * half;
        const uint32_t shift = 4 * half;
        for (uint32_t k = 0; k < 4; ++k)
            framebufferARGB[rowOffset + col + k] = ((pix >> (7 - shift - k)) & 1) ? ink : paper;
        return;
    }

    if (mode == M_ATMTL)
    {
        // ZX-Evo Text Linear (FF77 mode 7): 80x25 text, 640x200, read from a
        // single DEDICATED page - videoPage==5 -> RAM page 8, else page 10
        // (reference ZXMAK2 UlaAtm450.UpdateVideoPage; unlike the modes above
        // there are no vp/ap plane pairs, and videoPage comes from 7FFD bit 3
        // exactly as for them). Text rows are linear 64-byte blocks inside
        // that page (reference ZXMAK2 EvoTxtRenderer.OnParamsChanged):
        //   codes: even char column n at +0x01C0 + 64*r + (n>>1),
        //          odd char column n at +0x11C0 + 64*r + (n>>1)
        //   attrs: complement parity - even n at +0x31C0 + 64*r + ((n+1)>>1),
        //          odd n at +0x21C0 + 64*r + ((n+1)>>1)
        // Font and bit order are the same as TX: built-in SGEN table,
        // row-major [(scanline % 8)*256 + code], MSB-first (bit 7 = leftmost).
        const uint32_t n = t / 2;      // char column 0..79
        const uint32_t half = t % 2;   // 0: font bits 7..4, 1: bits 3..0
        uint8_t* page = _memory->RAMPageAddress(videoPage == 5 ? 8 : 10);
        const uint32_t rowBase = (screenY / 8) * 64;
        const bool evenCol = (n % 2 == 0);
        const uint32_t codeAddr = (evenCol ? 0x01C0u : 0x11C0u) + rowBase + (n >> 1);
        const uint32_t attrAddr = (evenCol ? 0x31C0u : 0x21C0u) + rowBase + ((n + 1) >> 1);
        const uint8_t code = page[codeAddr];
        const uint8_t attr = page[attrAddr];
        const uint8_t glyph = ATM_FONT[(screenY % 8) * 256 + code];
        // vidATMDoubleDot decode: bit 6 = ink bright, bit 7 = paper bright
        const uint32_t ink = state.atmPalette[(attr & 0x07) | ((attr & 0x40) >> 3)];
        const uint32_t paper = state.atmPalette[((attr & 0x38) >> 3) | ((attr & 0x80) >> 4)];
        const uint32_t col = rd.screenOffsetLeft + 8 * n + 4 * half;
        const uint32_t shift = 4 * half;
        for (uint32_t k = 0; k < 4; ++k)
            framebufferARGB[rowOffset + col + k] = ((glyph >> (7 - shift - k)) & 1) ? ink : paper;
        return;
    }

    // M_ATMTX: Text 80x25, 640x200 (4 px per T-state = half a char column).
    // Text rows live at plane byte 0x1C0 + 64*row (reference draw.cpp
    // PrepareFrameATM2: Offset = 64*(rayLine/8) with the screen starting at
    // ray line 56 -> row 0 at 0x1C0; dxr_atm6.cpp line_atm6_32 advances +2
    // per 4-char group within the row). All 8 scanlines of a text row read
    // the SAME 40 bytes per plane - the font row (screenY % 8) selects the
    // glyph line.
    // Char codes: p0 = vp+0, p1 = vp+0x2000; attrs: a1 = ap+0x2000 (pairs with
    // p0 chars) and a0 = ap+1 (pairs with p1 chars - the +1 offset is a
    // hardware quirk kept from the reference dxr_atm6.cpp). Font: built-in 2KB
    // table (atmfont.h), row-major [row * 256 + code], MSB-first bits (bit 7
    // = leftmost pixel - reference dxr_atm6_8/16 and ZXMAK2 AtmTxtRenderer;
    // the unrealspeccy 32bpp paths are LSB-first outliers).
    {
        const uint32_t n = t / 2;      // char column 0..79
        const uint32_t half = t % 2;   // 0: font bits 7..4, 1: bits 3..0
        const uint32_t byteIdx = 0x1C0 + 64 * (screenY / 8) + n / 2;
        const bool fromP0 = (n % 2 == 0);
        const uint8_t code = fromP0 ? vp[byteIdx] : vp[0x2000 + byteIdx];
        const uint8_t attr = fromP0 ? ap[0x2000 + byteIdx] : ap[1 + byteIdx];
        const uint8_t glyph = ATM_FONT[(screenY % 8) * 256 + code];
        // vidATMDoubleDot decode: bit 6 = ink bright, bit 7 = paper bright
        const uint32_t ink = state.atmPalette[(attr & 0x07) | ((attr & 0x40) >> 3)];
        const uint32_t paper = state.atmPalette[((attr & 0x38) >> 3) | ((attr & 0x80) >> 4)];
        const uint32_t col = rd.screenOffsetLeft + 8 * n + 4 * half;
        const uint32_t shift = 4 * half;
        for (uint32_t k = 0; k < 4; ++k)
            framebufferARGB[rowOffset + col + k] = ((glyph >> (7 - shift - k)) & 1) ? ink : paper;
    }
}
