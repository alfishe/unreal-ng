#include "screenalco.h"

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

ScreenAlco::ScreenAlco(EmulatorContext* context, Memory* memory) : _context(context), _memory(memory)
{
}

/// Ported from xpeccy vidDrawAlco and the BaseConf video_addrgen.v address map:
///   M_P16: four planes at the video page pair {vidPage ^ 1, vidPage} x {+0,
///     +0x2000}, ZX screen addressing; each byte holds two adjacent pixels as
///     4-bit palette indices (left = {b6,b2,b1,b0}, right = {b7,b5,b4,b3} - the
///     same packing as ATM EGA, but the pair pages are ^1, not ^4).
///   M_PMC: vidDrawHwmc - the bitmap byte AND the attribute byte are both
///     fetched from the PIXEL address of the video page. Attr decode: ink =
///     bits 0-2 + bit 6, paper = bits 3-6 (bit 6 brights both), bit 7 = flash -
///     inverts the bitmap on the flash phase.
/// Border and all colors go through the #FF 16-cell palette RAM, which defaults
/// to the standard ZX colors - Pentagon (no #FF port) sees stock colors.
void ScreenAlco::DrawRange(uint32_t from, uint32_t to, VideoModeEnum mode, const TstateCoordLUT* lut,
                           uint16_t fbWidth, FramebufferDescriptor& framebuffer, bool flash)
{
    if (framebuffer.memoryBuffer == nullptr)
        return;

    EmulatorState& state = _context->emulatorState;
    uint32_t* const fb = reinterpret_cast<uint32_t*>(framebuffer.memoryBuffer);

    // Video page from 7FFD bit 3; latches cannot change inside one catch-up range
    const uint8_t videoPage = (state.p7FFD & 0x08) ? 7 : 5;
    const uint8_t* const pageVideo = _memory->RAMPageAddress(videoPage);
    const uint8_t* const pagePair = _memory->RAMPageAddress(videoPage ^ 1);
    const uint32_t* const palette = state.atmPalette;
    const uint32_t borderColor = palette[(state.border_attr & 0x07) | ((state.atmBorderBright & 1) << 3)];
    const bool p16 = mode == M_P16;

    for (uint32_t t = from; t <= to; ++t)
    {
        const TstateCoordLUT& e = lut[t];
        if (e.renderType == RT_BLANK)
            continue;

        uint32_t* const out = fb + static_cast<size_t>(e.framebufferY) * fbWidth + e.framebufferX;
        if (e.renderType == RT_BORDER)
        {
            out[0] = borderColor;
            out[1] = borderColor;
            continue;
        }

        // One T renders one pixel pair (zxX, zxX + 1)
        if (p16)
        {
            // Byte group j = 8 pixels; plane q holds pixel pair q
            const uint32_t j = e.zxX >> 3;
            const uint32_t q = (e.zxX >> 1) & 3;
            const uint8_t* const plane = (q & 1) ? pageVideo : pagePair;
            const uint8_t bt = plane[((q >> 1) << 13) + e.screenOffset + j];
            out[0] = palette[(bt & 0x07) | ((bt & 0x40) >> 3)];
            out[1] = palette[((bt & 0x38) >> 3) | ((bt & 0x80) >> 4)];
            continue;
        }

        const uint8_t attr = pageVideo[e.screenOffset + e.symbolX];
        const uint8_t bitmap = ((attr & 0x80) && flash) ? static_cast<uint8_t>(attr ^ 0xFF) : attr;
        const uint32_t ink = palette[(attr & 0x07) | ((attr & 0x40) >> 3)];
        const uint32_t paper = palette[(attr & 0x78) >> 3];

        const uint32_t bit0 = (bitmap << e.pixelXBit) & 0x80;
        const uint32_t mask0 = static_cast<uint32_t>(-static_cast<int32_t>(bit0 >> 7));
        out[0] = (ink & mask0) | (paper & ~mask0);
        const uint32_t bit1 = (bitmap << (e.pixelXBit + 1)) & 0x80;
        const uint32_t mask1 = static_cast<uint32_t>(-static_cast<int32_t>(bit1 >> 7));
        out[1] = (ink & mask1) | (paper & ~mask1);
    }
}
