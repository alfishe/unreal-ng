#pragma once

/// @file nextlayerframe.h
/// @brief One frame of the Next picture split into its layers, exactly as the compositor was given them
/// (docs/inprogress/2026-10-07-zx-next/design-layer-capture.md): the ULA, Layer 2, tilemap and sprite pixels of the 640 x 256
/// grid (lines not doubled), which layer won each pixel, and the register state each line segment was drawn with.
///
/// ScreenNext owns two of these - the one being drawn and the last completed one - and swaps them at the end of a frame, so a
/// reader (a WebAPI request, a Lua call) takes the last complete frame and never a half-drawn one. The record is compact (16 bits
/// per layer pixel); the conversion to RGBA happens on the reader's thread.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/zeroinitbuffer.h"

namespace NextLayer
{
constexpr unsigned kWidth = 640;   ///< sub-pixels per grid line (the NextVideoRenderer frame width)
constexpr unsigned kHeight = 256;  ///< grid lines (the framebuffer has every line twice)

/// The planes, in the order of NextLayerFrame::plane
enum Plane : unsigned
{
    kUla = 0,
    kLayer2 = 1,
    kTilemap = 2,
    kSprites = 3,
    kPlanes = 4
};

/// What won a pixel (NextLayerFrame::ids)
enum Id : uint8_t
{
    kFallback = 0,  ///< nothing drew: the fallback colour NR #4A
    kIdUla = 1,     ///< the ULA paper / ink
    kIdBorder = 2,  ///< the ULA border colour
    kIdLayer2 = 3,
    kIdTilemap = 4,
    kIdSprite = 5,
    kIdBlend = 6,    ///< layer order 110 / 111: Layer 2 added to / subtracted from the mix layer
    kIdStencil = 7,  ///< NR #68 bit 0: the ULA and tilemap pixels ANDed
    kIdCount = 8
};

/// One pixel of a layer: what NextVideoRenderer's layer functions paint and the compositor reads, 16 bits. The renderer builds a line
/// of these in the sink's row itself when a capture is on, so recording a plane costs no extra pass
struct Pixel
{
    uint16_t colour : 10 = 0;   ///< 9-bit RRRGGGBBb; bit 9: Layer 2 priority (above everything)
    uint16_t opaque : 1 = 0;    ///< the compositor sees this pixel: painted and not in the global transparency colour (NR #14; a
                                ///< text-mode tile pixel and the ULA / Layer 2 pixels; a sprite pixel is opaque whatever its colour)
    uint16_t border : 1 = 0;    ///< ULA: the border colour
    uint16_t below : 1 = 0;     ///< tilemap: the ULA pixel is above this one
    uint16_t textMode : 1 = 0;  ///< tilemap: a text-mode tile
    uint16_t spare : 2 = 0;
};
static_assert(sizeof(Pixel) == 2, "a plane pixel is 16 bits");

/// A pixel built in registers and stored once (assigning the bit-fields of a pixel in memory one by one is a read-modify-write each)
inline Pixel MakePixel(unsigned colour, bool opaque, bool border = false, bool below = false, bool textMode = false)
{
    Pixel p;
    p.colour = colour;
    p.opaque = opaque;
    p.border = border;
    p.below = below;
    p.textMode = textMode;
    return p;
}

/// One grid line of the sink the renderer writes (kWidth entries each)
struct LineSink
{
    Pixel* plane[kPlanes] = {};
    uint8_t* ids = nullptr;
    uint8_t* info = nullptr;  ///< the sprite number (0-127) of the sprite pixel at this position, 0 where the sprites plane is not opaque
};

/// The NextREGs the layer renderers read, stored with every segment of a line (a few bytes per segment, not per pixel: the pixel
/// inspector recomputes where a pixel came from with the state its line segment was drawn with)
constexpr uint8_t kKeyRegs[] = {0x12, 0x14, 0x15, 0x16, 0x17, 0x26, 0x27, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x4A, 0x4B, 0x4C, 0x68,
                                0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71};
constexpr unsigned kKeyRegCount = sizeof(kKeyRegs);

/// The register state one stretch [fromX, toX) of a grid line (sub-pixels) was drawn with. A line without copper writes is one segment
struct Segment
{
    uint16_t fromX = 0;
    uint16_t toX = 0;
    uint8_t border = 0;  ///< port #FE bits 2:0
    uint8_t portFf = 0;  ///< the Timex port
    uint8_t flags = 0;   ///< kShadowScreen | kLayer2Enable | kFlash
    uint8_t paletteControl = 0;  ///< NR #43
    uint8_t ulaNextFormat = 0;   ///< NR #42
    uint8_t clip[4][4] = {};     ///< the four clip windows (Layer 2, sprites, ULA, tilemap) x1 x2 y1 y2
    uint8_t nr[kKeyRegCount] = {};

    static constexpr uint8_t kShadowScreen = 1, kLayer2Enable = 2, kFlash = 4;
    /// The stored value of NextREG `reg` if it is one of kKeyRegs, else 0
    uint8_t Reg(uint8_t reg) const
    {
        for (unsigned i = 0; i < kKeyRegCount; i++)
            if (kKeyRegs[i] == reg)
                return nr[i];
        return 0;
    }
};

struct Frame
{
    ZeroInitBuffer<Pixel> plane[kPlanes];  ///< kWidth * kHeight each
    ZeroInitBuffer<uint8_t> ids;
    ZeroInitBuffer<uint8_t> info;
    std::vector<Segment> segments;      ///< in drawing order: line by line, left to right
    uint16_t lineFirst[kHeight] = {};   ///< index of a line's first segment
    uint16_t lineSegments[kHeight] = {};  ///< and their count
    uint64_t sequence = 0;    ///< the n-th frame captured since the capture was switched on (1 = first)
    uint64_t frameCounter = 0;  ///< the emulator's frame counter when it completed

    void Allocate()
    {
        for (auto& p : plane)
            p.resize(static_cast<size_t>(kWidth) * kHeight);
        ids.resize(static_cast<size_t>(kWidth) * kHeight);
        info.resize(static_cast<size_t>(kWidth) * kHeight);
        segments.reserve(2 * kHeight);
    }
    /// A new frame is about to be drawn into this set
    void BeginFrame()
    {
        segments.clear();
        for (unsigned y = 0; y < kHeight; y++)
            lineFirst[y] = lineSegments[y] = 0;
    }
    /// The segment of line y that holds sub-pixel x (null before the line was drawn)
    const Segment* SegmentAt(unsigned y, unsigned x) const
    {
        if (y >= kHeight)
            return nullptr;
        for (unsigned i = 0; i < lineSegments[y]; i++)
        {
            const Segment& s = segments[lineFirst[y] + i];
            if (x >= s.fromX && x < s.toX)
                return &s;
        }
        return nullptr;
    }
    bool Allocated() const { return !ids.empty(); }
    static size_t Bytes() { return static_cast<size_t>(kWidth) * kHeight * (kPlanes * sizeof(Pixel) + 2); }

    LineSink Line(unsigned y)
    {
        LineSink sink;
        const size_t at = static_cast<size_t>(y) * kWidth;
        for (unsigned p = 0; p < kPlanes; p++)
            sink.plane[p] = plane[p].data() + at;
        sink.ids = ids.data() + at;
        sink.info = info.data() + at;
        return sink;
    }
};
}  // namespace NextLayer
