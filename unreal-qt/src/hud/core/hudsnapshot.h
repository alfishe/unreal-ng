#pragma once

#include "hudsurface.h"
#include "hudtheme.h"
#include "hudtiming.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// @brief Functional kind of HUD element
enum class HudKind : uint8_t
{
    Toast,
    Indicator,
    Stats,
    Banner,
    Image,      // Standalone RGBA image / sprite with alpha channel
    Tile,       // Sub-rectangle from a tileset / spritesheet
    Tilemap,    // Dense grid of indexed tiles referencing a shared tileset
    Text        // Arbitrary positioned text
};

/// @brief Screen anchoring position for HUD elements
enum class HudAnchor : uint8_t
{
    None = 0,
    TopLeft,
    Top,        // Mid top
    TopRight,
    Left,       // Mid left
    Center,     // Center of viewport
    Right,      // Mid right
    BottomLeft,
    Bottom,     // Mid bottom
    BottomRight
};

/// @brief Map HudAnchor to HudTilePosition
inline HudTilePosition HudAnchorToTilePosition(HudAnchor anchor)
{
    switch (anchor)
    {
        case HudAnchor::TopLeft: return HudTilePosition::TopLeft;
        case HudAnchor::TopRight: return HudTilePosition::TopRight;
        case HudAnchor::BottomLeft: return HudTilePosition::BottomLeft;
        case HudAnchor::BottomRight: return HudTilePosition::BottomRight;
        case HudAnchor::Top: return HudTilePosition::MidTop;
        case HudAnchor::Bottom: return HudTilePosition::MidBottom;
        case HudAnchor::Center: return HudTilePosition::Center;
        case HudAnchor::Left: return HudTilePosition::MidLeft;
        case HudAnchor::Right: return HudTilePosition::MidRight;
        default: return HudTilePosition::Custom;
    }
}

/// @brief Map HudTilePosition to HudAnchor
inline HudAnchor HudTilePositionToAnchor(HudTilePosition pos)
{
    switch (pos)
    {
        case HudTilePosition::TopLeft: return HudAnchor::TopLeft;
        case HudTilePosition::TopRight: return HudAnchor::TopRight;
        case HudTilePosition::BottomLeft: return HudAnchor::BottomLeft;
        case HudTilePosition::BottomRight: return HudAnchor::BottomRight;
        case HudTilePosition::MidTop: return HudAnchor::Top;
        case HudTilePosition::MidBottom: return HudAnchor::Bottom;
        case HudTilePosition::Center: return HudAnchor::Center;
        case HudTilePosition::MidLeft: return HudAnchor::Left;
        case HudTilePosition::MidRight: return HudAnchor::Right;
        default: return HudAnchor::None;
    }
}

/// @brief Toast urgency / presentation priority
enum class HudPriority : uint8_t
{
    Low,
    Normal,
    High,
    Critical
};

/// @brief Dynamic state for persistent indicators
enum class HudState : uint8_t
{
    Off,
    Idle,
    Active,
    Alert
};

/// @brief Transition animations
enum class HudAnimation : uint8_t
{
    None,
    Fade,
    SlideFade,
    ScaleFade,
    Pulse
};

/// @brief Texture filtering mode when scaling
enum class HudFilterMode : uint8_t
{
    Nearest,    // Crisp retro pixel art
    Linear      // Smooth bilinear interpolation
};

/// @brief Text alignment
enum class HudTextAlign : uint8_t
{
    Left,
    Center,
    Right
};

/// @brief Pixel format of image buffers
enum class HudPixelFormat : uint8_t
{
    RGBA8888,
    BGRA8888,
    ARGB8888_Premul
};

/// Clock used for HUD timestamps and TTL expiry (steady_clock)
using HudClock = std::chrono::steady_clock;

/// @brief Ref-counted pixel buffer for images and tilesets
class HudImageBuffer
{
public:
    HudImageBuffer(int width, int height, HudPixelFormat format, std::vector<uint8_t> data, int stride = 0)
        : _width(width), _height(height), _format(format), _data(std::move(data))
    {
        _stride = (stride > 0) ? stride : (width * 4);
    }

    static std::shared_ptr<HudImageBuffer> CreateRgba(int width, int height, const uint8_t* rgba, int stride = 0)
    {
        int s = (stride > 0) ? stride : (width * 4);
        std::vector<uint8_t> buf(rgba, rgba + (s * height));
        return std::make_shared<HudImageBuffer>(width, height, HudPixelFormat::RGBA8888, std::move(buf), s);
    }

    int width() const { return _width; }
    int height() const { return _height; }
    int stride() const { return _stride; }
    HudPixelFormat format() const { return _format; }
    const uint8_t* data() const { return _data.data(); }
    size_t sizeBytes() const { return _data.size(); }

private:
    int _width = 0;
    int _height = 0;
    int _stride = 0;
    HudPixelFormat _format = HudPixelFormat::RGBA8888;
    std::vector<uint8_t> _data;
};

/// @brief Dense tilemap grid definition
struct HudTilemapData
{
    std::shared_ptr<const HudImageBuffer> tileset;
    int tileWidth = 8;
    int tileHeight = 8;
    int cols = 0;
    int rows = 0;
    std::vector<uint16_t> tileIndices; // 0xFFFF = transparent / empty
};

