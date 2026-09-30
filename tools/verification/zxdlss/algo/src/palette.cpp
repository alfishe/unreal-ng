#include "palette.h"

namespace zxdlss
{

const Palette& Palette::instance()
{
    static const Palette palette = [] {
        // unreal-ng palette (core/src/emulator/video/zx/screenzx.cpp, ABGR)
        static const uint32_t abgr[16] = {0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700,
                                          0xFF2AC8CC, 0xFFCACACA, 0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF,
                                          0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF};
        Palette p;
        for (int c = 0; c < 16; ++c)
        {
            p.rgb[c] = {static_cast<uint8_t>(abgr[c] & 0xFF), static_cast<uint8_t>((abgr[c] >> 8) & 0xFF),
                        static_cast<uint8_t>((abgr[c] >> 16) & 0xFF)};
            for (int k = 0; k < 3; ++k)
                p.linear[c][k] = srgbToLinear(p.rgb[c][k]);
            p.luma[c] = static_cast<float>(0.299 * p.rgb[c][0] + 0.587 * p.rgb[c][1] + 0.114 * p.rgb[c][2]);
        }
        return p;
    }();
    return palette;
}

}  // namespace zxdlss
