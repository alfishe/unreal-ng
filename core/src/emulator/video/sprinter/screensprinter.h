#pragma once

#include "emulator/video/screen.h"
#include "emulator/video/sprinter/sprintervideorenderer.h"

class PortDecoder_Sprinter;
class SprinterVideoRam;
struct SprinterPldState;

/// What the Sprinter video debug mapper reads (Screen::VideoFamilyView of
/// ScreenSprinter): the video RAM and palettes, and the inputs the picture is
/// drawn with now (mode page, HOLD, border, flash, the #7FFD font bit, height)
struct SprinterVideoView
{
    const SprinterVideoRam* vram = nullptr;
    SprinterVideoInputs inputs;
    uint8_t hold = 0x77;  ///< the HOLD register (code #CB) the offsets come from
};

/// Sprinter Sp2000 video (Sprinter tdd-video §1-§3, phase S2).
///
/// One renderer for every mode: the mode table in video RAM decides per 8x8
/// square (text 40 / 80 columns, Spectrum screens, graphics 320 x 256 colors
/// and 640 x 16 colors, border and blank squares), so there is one video mode,
/// M_SPRINTER, and one 736x288 framebuffer (the R_736_288 raster: 640x256 plus
/// MAME's 48 / 16 pixels of border).
///
/// Raster: 896 pixels x 320 or 312 lines, 4 pixels per base T-state (14 MHz);
/// the CPU's frame T-state divided by the clock ratio (GetCurrentTstate) is
/// the beam: line = t / 224, x = (t mod 224) x 4. Line 0 / T 0 is the first
/// visible pixel (MAME's frame origin: the INT list of SprinterIntSource is in
/// the same coordinates), so framebuffer pixel (x, y) is drawn at frame T
/// y x 224 + x / 4; lines 288.. and pixels 736.. are blanking.
///
/// Beam-accurate by catch-up (tdd-video §3): the range renderer draws the
/// pixels the beam passed since the last call; a change that alters the
/// picture - a video RAM byte, RGMOD, HOLD, the border - first draws up to
/// its moment with the old state (MAME update_now), so a palette or mode
/// change shows from where the beam was.
///
/// Frame height (codes #2C / #2D): the PLD state's frameLines is applied at
/// the next frame start - config.frame becomes 71 680 or 69 888 T and the
/// raster state follows; the CPU's frame (Z80::BeginFrame) picks it up after.
///
/// The picture itself comes from the active PLD configuration module
/// (SprinterPldConfiguration::VideoRenderer, hook 3): the Standard module's
/// SprinterVideoRenderer unless a module brings its own.
class ScreenSprinter : public Screen
{
public:
    static constexpr uint32_t kLineTStates = 224;
    static constexpr uint32_t kVisibleTStates = SprinterVideoRenderer::kVisibleWidth / 4;  // 184
    static constexpr uint32_t kVisibleLines = SprinterVideoRenderer::kVisibleLines;       // 288

    ScreenSprinter() = delete;
    explicit ScreenSprinter(EmulatorContext* context);
    ~ScreenSprinter() override = default;

    /// region <Screen>
    void CreateTables() override {}
    void InitRaster() override;
    void SetVideoMode(VideoModeEnum mode) override;
    void UpdateScreen() override;
    void DrawRange(uint32_t fromTstate, uint32_t toTstate) override;
    void RenderFrameBatch() override;
    void RenderOnlyMainScreen() override;
    void FillBorderWithColor(uint8_t color) override;
    ScreenState DescribeScreenState() const override;
    BeamPosition DescribeBeam(uint32_t tInFrame) const override;
    const void* VideoFamilyView() const override;
    void CaptureFamilyLatches(videomap::VideoLatches& latches) const override;
    bool DigestSurface(ScreenDigestSurface& out) const override;
    bool IndexedFrame(std::vector<uint16_t>& pens, uint16_t& width, uint16_t& height, std::string& encoding) const override;
    /// endregion </Screen>

    /// The frame height the raster runs with now: 320 or 312 lines
    uint16_t FrameLines() const { return _frameLines; }
    /// A TTD restore: the frame height the raster ran with at the capture (applied at a frame start, so it
    /// can lag the PLD's latch inside a frame) - config.frame and the raster zones follow it
    void RestoreFrameLines(uint16_t lines);
    /// The inputs the picture is drawn with now (tests, the debug mapper)
    SprinterVideoInputs CurrentInputs() const;

private:
    PortDecoder_Sprinter* Decoder() const;
    /// Apply the PLD's frame height (codes #2C / #2D) at a frame start
    void ApplyFrameLines();
    /// The raster zones of the visible-first Sprinter raster (RasterState)
    void SetRasterZones();

    mutable PortDecoder_Sprinter* _decoder = nullptr;
    mutable SprinterVideoView _view;
    uint16_t _frameLines = 320;
};
