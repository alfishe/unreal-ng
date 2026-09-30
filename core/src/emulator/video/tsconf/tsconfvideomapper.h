#pragma once

#include "emulator/video/map/videomapper.h"

struct TsConfState;
struct TsConfLine;
struct EmulatorState;
class TsConfEngine;

/// What the TS-Conf mapper reads (Screen::VideoFamilyView of ScreenTSConf):
/// the register file and CRAM, and the engine's line table - the registers
/// each displayed line was latched with
struct TsConfVideoView
{
    const TsConfState* ts = nullptr;
    const TsConfEngine* engine = nullptr;
    const EmulatorState* state = nullptr;  ///< the frame counter (ZX flash)
    const uint8_t* ram = nullptr;          ///< the 4 MB RAM (PixelsFor: which character a text cell holds)
};

/// TS-Conf video debug mapping (PLAN #42 phase 5, design §5 "TSConf"): the
/// graphics layer of every mode as ScreenTSConf draws it, one layer per mode:
///   tszx:  the ZX layout at V_PAGE, rows wrapping at 256 and columns at 32 bytes;
///   ts16:  4 bpp at (V_PAGE & #F8) << 14 + y * 256 + x / 2, 512 x 512 scrolled;
///   ts256: 8 bpp at (V_PAGE & #F0) << 14 + y * 512 + x;
///   tstx:  256-byte rows (128 characters, 128 attributes) at V_PAGE, font at V_PAGE ^ 1;
/// each through CRAM (Space::Palette, 16-bit cells, offset = index * 2).
///
/// Surfaces are in 14 MHz pixels, as the 720x288 framebuffer: a graphics dot
/// is 2 surface pixels (dotsPerT 4 for every mode), a TXT pixel 1; so surface
/// pixel (x, y) is framebuffer pixel (window left + x, window top + y). The
/// window is the V_CONFIG geometry (256x192 / 320x200 / 320x240 / 360x288).
/// Per line the mapper uses the registers the engine latched for it (mode,
/// V_PAGE, X offset, row counter, PAL_SEL), so a mid-frame split is answered
/// line by line.
///
/// Layer 1, "tsu" (present while T_CONFIG enables tiles or sprites): the TSU
/// picture over the TS window (the graphics window, or all 360x288 with
/// T_CONFIG[0]), in the same 14 MHz pixels. A pixel's answer names the object
/// that drew it - `contribution.layer` "tsu.s0" / "tsu.t0" / "tsu.s1" /
/// "tsu.t1" / "tsu.s2" - with the tilemap word (TileDescriptor, 2 bytes) and
/// the graphics byte (TileGraphic) of a tile, or the three SFILE words
/// (SpriteDescriptor, Space::SpriteRam, offset = word * 2) and the graphics
/// byte (SpriteGraphic) of a sprite, then the CRAM cell; transparent pixels
/// answer nothing. The line is drawn again for the answer
/// (TsConfEngine::ProbeTsuLine): TSU registers and SFILE as they are now.
///
/// What the screen shows (the video plex, ScreenTSConf): inside the graphics
/// window the TSU pixel unless NOTSU (V_CONFIG[4]) or GFXOVR (V_CONFIG[3])
/// with a "visible" graphics dot (ZX ink, 16C / 256C index != 0, TXT font
/// bit); outside it the TSU pixel, else BORDER. In TXT every source is
/// flattened to 4 bits in the PAL_SEL bank.
///
/// Worked example (16C 320x200, V_PAGE #10, no offsets, PAL_SEL 0): surface
/// pixel (6, 0) is dot 3 of line 0: RAM byte #40001 = page #10 offset 1, low
/// nibble (bit mask #0F); colour index = that nibble; CRAM cell index * 2.
class TsConfVideoMapper final : public videomap::IVideoMapper
{
public:
    const char* Family() const override { return "tsconf"; }
    videomap::VideoLayout Layout(const videomap::VideoState& s) const override;
    bool SourcesAt(const videomap::VideoState& s, const videomap::MemView& m, size_t layerIndex, uint32_t x, uint32_t y,
                   videomap::LayerContribution& out) const override;
    void BorderSources(const videomap::VideoState& s, videomap::LayerContribution& out) const override;
    void PixelsFor(const videomap::VideoState& s, const videomap::SourceRef& ref,
                   std::vector<videomap::SurfaceArea>& out) const override;
    bool TextAt(const videomap::VideoState& s, const videomap::MemView& m, size_t layerIndex, uint32_t col, uint32_t row,
                videomap::TextCell& out) const override;

    static const char* LayerId(uint8_t vConfig);
    /// The TSU layer is present (T_CONFIG enables tiles or sprites)
    static bool HasTsu(const TsConfState& ts);

private:
    /// The colour index of surface x of a line drawn with `set` (graphics layer)
    static uint8_t GraphicsIndex(const TsConfVideoView& v, const TsConfLine& set, uint32_t x);
    /// The TSU pixels a tilemap word, graphics byte, SFILE word or CRAM cell feeds
    static void TsuPixelsFor(const TsConfVideoView& v, const videomap::SourceRef& ref, std::vector<videomap::SurfaceArea>& out);
    /// The line set of window row y, null when the view is missing
    static const TsConfLine* LineOf(const TsConfVideoView& v, uint8_t vConfig, uint32_t y);
};
