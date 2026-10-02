#pragma once

#include "emulator/video/map/videomapper.h"

struct SprinterVideoInputs;

/// Sprinter video debug mapping (PLAN #42; Sprinter tdd-video §7): which video
/// RAM bytes make a pixel, which pixels a byte feeds - the rules of
/// SprinterVideoRenderer, read from ScreenSprinter's SprinterVideoView.
///
/// One layer, "sprinter": the whole 736x288 visible window in 14 MHz pixels
/// (the mode table covers the border too), beam line y, T x / 4 (dotsPerT 4),
/// framebuffer pixel (x, y). Sources are video RAM addresses (Space::Vram,
/// page 0, offset 0..#3FFFF), memory first, the palette last:
///   graphics: the 3 mode bytes (ModeDescriptor), the pixel byte (PixelBits,
///             mask #F0 / #0F for a 640 nibble), the pen (PaletteEntry: the
///             red byte's address, width 3 = R, G, B);
///   text:     the mode bytes (Line2 for the right half of a 640 square), the
///             font byte (FontRow, mask = the pixel's bit), the attribute
///             (CharAttr), the pen;
///   border:   the mode bytes, the #FE border (Register, page #FE), the pen;
///   blank:    the mode bytes, the pen #400.
/// RAM pages #50-#5F hold the CPU copy of video RAM (graphics writes go to
/// both, hardware-reference §6.2), so PixelsFor takes those RAM bytes too.
///
/// Worked example (the BIOS text screen, no HOLD shift): pixel (52, 20) is
/// square a = 0, b = 0, row 4, square pixel 4; with Mode0 = #10 (text 640) it
/// is the left character, font byte (Mode1 << 10) | 4, its bit #08.
class SprinterVideoMapper final : public videomap::IVideoMapper
{
public:
    static constexpr const char* kLayer = "sprinter";

    const char* Family() const override { return "sprinter"; }
    videomap::VideoLayout Layout(const videomap::VideoState& s) const override;
    bool SourcesAt(const videomap::VideoState& s, const videomap::MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                   videomap::LayerContribution& out) const override;
    void BorderSources(const videomap::VideoState& s, videomap::LayerContribution& out) const override;
    void PixelsFor(const videomap::VideoState& s, const videomap::SourceRef& ref,
                   std::vector<videomap::SurfaceArea>& out) const override;

    /// The sources and pen of visible pixel (x, y) under `in` (also the renderer's tests)
    static uint32_t Collect(const SprinterVideoInputs& in, uint32_t x, uint32_t y, std::vector<videomap::SourceRef>& sources);
};
