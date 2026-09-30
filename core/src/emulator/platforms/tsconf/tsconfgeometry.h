#pragma once

#include <cstdint>

/// @file tsconfgeometry.h
/// @brief TS-Conf raster and graphics window geometry (hardware-spec §4.1-§4.2).

namespace TsConfGeometry
{
    constexpr uint32_t kLineTacts = 224;         ///< 448 dots at 7 MHz
    constexpr uint32_t kLines = 320;
    constexpr uint32_t kFirstVisibleLine = 32;   ///< lines 0-31: vertical blank
    constexpr uint32_t kFirstVisibleDot = 88;    ///< dots 0-87: horizontal blank

    /// Graphics window per V_CONFIG[7:6], in raster dots / lines
    struct Window
    {
        uint16_t x0, y0, w, h;
    };
    constexpr Window kWindows[4] = {
        {140, 80, 256, 192},
        {108, 76, 320, 200},
        {108, 56, 320, 240},
        {88, 32, 360, 288},
    };

    inline const Window& WindowOf(uint8_t vConfig) { return kWindows[vConfig >> 6]; }
    inline bool LineInWindow(uint8_t vConfig, uint32_t line)
    {
        const Window& w = WindowOf(vConfig);
        return line >= w.y0 && line < static_cast<uint32_t>(w.y0 + w.h);
    }
}
