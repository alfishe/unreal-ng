#pragma once

#include "emulator/video/tsconf/tsconfvideomapper.h"
#include "emulator/video/zx/screenzx.h"

struct TsConfState;
class PortDecoder_TSConf;
struct TsConfLine;
class TsConfEngine;

/// TS-Conf video (TSConf technical-design §3.9, hardware-spec §4).
///
/// Every TS mode renders into one 720x288 framebuffer: the 360x288 visible
/// dots of the 448-dot x 320-line raster at 2 px per dot (TXT pixels are
/// 14 MHz, one framebuffer pixel each). Beam-accurate per T-state: the range
/// renderer draws the dots the beam passed since the last CPU step with the
/// registers of that moment.
///
/// Raster (hs §4.1): frame T-state t -> raster tact (t / multiplier, done by
/// GetCurrentTstate), line = tact / 224, dot = (tact % 224) * 2; lines 0-31 and
/// dots 0-87 are blanking, so framebuffer (x, y) = ((dot - 88) * 2, line - 32).
///
/// Built (phase 3 + TXT from phase 4): ZX (M_TSZX), 16C, 256C and TXT graphics
/// in the V_CONFIG geometry window with the X offset and the graphics row
/// counter, BORDER outside it, CRAM colors through the no-VDAC PWM curve,
/// flash. The line-latched registers and the row counter come from the
/// engine's line table (TsConfEngine), so the picture follows the hardware's
/// line-start latching; BORDER and CRAM act at the dot. The TSU pixels come
/// from the engine's per-line buffers and are mixed as the video plex does
/// (NOTSU / NOGFX / GFXOVR, TS window). Not yet: the VDAC curves.
///
/// Speed (TS-O1..O3): a range is drawn as one span per raster line - the
/// graphics of the window into an index buffer (one loop per mode), the TSU
/// mixed only over the TS window of lines it drew on, then CRAM colors from
/// a palette rebuilt only when CRAM changed.
class ScreenTSConf : public ScreenZX
{
public:
    static constexpr uint32_t kLineTacts = 224;
    static constexpr uint32_t kLines = 320;
    static constexpr uint32_t kFirstVisibleLine = 32;
    static constexpr uint32_t kFirstVisibleTact = 44;  // dot 88
    static constexpr uint32_t kVisibleDots = 360;
    static constexpr uint32_t kVisibleLines = 288;

    ScreenTSConf() = delete;
    explicit ScreenTSConf(EmulatorContext* context);
    ~ScreenTSConf() override = default;

    /// Mode and window from TsConfState (V_CONFIG)
    void InitRaster() override;
    /// The graphics window (V_CONFIG geometry) in frame pixels: a TS frame stores 2 px per raster dot
    /// and one pixel per raster line, so the 256x192 window is 512x192 pixels at (104, 48)
    PictureRect WorkingWindow() const override;
    /// The TS mode with its geometry ("TS16 320x200"), its pixel format and
    /// the RAM pages it reads (V_PAGE based)
    ScreenState DescribeScreenState() const override;
    /// Draw the beam's dots up to frame tact `raster` (not beyond what the CPU
    /// reached): a DMA CRAM write draws what came before it first (TIM-5)
    void DrawTo(uint32_t raster);
    /// The TS state the video debug mapper reads (TsConfVideoMapper)
    const void* VideoFamilyView() const override;
    void SetVideoMode(VideoModeEnum mode) override;
    void DrawRange(uint32_t fromTstate, uint32_t toTstate) override;
    void SetBorderColor(uint8_t color) override;
    void SetActiveScreen(SpectrumScreenEnum screen) override;
    void RenderFrameBatch() override;
    void RenderOnlyMainScreen() override;
    void FillBorderWithColor(uint8_t color) override;

