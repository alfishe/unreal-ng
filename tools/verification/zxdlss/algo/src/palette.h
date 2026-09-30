#pragma once

/// @file palette.h
/// @brief The emulator's ZX palette, brightness and the linear-light mixer
/// (algorithm-mod-tpgw.md sections 3.1 and 8).

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

    static const Palette& instance();

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
