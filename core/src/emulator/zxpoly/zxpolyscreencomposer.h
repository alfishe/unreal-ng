#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/// Composes the ZX-Poly picture from the four modules' video memory.
///
/// A pure function: no emulator state, so it is testable with hand-made
/// buffers and golden frames. Rules follow the zxpoly reference
/// (zxpoly-emul/.../components/video/VideoController.java):
///
///   mode 0-3  classic attributed picture of module n, doubled 2x2
///   mode 4    per pixel idx = CPU3*8 | CPU0*4 | CPU1*2 | CPU2*1
///   mode 5    per source pixel a 2x2 block: CPU0 top-left, CPU1 top-right,
///             CPU2 bottom-left, CPU3 bottom-right, each with its own
///             attribute (FLASH blinks)
///   mode 6    CPU0 attribute (FLASH blinks): ink == paper floods the cell
///             with that colour, otherwise mode-4 pixels
///   mode 7    CPU0 attribute, FLASH is a selector: 0 -> mode-5 layout in
///             CPU0's ink/paper; 1 -> ink == paper flood, else mode-4 pixels
///
/// Colours are compared as palette entries after BRIGHT is applied, as the
/// reference compares RGB values.
class ZXPolyScreenComposer
{
public:
    static constexpr size_t MODULES = 4;
    static constexpr size_t SCREEN_BYTES = 6912;        // 6144 bitmap + 768 attributes
    static constexpr unsigned OUT_WIDTH = 512;
    static constexpr unsigned OUT_HEIGHT = 384;

    /// @param vram       per module, the 6912-byte screen (bitmap + attributes)
    /// @param mode       ZX-Poly video mode 0..7 (#3D00 D2-D4)
    /// @param flashPhase true while FLASH cells show inverted
    /// @param palette    16 colours in framebuffer format, ZX order
    ///                   (bit0 blue, bit1 red, bit2 green, bit3 bright)
    /// @param out        OUT_WIDTH * OUT_HEIGHT pixels
    static void Compose(const std::array<const uint8_t*, MODULES>& vram, uint8_t mode, bool flashPhase,
                        const uint32_t* palette, uint32_t* out);

    /// Offset of the bitmap byte holding pixel (x, y) inside a ZX screen
    static inline size_t BitmapOffset(unsigned x, unsigned y)
    {
        return ((y & 0xC0u) << 5) | ((y & 0x07u) << 8) | ((y & 0x38u) << 2) | (x >> 3);
    }

    /// Offset of the attribute byte for pixel (x, y)
    static inline size_t AttributeOffset(unsigned x, unsigned y)
    {
        return 6144 + (y >> 3) * 32 + (x >> 3);
    }
};
