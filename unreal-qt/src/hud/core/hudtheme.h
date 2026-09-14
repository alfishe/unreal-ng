#pragma once

#include "hudsurface.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/// @brief Pre-defined HUD themes
enum class HudThemeId : uint8_t
{
    DarkGlass,   // Modern Keynote / Apple Liquid Dark Glass (default)
    RetroZX,     // Sinclair ZX Spectrum authentic 8-color palette & pixel framing
    Cyberpunk,   // High-contrast neon cyan & hot magenta on deep indigo
    AmberCRT,    // Phosphor amber monochrome CRT
    EmeraldCRT,  // P1 phosphor green monochrome terminal
    LightModern  // Clean frosted light modern theme
};

/// @brief Styling for a whole tile (frame, background, border, corner radius, padding)
struct HudTileFrameStyle
{
    uint32_t backgroundColor{0xDD10141C};       // ARGB32 primary background
    uint32_t backgroundGradientEnd{0x00000000}; // ARGB32 gradient end (0 = solid)
    uint32_t borderColor{0x2EFFFFFF};           // ARGB32 border stroke
    float borderWidth{1.0f};                    // Border thickness in points
    float borderRadius{12.0f};                  // Corner radius in points
    uint32_t shadowColor{0x66000000};           // ARGB32 drop shadow / outer glow
    float shadowBlur{10.0f};
    HudMargins padding{12, 10, 12, 10};         // Inner content insets (left, top, right, bottom)
    bool glassEffect{true};                     // Specular translucent highlight
};

/// @brief Styling for tile content (typography, colors, icons)
struct HudTileContentStyle
{
    std::string fontFamily{"Inter"};
    float titleFontSize{11.0f};                 // Base font size in points (multiplied by UI scale)
    float bodyFontSize{9.5f};                   // Base body size in points
    uint32_t titleColor{0xFFFFFFFF};            // ARGB32 title text
    uint32_t bodyColor{0xFFB8C2CC};             // ARGB32 secondary / body text
    uint32_t accentColor{0xFF38BDF8};           // ARGB32 highlight / accent color
    uint32_t iconColor{0xFFE2E8F0};             // ARGB32 icon stroke / fill
    float iconStrokeWidth{1.8f};
    uint32_t badgeBgColor{0xB0475569};          // ARGB32 badge background
    uint32_t badgeTextColor{0xFFFFFFFF};        // ARGB32 badge text
};

/// @brief Complete tile style combining whole-tile frame and inner content
struct HudTileStyle
{
    HudTileFrameStyle frame;
    HudTileContentStyle content;
};

/// @brief Complete theme defining global and specialized styles for tiles and HUD overlays
class HudTheme
{
public:
    HudThemeId id{HudThemeId::DarkGlass};
    std::string name{"Dark Glass"};
    std::string description{"Modern translucent keynote dark glass"};

    HudTileStyle defaultTile;                   // Default style for arbitrary tiles / panels
    HudTileStyle toast;                         // Style for transient toasts
    HudTileStyle indicator;                     // Style for persistent status pills
    HudTileStyle alert;                         // Style for high-priority alerts / breakpoints

    std::unordered_map<std::string, HudTileStyle> customStyles;

    /// @brief Resolve style for a given style ID, falling back to defaultTile
    const HudTileStyle& resolveStyle(const std::string& styleId = "") const;

    /// @brief Factory methods for pre-built themes
    static HudTheme CreateDarkGlass();
    static HudTheme CreateRetroZX();
    static HudTheme CreateCyberpunk();
    static HudTheme CreateAmberCRT();
    static HudTheme CreateEmeraldCRT();
    static HudTheme CreateLightModern();

    /// @brief Get pre-built theme by ID
    static const HudTheme& FromId(HudThemeId id);

    /// @brief Get pre-built theme by name
    static const HudTheme& FromName(const std::string& name);

    /// @brief List all available theme IDs
    static std::vector<HudThemeId> AvailableThemes();

    /// @brief Get display name of a theme ID
    static std::string ThemeName(HudThemeId id);
};
