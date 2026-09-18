#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

/// @brief Point in device pixels
struct HudPoint
{
    int x = 0;
    int y = 0;
};

/// @brief Rectangle in device pixels
struct HudRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool empty() const { return w <= 0 || h <= 0; }
};

/// @brief Margins / insets in device pixels
struct HudMargins
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

/// @brief Predefined display positions for HUD tiles and elements
enum class HudTilePosition : uint8_t
{
    Custom = 0,     // Explicit coordinates in specified coord space
    TopLeft,        // Top-left corner
    TopRight,       // Top-right corner
    BottomLeft,     // Bottom-left corner
    BottomRight,    // Bottom-right corner
    MidTop,         // Top center / mid top
    MidBottom,      // Bottom center / mid bottom
    Center,         // Exact center of viewport
    MidLeft,        // Middle left edge
    MidRight        // Middle right edge
};

/// @brief Convert HudTilePosition to lowercase string identifier
inline std::string HudTilePositionToString(HudTilePosition pos)
{
    switch (pos)
    {
        case HudTilePosition::TopLeft: return "top_left";
        case HudTilePosition::TopRight: return "top_right";
        case HudTilePosition::BottomLeft: return "bottom_left";
        case HudTilePosition::BottomRight: return "bottom_right";
        case HudTilePosition::MidTop: return "mid_top";
        case HudTilePosition::MidBottom: return "mid_bottom";
        case HudTilePosition::Center: return "center";
        case HudTilePosition::MidLeft: return "mid_left";
        case HudTilePosition::MidRight: return "mid_right";
        case HudTilePosition::Custom:
        default: return "custom";
    }
}

