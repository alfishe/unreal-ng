//
// screen_renderer.cpp — ZX Spectrum screen rendering (C++ port of Python).
//
// Direct port of tools/verification/ttd-analyzer/src/framebuffer_renderer.py
// ::decode_screen_rgb. Output is a QImage in Format_RGB888.
//

#include "screen_renderer.h"
#include "ttd_format.h"

#include <stdexcept>

namespace ttd {

// ---------------------------------------------------------------------------
// Palettes — canonical "Spectrum RGB" (matches Python constants)
// ---------------------------------------------------------------------------
struct RGB { uint8_t r, g, b; };

static const RGB kPaletteNormal[8] = {
    {0x00, 0x00, 0x00},  // black
    {0x00, 0x00, 0xD7},  // blue
    {0xD7, 0x00, 0x00},  // red
    {0xD7, 0x00, 0xD7},  // magenta
    {0x00, 0xD7, 0x00},  // green
    {0x00, 0xD7, 0xD7},  // cyan
    {0xD7, 0xD7, 0x00},  // yellow
    {0xD7, 0xD7, 0xD7},  // white
};

static const RGB kPaletteBright[8] = {
    {0x00, 0x00, 0x00},  // black (bright has no effect)
    {0x00, 0x00, 0xFF},  // blue
    {0xFF, 0x00, 0x00},  // red
    {0xFF, 0x00, 0xFF},  // magenta
    {0x00, 0xFF, 0x00},  // green
    {0x00, 0xFF, 0xFF},  // cyan
    {0xFF, 0xFF, 0x00},  // yellow
    {0xFF, 0xFF, 0xFF},  // white
};

// ---------------------------------------------------------------------------
// RenderScreen — port of decode_screen_rgb()
// ---------------------------------------------------------------------------
QImage RenderScreen(const Checkpoint& cp, const std::vector<uint8_t>& ram,
                    int borderPx) {
    const int bank = SelectedScreenBank(cp.chipset.p7ffd);
    const size_t bankOffset = static_cast<size_t>(bank) * kEmuPageSize;

    if (bankOffset + kScreenBytes > ram.size())
        throw std::runtime_error("RAM too small for bank " + std::to_string(bank));

    const uint8_t* pixelData = ram.data() + bankOffset;
    const uint8_t* attrData  = pixelData + kPixelBytes;

    const int outW = kScreenWidth + 2 * borderPx;
    const int outH = kScreenHeight + 2 * borderPx;

    QImage img(outW, outH, QImage::Format_RGB888);
    if (img.isNull())
        throw std::runtime_error("cannot allocate QImage");

    // Border fill
    const int borderIdx = cp.chipset.border_attr & 0x07;
    const RGB borderRGB = kPaletteNormal[borderIdx];
    img.fill(qRgb(borderRGB.r, borderRGB.g, borderRGB.b));

    // Pixel decode — classic ULA addressing
    const int flashPhase = 0;  // static for PoC (no flash animation)

    for (int y = 0; y < kScreenHeight; ++y) {
        // ULA pixel address: third | line_in_char | char_row
        const int addr = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
        const int attrRow = (y / 8) * 32;

        // Pointer to the start of this scanline in the output image
        uint8_t* scanline = img.scanLine(y + borderPx);

        for (int xByte = 0; xByte < 32; ++xByte) {
            const uint8_t byteVal = pixelData[addr + xByte];
            const uint8_t attr    = attrData[attrRow + xByte];

            const int ink   = attr & 0x07;
            const int paper = (attr >> 3) & 0x07;
            const int bright = (attr >> 6) & 0x01;
            const int flash  = (attr >> 7) & 0x01;

            const RGB* palette = bright ? kPaletteBright : kPaletteNormal;

            // Flash phase swaps ink/paper
            int fgIdx, bgIdx;
            if (flash && flashPhase) {
                fgIdx = paper;
                bgIdx = ink;
            } else {
                fgIdx = ink;
                bgIdx = paper;
            }

            const RGB& fg = palette[fgIdx];
            const RGB& bg = palette[bgIdx];

            // 8 horizontal pixels per byte, MSB first
            int base = (xByte * 8 + borderPx) * 3;
            for (int bit = 0; bit < 8; ++bit) {
                const RGB& color = (byteVal & (0x80 >> bit)) ? fg : bg;
                scanline[base]     = color.r;
                scanline[base + 1] = color.g;
                scanline[base + 2] = color.b;
                base += 3;
            }
        }
    }

    return img;
}

} // namespace ttd
