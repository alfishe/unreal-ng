#include "palette.h"

namespace zxdlss
{

Palette Palette::fromRGBA(const uint32_t* rgba)
{
    Palette p;
    for (int c = 0; c < 16; ++c)
    {
        // RGBA8888 as the emulator's framebuffer: little-endian uint32 0xAABBGGRR
        p.rgb[c] = {static_cast<uint8_t>(rgba[c] & 0xFF), static_cast<uint8_t>((rgba[c] >> 8) & 0xFF),
                    static_cast<uint8_t>((rgba[c] >> 16) & 0xFF)};
        for (int k = 0; k < 3; ++k)
            p.linear[c][k] = srgbToLinear(p.rgb[c][k]);
        p.luma[c] = static_cast<float>(0.299 * p.rgb[c][0] + 0.587 * p.rgb[c][1] + 0.114 * p.rgb[c][2]);
        p.source[c] = rgba[c] | 0xFF000000u;
    }
    return p;
}

}  // namespace zxdlss