    /// Framebuffer RGBA (0xAABBGGRR) of a CRAM word (hs §4.3). `vdac` = the
    /// build's STATUS VDAC_VER ([MISC] TS_VDAC): 0 = no VDAC (2-bit DAC + PWM,
    /// time-averaged); 3 = the 5-bit VDAC board and 7 = the VDAC2 card, whose
    /// CPLDs share one table (Vdac2Level); 1 / 2 = a 3 / 4-bit video DAC (no
    /// board known): CRAM bit 15 set = the DAC bits scaled to full, clear = the
    /// PWM-compatible linear curve (levels 0..24, then full)
    static uint32_t CramToRgba(uint16_t cram, uint8_t vdac = 0);
    /// One 5-bit channel through the VDAC2 card's CPLD: direct (PAL_SEL = 1)
    /// = level << 3, otherwise the card's linear table (vdac2-tdd.md D6)
    static uint32_t Vdac2Level(uint32_t level, bool direct);
    /// Video mode of a V_CONFIG value
    static VideoModeEnum ModeOf(uint8_t vConfig);

    /// G_X_OFFS in ZX and TXT mode is not a pixel scroll: it loads the DRAM column counter (cstart = G_X_OFFS >> 2,
    /// [V] video_mode.v x_offs_mode) and the counter picks what each fetch reads, so a window pixel can show
    /// attribute bits, colors from pixel bytes, or a raw character code (found with tools/machines/tsconf/rtl-sim,
    /// test GX1). These say where one pixel comes from; the renderer and the video mapper both use them.
    struct ZxSource
    {
        uint8_t pixelColumn;   ///< byte column (0..31) of the pixel bits
        uint8_t colorColumn;   ///< byte column of the colors
        bool pixelFromAttr;    ///< the pixel bits are an attribute byte
        bool colorFromPixels;  ///< the colors are a pixel byte
        uint8_t bit;           ///< bit (7 = left) of the pixel byte
    };
    /// Window pixel `pixel` (0..255) of a ZX line
    static ZxSource ZxSourceOf(uint16_t gxOffs, uint32_t pixel);
    struct TxtSource
    {
        uint8_t codeColumn;  ///< character column (0..127) whose code gives the bits
        uint8_t attrColumn;  ///< character column whose attribute gives the colors
        bool glyph;          ///< the bits are the code's font line (else the raw code byte)
        uint8_t bit;         ///< bit (7 = left)
    };
    /// Hires pixel `pixel` (2 per dot) of a TXT line
    static TxtSource TxtSourceOf(uint16_t gxOffs, uint32_t pixel);

private:
    /// The TS-Conf state (the port decoder owns it; null before it exists)
    const TsConfState* State();
    /// CRAM -> RGBA for every entry that changed since the last call (TS-O1)
    void RefreshPalette(const TsConfState& ts);
    /// Raster dots [dotFrom, dotTo) of raster line `line` into `out` (2 px
    /// per dot): graphics in the window, the TSU mixed as the video plex does,
    /// BORDER elsewhere (TS-O2, TS-O3)
    void DrawLineSpan(const TsConfState& ts, uint32_t line, uint32_t dotFrom, uint32_t dotTo, uint32_t* out) const;
    /// The span straight to colours when nothing mixes into it (no TSU pixels
    /// on the line or NOTSU, no GFXOVR): the common case, one pass
    /// @return false when the span needs the mixing path (DrawLineSpan)
    bool DirectSpan(const TsConfState& ts, const TsConfLine& set, uint32_t line, uint32_t dotFrom, uint32_t dotTo,
                    uint32_t* out) const;
    /// Graphics color indices of `count` dots from window x `wx` (dots from
    /// the window's left edge); `visible` = the dot counts as "visible" for
    /// GFXOVR (ZX ink after flash, 16C / 256C index != 0, TXT font bit).
    /// `index1` / `visible1`: the second TXT half dot (TXT only)
    void GraphicsSpan(const TsConfLine& set, uint32_t wx, uint32_t count, uint8_t* index0, uint8_t* index1,
                      uint8_t* visible0, uint8_t* visible1) const;

    const TsConfState* _ts = nullptr;
    const TsConfEngine* _engine = nullptr;
    mutable TsConfVideoView _view;
    uint32_t _palette[256] = {};      ///< RGBA of each CRAM entry
    uint16_t _paletteCram[256] = {};  ///< the CRAM words _palette was built from
    uint8_t _paletteVdac = 0;         ///< the DAC _palette was built for
    uint32_t _paletteVersion = 0;     ///< the decoder's CRAM version _palette was built at
    bool _paletteValid = false;
    bool _paletteVerify = false;      ///< compare every cell (whole-frame renders: direct CRAM writes)
    const PortDecoder_TSConf* _decoder = nullptr;
};