/// @brief A single rendered element in the HUD snapshot
struct HudElement
{
    std::string id;                         // stable key; toasts get "toast/<n>", indicators "ind/fdd/A"
    HudKind kind{HudKind::Toast};
    HudAnchor anchor{HudAnchor::Bottom};
    HudTilePosition position{HudTilePosition::Custom}; // Predefined display position
    HudMargins margins{16, 16, 16, 16};     // Viewport safe area margins
    HudPriority priority{HudPriority::Normal};
    HudClock::time_point created{};
    std::chrono::milliseconds ttl{0};      // 0 = persistent
    HudAnimation enter{HudAnimation::Fade};
    HudAnimation exit{HudAnimation::Fade};

    std::string title;                     // toast title / indicator label / text content
    std::string body;                      // toast body
    std::string icon;                      // symbolic name ("floppy", "tape", "breakpoint", "rec", "pause", "turbo", "file")
    std::string value;                     // indicator value ("A:12", "2x")
    HudState state{HudState::Idle};
    float progress{-1.f};                  // 0..1 when >= 0 (toast progress bar)
    uint32_t coalesced{0};                 // dedup counter shown as "x3"
    std::string dedupKey;                  // equal keys coalesce
    std::string styleId;                   // optional theme style override ("alert", "neon", "retro", etc.)
    bool monospace{false};                 // render with monowidth/fixed-pitch font

    // Picture augmentation geometry & rendering attributes
    HudRect rect{};                        // destination rect in coordSpace
    HudRect srcRect{};                     // source rect in image/tileset (empty = full image)
    HudCoordSpace coordSpace{HudCoordSpace::Output};
    HudPoint referenceSize{256, 192};      // native reference coordinate system for Picture space
    int32_t zIndex{0};                     // drawing order: lower draws first, higher on top
    float opacity{1.0f};                   // 0.0 .. 1.0
    uint32_t color{0xFFFFFFFF};            // tint or text color (ARGB32)
    uint32_t bgColor{0x00000000};          // background color (ARGB32, 0 = transparent)
    HudFilterMode filter{HudFilterMode::Nearest};
    HudTextAlign textAlign{HudTextAlign::Left};
    float fontSize{13.0f};

    std::shared_ptr<const HudImageBuffer> image;
    std::shared_ptr<const HudTilemapData> tilemap;
};

/// @brief Immutable snapshot published to presenters atomically
struct HudSnapshot
{
    uint64_t generation{0};
    HudClock::time_point produced{};
    HudThemeId themeId{HudThemeId::DarkGlass};
    float scaleFactor{2.0f};               // Base HiDPI UI scale multiplier (default 2.0x)
    HudTilePosition toastPosition{HudTilePosition::MidBottom};
    HudTilePosition indicatorPosition{HudTilePosition::TopRight};
    std::vector<HudElement> elements;      // ordered by zIndex ascending, then priority desc, age asc
};

/// @brief Request to create a new toast notification
struct HudToastRequest
{
    std::string title;
    std::string body;
    std::string icon;
    std::string dedupKey;
    bool coalesceCount{true};
    HudPriority priority{HudPriority::Normal};
    std::chrono::milliseconds ttl{HudTiming::ToastDefault};
    HudAnchor anchor{HudAnchor::Bottom};
    HudTilePosition position{HudTilePosition::MidBottom};
    HudMargins margins{16, 16, 16, 16};
    float progress{-1.f};
};

/// @brief Request to add or update an augmented image
struct HudImageRequest
{
    std::shared_ptr<const HudImageBuffer> image;
    HudRect rect;
    HudRect srcRect{};
    HudTilePosition position{HudTilePosition::Custom};
    HudMargins margins{16, 16, 16, 16};
    HudCoordSpace coordSpace{HudCoordSpace::Picture};
    HudPoint referenceSize{256, 192};
    int32_t zIndex{0};
    float opacity{1.0f};
    uint32_t tint{0xFFFFFFFF};
    HudFilterMode filter{HudFilterMode::Nearest};
    std::string styleId;
};

/// @brief Request to add or update an augmented tile
struct HudTileRequest
{
    std::shared_ptr<const HudImageBuffer> tileset;
    HudRect rect;
    HudRect srcRect;
    HudTilePosition position{HudTilePosition::Custom};
    HudMargins margins{16, 16, 16, 16};
    HudCoordSpace coordSpace{HudCoordSpace::Picture};
    HudPoint referenceSize{256, 192};
    int32_t zIndex{0};
    float opacity{1.0f};
    uint32_t tint{0xFFFFFFFF};
    HudFilterMode filter{HudFilterMode::Nearest};
    std::string styleId;
};

/// @brief Request to add or update a dense tilemap
struct HudTilemapRequest
{
    std::shared_ptr<const HudTilemapData> tilemap;
    HudRect destRect;
    HudTilePosition position{HudTilePosition::Custom};
    HudMargins margins{16, 16, 16, 16};
    HudCoordSpace coordSpace{HudCoordSpace::Picture};
    HudPoint referenceSize{256, 192};
    int32_t zIndex{0};
    float opacity{1.0f};
    HudFilterMode filter{HudFilterMode::Nearest};
    std::string styleId;
};

/// @brief Request to add or update positioned text
struct HudTextRequest
{
    std::string text;
    HudRect rect;
    HudTilePosition position{HudTilePosition::Custom};
    HudMargins margins{16, 16, 16, 16};
    HudCoordSpace coordSpace{HudCoordSpace::Picture};
    HudPoint referenceSize{256, 192};
    int32_t zIndex{10};
    float opacity{1.0f};
    uint32_t color{0xFFFFFFFF};
    uint32_t bgColor{0x00000000};
    HudTextAlign align{HudTextAlign::Left};
    float fontSize{13.0f};
    std::string styleId;
};
