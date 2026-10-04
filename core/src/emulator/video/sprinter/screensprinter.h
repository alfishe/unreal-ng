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
    /// The frame start: also closes the beam-ordered picture's previous frame (SprinterBeamVideo::CloseFrame)
    void InitFrame() override;
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
    void TemporalInput(TemporalFrame& frame) override;
    /// endregion </Screen>

    /// Temporal effects (ZX DLSS) in the Spectrum mode: where the 352 x 288 ZX frame (256 x 192
    /// paper + 48 of border all round, as a Pentagon draws it) sits in the 736 x 288 framebuffer.
    /// The Spectrum mode is the mode table the launcher writes (SprinterPicture: Spectrum, not
    /// mixed): 32 x 24 Spectrum squares in ZX order, square (a0 + c, b0 + r) = character cell
    /// (r, c), each ZX pixel two 14 MHz pixels. Worked example: the launcher's table has cell
    /// (0, 0) at square (4, 4): paper at (48 + 64, 16 + 32) = (112, 48), the ZX frame at
    /// (112 - 96, 48 - 48) = (16, 0). False with the reason when there is no such picture
    /// (a native mode) or it does not leave the ZX border inside the frame
    bool SpectrumWindow(const SprinterVideoInputs& in, TemporalWindow& window, std::string& why) const;

    /// The frame height the raster runs with now: 320 or 312 lines
    uint16_t FrameLines() const { return _frameLines; }
    /// A TTD restore: the frame height the raster ran with at the capture (applied at a frame start, so it
    /// can lag the PLD's latch inside a frame) - config.frame and the raster zones follow it
    void RestoreFrameLines(uint16_t lines);
    /// The inputs the picture is drawn with now (tests, the debug mapper)
    SprinterVideoInputs CurrentInputs() const;

    /// A CPU (or accelerator) write is about to change video RAM: draw the beam up to the moment the byte
    /// lands, with the old contents. The callback runs at the end of the 3-T write cycle (cpu->t); the
    /// PLD stores the byte in its next write slot after /WR (VIDEO2.TDF E_WR, VCM state 2: every half T),
    /// about 1.5 T into the cycle, so the beam positions before the cycle's end - kWriteLandsBeforeEndT
    /// still read the old byte (MAME's update_now in ram_w / vram_w; ScreenZX does the same for #FE)
    void CatchUpToWrite();
    static constexpr uint32_t kWriteLandsBeforeEndT = 1;

    /// A border write (#FE, code #C2) is about to change the border color: draw the beam up to the moment the
    /// PLD latches it, with the old color. The port callback runs at IORQ (T2 of the 4-T I/O cycle, op_D3);
    /// the PLD clocks BORDER on /IOWR rising (SP2_ACEX.TDF:310-315: /IOWR = /WR or /IO, preset when /IO
    /// goes high), i.e. when /IORQ ends at T3's falling edge, 2.5 T after the callback, and the video logic
    /// samples it every half T with the attribute (VIDEO2.TDF DCOL <- BRD on LWR_COL). In the rounding of
    /// kWriteLandsBeforeEndT (a byte stored 1.5 T into its cycle lands at T 2) that is the I/O cycle's end,
    /// 3 T after the callback: drawn at the callback, the border ran 3 T (6 ZX pixels) ahead of the paper
    /// of a Pentagon-timed program (Across the Edge in P128 mode).
    /// The constant is 4, one T more than the PLD sources give: the Sprinter's first paper pixel comes 2 T
    /// later after its INT than the Pentagon's (17 990 vs 17 988 T), so with the PLD's 3 T a Pentagon-timed
    /// border split still ended 2 ZX pixels before the paper edge - visible in Across the Edge. Owner
    /// decision (2026-10-03): the picture must match the PENTAGON model exactly, so the border keeps the
    /// Pentagon's position relative to the paper. Revisit with a capture from a real board
    void CatchUpToBorderLatch();
    static constexpr uint32_t kBorderLatchAfterIorqT = 4;

private:
    /// Draw [_prevTstate, end) - every beam position before `end` - with the state of now
    void DrawTo(uint32_t end);
    /// Before a write lands at base T `t`: if the beam is inside a text / Spectrum square there, keep the
    /// font byte the video logic latched at the square's start (SprinterVideoInputs::FontLatch)
    void LatchFont(uint32_t t);
    PortDecoder_Sprinter* Decoder() const;
    /// The framebuffer is the 736 x 288 Sprinter raster
    bool FramebufferReady() const;
    /// ZX DLSS plane B while it is on (the framebuffer's size), else null
    uint16_t* PlaneB();
    /// Apply the PLD's frame height (codes #2C / #2D) at a frame start
    void ApplyFrameLines();
    /// The raster zones of the visible-first Sprinter raster (RasterState)
    void SetRasterZones();

    mutable PortDecoder_Sprinter* _decoder = nullptr;
    mutable SprinterVideoView _view;
    uint16_t _frameLines = 320;
    SprinterVideoInputs::FontLatch _fontLatch;
    uint64_t _fontLatchFrame = 0;  ///< the frame _fontLatch belongs to (frame_counter)
    uint32_t _fontLatchCheckedT = ~0u;  ///< the last moment LatchFont looked at, and its frame
    uint64_t _fontLatchCheckedFrame = 0;
    std::vector<uint16_t> _zxPlaneB;  // the ZX frame's plane B for the temporal effect (only while it runs)
};
