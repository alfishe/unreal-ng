#pragma once

/// @file videomap.h
/// @brief Plain result types of the video debug translation (PLAN #42,
/// docs/inprogress/2026-09-27-video-debug-translation/design.md §4): which
/// memory makes a pixel, which pixels a byte feeds, where the beam is in a
/// mode's own coordinates. Debug path only - nothing here is on the render path.
///
/// Worked example (48K, beam line 100, T 50): layer "zx", pixel (52, 28); its
/// sources are the pixel byte at RAM page 5 offset ZxGeometry::PixelOffset(28, 6)
/// (bit 7 - 52 % 8) and the attribute at page 5 offset 0x1800 + 3 * 32 + 6.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/platform.h"
#include "emulator/video/screen.h"

namespace videomap
{
/// Where a source byte lives (design G4: physical locations, not Z80 addresses)
enum class Space : uint8_t
{
    Ram,
    Rom,
    Vram,
    Palette,        ///< palette RAM cell (ATM / AlCo #FF palette, Profi palette)
    SpriteRam,
    TileRam,
    CopperRam,
    Register,       ///< a colour or mode from a port latch (#FE border, Timex #FF); page = port
    InternalTable,  ///< emulator data with no machine address (the ATM text font, atmfont.h)
};

/// What a source contributes to the pixel
enum class SourceRole : uint8_t
{
    PixelBits,
    Attribute,
    Plane,
    CharCode,
    CharAttr,
    FontRow,
    TileDescriptor,
    TileGraphic,
    SpriteDescriptor,
    SpriteGraphic,
    PaletteEntry,
    Border,
    ModeDescriptor,
    Selector,
};

struct SourceRef
{
    Space space = Space::Ram;
    uint8_t module = 0;       ///< ZX-Poly module 0..3
    uint16_t page = 0;        ///< page in that space; the port for Space::Register
    uint32_t offset = 0;      ///< byte offset in the page
    uint8_t width = 1;        ///< 1 or 2 bytes (16-bit palette cells)
    uint16_t bitMask = 0xFF;  ///< the bits this pixel uses
    SourceRole role = SourceRole::PixelBits;

    bool operator==(const SourceRef& o) const
    {
        return space == o.space && module == o.module && page == o.page && offset == o.offset && width == o.width &&
               bitMask == o.bitMask && role == o.role;
    }
};

enum class SurfaceUnit : uint8_t
{
    Pixel,
    TextCell,
};

/// A layer's picture in its own pixels. Text layers are addressed in pixels
/// too (640x200 for 80x25 cells of 8x8); textColumns / cellWidth describe the grid
struct SurfaceDesc
{
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t bitsPerPixel = 1;
    uint16_t textColumns = 0, textRows = 0;  ///< non-zero for text layers
    uint8_t cellWidth = 8, cellHeight = 8;
};

/// Where the layer is in the beam (design §3.1 origin: T 0 = first T of the
/// left border, line 0 = first vsync line)
struct LayerWindow
{
    uint16_t firstLine = 0;
    uint16_t lineCount = 0;
    uint16_t firstT = 0;   ///< T in line of the first pixel
    uint16_t tCount = 0;
    uint8_t dotsPerT = 2;  ///< 2 (7 MHz) or 4 (14 MHz)
};

/// Surface pixel (x, y) is framebuffer pixel (left + x, top + y)
struct FramebufferMap
{
    uint16_t width = 0, height = 0;
    uint16_t surfaceLeft = 0, surfaceTop = 0;
};

struct LayerDesc
{
    std::string id;      ///< "zx", "atm16", "atmhr", "atmtx", "atmtl", "p16", "pmc", "profihr"
    SurfaceDesc surface;
    LayerWindow window;
};

struct VideoLayout
{
    bool mapped = false;  ///< false: no mapper for this mode (NullMapper) - frame geometry only
    std::string family;   ///< "zx", "alco", "atm", "profi", "none"
    VideoModeEnum mode = M_NUL;
    uint16_t tstatesPerLine = 0;
    uint16_t lines = 0;
    std::vector<LayerDesc> layers;
    FramebufferMap fb;
};

/// One layer's part of a pixel's colour
struct LayerContribution
{
    std::string layer;
    std::vector<SourceRef> sources;  ///< memory first, palette / register last
    uint8_t colourIndex = 0;         ///< index before the palette, from memory as it is now
    uint32_t rgb = 0;                ///< after the palette (ABGR)
};

struct PixelSources
{
    bool valid = false;
    bool border = false;             ///< the point is border, not a layer pixel
    LayerContribution contribution;  ///< classic modes: one layer (composition Select)
    uint32_t finalRgb = 0;
    uint32_t renderedRgb = 0;        ///< what the framebuffer holds
    bool renderedKnown = false;
    std::vector<uint16_t> z80;       ///< Z80 addresses of the first memory source under current paging

    /// Time labels (design §4.6). Addresses follow the latches at stateAtT
    /// (-1: now); colours come from memory and palettes as they are now
    int64_t stateAtT = -1;
    uint64_t stateFrame = 0;         ///< the frame whose history answered (stateAtT >= 0)
    bool statePartial = false;       ///< that frame's write log overflowed: later writes are missing
    bool fromSnapshot = false;       ///< the machine was running: the last completed frame answered
};

/// A rectangle of a layer that a byte feeds
struct SurfaceArea
{
    std::string layer;
    uint16_t x = 0, y = 0, width = 0, height = 0;
};

struct TextCell
{
    uint8_t code = 0;
    uint8_t attr = 0;
    SourceRef codeSource;
    SourceRef attrSource;
};

/// The latches and settings a mode's geometry and memory depend on (design
/// §4.1). Derived from EmulatorState / CONFIG / Screen, never stored
struct VideoState
{
    VideoModeEnum mode = M_NUL;
    MEM_MODEL model = MM_PENTAGON;
    uint8_t p7FFD = 0;
    uint8_t pFE = 0;
    uint8_t borderIndex = 0;       ///< as the ZX / Profi renderer latched it (Screen::GetBorderColor)
    uint8_t borderAttr = 0;        ///< EmulatorState::border_attr (ATM / AlCo border through the #FF palette)
    bool atmBorderBright = false;
    bool flashPhase = false;       ///< AlCo PMC blinks FLASH
    bool profiMonochrome = false;
    uint16_t zxScreenPage = 5;     ///< the page the ZX renderer draws from
    uint16_t ramMask = 0xFF;
    const uint32_t* atmPalette = nullptr;   ///< ATM / AlCo #FF palette cells (16)
    const uint16_t* profiPalette = nullptr; ///< Profi palette cells (16)
    RasterDescriptor layoutDesc{};          ///< framebuffer storage of the mode
    RasterDescriptor timingDesc{};          ///< beam timing of the mode
    /// The family's own state for its mapper (Screen::VideoFamilyView): TS-Conf
    /// registers, line table and CRAM; null for the classic families
    const void* familyView = nullptr;
};
} // namespace videomap
