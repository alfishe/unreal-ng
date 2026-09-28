#pragma once
#include <cstdint>

#include "emulator/video/screen.h"

/// Pre-computed coordinates for one frame T-state of a ZX-class raster
/// (ScreenZX::CreateTstateLUT). Shared by every renderer that draws over the
/// ZX raster, so none of them divides or takes a modulo on the hot path.
struct TstateCoordLUT
{
    uint16_t framebufferX;      // Framebuffer X coordinate (UINT16_MAX if invisible)
    uint16_t framebufferY;      // Framebuffer Y coordinate
    uint16_t zxX;               // ZX screen X (UINT16_MAX if border/invisible)
    uint8_t zxY;                // ZX screen Y (255 if border/invisible)
    uint8_t symbolX;            // Pre-computed x / 8
    uint8_t pixelXBit;          // Pre-computed x % 8
    RenderTypeEnum renderType;  // RT_BLANK, RT_BORDER, or RT_SCREEN
    uint16_t screenOffset;      // Pre-computed _screenLineOffsets[y]
    uint16_t attrOffset;        // Pre-computed _attrLineOffsets[y]
};