/// @brief Parse HudTilePosition from string identifier
inline HudTilePosition HudTilePositionFromString(const std::string& str)
{
    std::string norm;
    norm.reserve(str.size());
    for (char c : str)
    {
        if (c != ' ' && c != '_' && c != '-')
        {
            norm.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }

    if (norm == "topleft") return HudTilePosition::TopLeft;
    if (norm == "topright") return HudTilePosition::TopRight;
    if (norm == "bottomleft") return HudTilePosition::BottomLeft;
    if (norm == "bottomright") return HudTilePosition::BottomRight;
    if (norm == "midtop" || norm == "top" || norm == "topcenter") return HudTilePosition::MidTop;
    if (norm == "midbottom" || norm == "bottom" || norm == "bottomcenter") return HudTilePosition::MidBottom;
    if (norm == "center" || norm == "centre" || norm == "viewportcenter") return HudTilePosition::Center;
    if (norm == "midleft" || norm == "left") return HudTilePosition::MidLeft;
    if (norm == "midright" || norm == "right") return HudTilePosition::MidRight;
    return HudTilePosition::Custom;
}

/// @brief Compute target rectangle for a predefined tile position within a viewport
inline HudRect ComputeTilePlacement(
    HudTilePosition position,
    int width,
    int height,
    const HudRect& viewport,
    const HudMargins& margins = HudMargins{16, 16, 16, 16},
    int offsetX = 0,
    int offsetY = 0)
{
    if (position == HudTilePosition::Custom)
    {
        return HudRect{viewport.x + offsetX, viewport.y + offsetY, width, height};
    }

    int x = viewport.x;
    int y = viewport.y;

    switch (position)
    {
        case HudTilePosition::TopLeft:
            x = viewport.x + margins.left + offsetX;
            y = viewport.y + margins.top + offsetY;
            break;

        case HudTilePosition::TopRight:
            x = viewport.x + viewport.w - width - margins.right + offsetX;
            y = viewport.y + margins.top + offsetY;
            break;

        case HudTilePosition::BottomLeft:
            x = viewport.x + margins.left + offsetX;
            y = viewport.y + viewport.h - height - margins.bottom + offsetY;
            break;

        case HudTilePosition::BottomRight:
            x = viewport.x + viewport.w - width - margins.right + offsetX;
            y = viewport.y + viewport.h - height - margins.bottom + offsetY;
            break;

        case HudTilePosition::MidTop:
            x = viewport.x + (viewport.w - width) / 2 + offsetX;
            y = viewport.y + margins.top + offsetY;
            break;

        case HudTilePosition::MidBottom:
            x = viewport.x + (viewport.w - width) / 2 + offsetX;
            y = viewport.y + viewport.h - height - margins.bottom + offsetY;
            break;

        case HudTilePosition::Center:
            x = viewport.x + (viewport.w - width) / 2 + offsetX;
            y = viewport.y + (viewport.h - height) / 2 + offsetY;
            break;

        case HudTilePosition::MidLeft:
            x = viewport.x + margins.left + offsetX;
            y = viewport.y + (viewport.h - height) / 2 + offsetY;
            break;

        case HudTilePosition::MidRight:
            x = viewport.x + viewport.w - width - margins.right + offsetX;
            y = viewport.y + (viewport.h - height) / 2 + offsetY;
            break;

        case HudTilePosition::Custom:
        default:
            x = viewport.x + offsetX;
            y = viewport.y + offsetY;
            break;
    }

    return HudRect{x, y, width, height};
}

/// @brief Host output surface geometry in device pixels
struct HudSurface
{
    HudRect outputRect;     // full output surface in device pixels
    HudRect imageRect;      // where the emulator picture sits inside outputRect
    int dpr = 1;            // device pixel ratio (integer — fractional DPR rounds up)
    HudMargins safeInsets;  // safe area insets in device pixels
};

/// @brief Computed placement for a single HUD element
struct HudPlacement
{
    std::string elementId;
    HudRect deviceRect;
    bool inLetterboxBand = false;
};

/// @brief Damaged / dirty region for targeted repainting
struct HudDirtyRegion
{
    std::vector<HudRect> rects;
};

/// @brief Coordinate space for element placement
enum class HudCoordSpace : uint8_t
{
    Picture,    // Locked to emulator screen picture (256x192 / 352x288)
    Output,     // Relative to host output window / viewport surface
    Normalized  // Normalized [0..1000] within target canvas
};

/// @brief Maps element source rectangle to absolute device pixels given coordinate space and surface
inline HudRect MapToDeviceRect(
    const HudRect& srcRect,
    HudCoordSpace space,
    const HudPoint& refSize,
    const HudSurface& surface,
    HudTilePosition position = HudTilePosition::Custom,
    const HudMargins& margins = HudMargins{16, 16, 16, 16},
    float uiScale = 1.0f)
{
    if (position != HudTilePosition::Custom)
    {
        const HudRect& vp = (space == HudCoordSpace::Picture) ? surface.imageRect : surface.outputRect;
        int w = static_cast<int>(srcRect.w * (space == HudCoordSpace::Picture ? 1.0f : uiScale));
        int h = static_cast<int>(srcRect.h * (space == HudCoordSpace::Picture ? 1.0f : uiScale));
        HudMargins scaledMargins{
            static_cast<int>(margins.left * uiScale),
            static_cast<int>(margins.top * uiScale),
            static_cast<int>(margins.right * uiScale),
            static_cast<int>(margins.bottom * uiScale)
        };
        int offX = static_cast<int>(srcRect.x * uiScale);
        int offY = static_cast<int>(srcRect.y * uiScale);
        return ComputeTilePlacement(position, w, h, vp, scaledMargins, offX, offY);
    }

    if (space == HudCoordSpace::Picture)
    {
        int rw = (refSize.x > 0) ? refSize.x : 256;
        int rh = (refSize.y > 0) ? refSize.y : 192;
        int dx = surface.imageRect.x + (srcRect.x * surface.imageRect.w) / rw;
        int dy = surface.imageRect.y + (srcRect.y * surface.imageRect.h) / rh;
        int dw = (srcRect.w * surface.imageRect.w) / rw;
        int dh = (srcRect.h * surface.imageRect.h) / rh;
        return HudRect{dx, dy, dw, dh};
    }
    else if (space == HudCoordSpace::Normalized)
    {
        int dx = surface.outputRect.x + (srcRect.x * surface.outputRect.w) / 1000;
        int dy = surface.outputRect.y + (srcRect.y * surface.outputRect.h) / 1000;
        int dw = (srcRect.w * surface.outputRect.w) / 1000;
        int dh = (srcRect.h * surface.outputRect.h) / 1000;
        return HudRect{dx, dy, dw, dh};
    }
    // Output space: relative to outputRect
    return HudRect{surface.outputRect.x + srcRect.x, surface.outputRect.y + srcRect.y, srcRect.w, srcRect.h};
}
