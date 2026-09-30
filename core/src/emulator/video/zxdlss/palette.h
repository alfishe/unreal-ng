#pragma once

/// @file palette.h
/// @brief The ZX palette an algorithm mixes with - the emulator's active one,
/// passed with every frame (FrameInput::palette) - its brightness and the
/// linear-light conversions (algorithm-mod-tpgw.md sections 3.1 and 8).

#include <array>
#include <cmath>
#include <cstdint>

namespace zxdlss
{

struct Palette
{
    std::array<std::array<uint8_t, 3>, 16> rgb{};
    std::array<std::array<double, 3>, 16> linear{};   ///< srgb_to_linear per channel
    std::array<float, 16> luma{};                      ///< Rec. 601 on the 0..255 values
    std::array<uint32_t, 16> source{};                 ///< the RGBA8888 values it was built from

    /// From the 16 colors as the emulator draws them: RGBA8888, little-endian
    /// uint32 0xAABBGGRR (Screen::GetRGBAPalette16)
    static Palette fromRGBA(const uint32_t* rgba);

    /// The palette these 16 colors make (alpha ignored)
    bool sameAs(const uint32_t* rgba) const
    {
        for (int c = 0; c < 16; ++c)
            if ((rgba[c] | 0xFF000000u) != source[c])
                return false;
        return true;
    }

    static double srgbToLinear(uint8_t c)
    {
        const double v = c / 255.0;
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    }

    /// linear -> sRGB 8 bit, round half to even (as NumPy's round)
    static uint8_t linearToSrgb(double v)
    {
        v = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
        const double s = (v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055) * 255.0;
        return static_cast<uint8_t>(std::nearbyint(s));
    }
};

}  // namespace zxdlss
