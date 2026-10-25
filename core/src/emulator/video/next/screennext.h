#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "emulator/video/next/nextlayerframe.h"
#include "emulator/video/next/nextvideorenderer.h"
#include "emulator/video/screen.h"

class PortDecoder_Next;
class NextCopper;

/// The ZX Spectrum Next picture (design-video-timing.md): one mode, M_NEXT, one 640 x 256 framebuffer drawn line by
/// line by NextVideoRenderer (every line twice: the 640 x 512 frame has the 4:3 picture of the 320 x 256 grid) from the register and memory state of the moment the beam leaves the line. The beam
/// timing - contention, the interrupt, the frame length - is the ZX family of NR #03 (Screen::GetTimingDescriptor), so
/// the 256-line visible window starts 32 lines above the paper.
///
/// Per-line, not per-pixel: a change inside a line shows from the next one (the copper and line effects of N7 act on
/// line boundaries anyway). Border stripes of a loader are the border colour at the moment of each line's end.
class ScreenNext : public Screen
{
public:
    ScreenNext() = delete;
    explicit ScreenNext(EmulatorContext* context);
    ~ScreenNext() override = default;

    /// region <Screen>
    void CreateTables() override {}
    void DetectRaster() override;
    void InitFrame() override;
    void SetVideoMode(VideoModeEnum mode) override;
    void UpdateScreen() override;
    void DrawRange(uint32_t fromTstate, uint32_t toTstate) override;
    void RenderFrameBatch() override;
    void RenderOnlyMainScreen() override;
    void FillBorderWithColor(uint8_t color) override;
    /// endregion

    /// Copper time (28 MHz clocks from the first paper line) of a T-state of the frame
    uint64_t CopperClock(uint32_t tstate) const;
    /// The beam now as copper time
    uint64_t CopperClockNow() { return CopperClock(GetCurrentTstate()); }

    /// The board's copper takes its clock, its geometry and its picture hook from the raster (the machine owns the copper and runs it;
    /// the picture only follows: before a MOVE writes, the pixels the old state made are drawn)
    void BindCopper(NextCopper& copper);
    /// Draw every pixel before copper time `clock` (a MOVE is about to write at it): the lines before its line, and its line up to its pixel
    void RenderBeforeCopperClock(uint64_t clock, uint8_t reg, uint8_t value);
    /// How a copper MOVE of a register reaches the output (measured on the simulated RTL, tools/poc/028-next-video-pipeline-alignment): `clocks`
    /// 28 MHz clocks from the copper seeing a WAIT true to the first changed pixel; `quantum` the group the register is read for (4: a
    /// pixel, 32: an 8-pixel cell), the effect starting at the next group boundary. 0 clocks: not measured, drawn at the write
    struct PictureLatency
    {
        unsigned clocks;
        unsigned quantum;
    };
    /// For a MOVE of `value` over `previous` in `reg`: the changed bits decide (the earliest of them when they differ). `paletteWriteSelect`
    /// is NR #43 bits 6:4, the palette NR #41 / #44 write to (each palette's lookup is a different stage)
    static PictureLatency PictureLatencyOf(uint8_t reg, uint8_t previous, uint8_t value, uint8_t paletteWriteSelect = 0);

    /// A write to the sprite engine's fill inputs (attributes, patterns, NR #34-#39 / #75-#79, NR #4B) is about to be applied at `tstate` (the
    /// frame's T-state; negative: now): the rows the engine has filled by then keep the state they saw (POC 028: it fills paper row R
    /// during row R - 1 from about hc -21, in index order, one clock a sprite and 17 more for a drawn one)
    void SpriteStateWillChange(int64_t tstate = -1);

    /// The T-state (base, from the frame start) at which visible line `y` (0-255) is complete
    uint32_t LineEndT(unsigned y) const;
    uint32_t GridFirstT() const;  ///< the T-state where the grid's line 0 starts

    /// region <Layer capture> (design-layer-capture.md)
    /// The per-layer record of the frame being drawn. Off: nothing is allocated and the renderer is called as before. Switching it
    /// on or off takes effect at the next frame start (InitFrame), on the emulator thread; callable from any thread.
    void SetLayerCapture(bool enabled);
    bool LayerCaptureEnabled() const { return _layerWanted.load(); }
    /// The last completed frame (null before the first one). The set is never written again while the caller holds it.
    std::shared_ptr<const NextLayer::Frame> LastLayerFrame() const;
    /// Frames completed since the capture was switched on
    uint64_t LayerFramesCaptured() const { return _layerFrames.load(); }
    /// Memory held by the two sets
    size_t LayerCaptureBytes() const { return _layerBytes.load(); }
    /// endregion

private:
    PortDecoder_Next* Decoder() const;
    void PrepareLayerFrame();
    void CompleteLayerFrame();
    void RenderLinesUpTo(unsigned lineExclusive);
    /// Draw the pixels before (line, x): the lines before `line` whole, `line` up to x
    void RenderUpTo(unsigned line, unsigned halfPixel);  // the line's 14 MHz pixels of the image (640 a line) before `halfPixel`
    void UpdateCopperGeometry();
    mutable uint64_t _copperB = 0, _copperFrame7 = 0;                    ///< CopperClock = ((2 t + B) mod frame7) * 4, cached per raster
    mutable uint32_t _copperKey[4] = {};
    bool FramebufferReady() const;

    mutable PortDecoder_Next* _decoder = nullptr;
    // the sprite engine's fill: what each row saw (shared snapshots of the sprite state, one per epoch of writes)
    std::shared_ptr<NextSprites> _fillSnap;
    uint32_t _fillSnapEpoch = 0;
    std::shared_ptr<NextSprites> _rowFill[NextVideoRenderer::kHeight];
    uint8_t _rowTransparent[NextVideoRenderer::kHeight] = {};
    unsigned _fillNext = 0;                ///< the next row to fill
    int64_t _fillNextEnd = 0;              ///< when its fill ends (valid for _fillNextEndEpoch / _fillNextEndRow)
    uint32_t _fillNextEndEpoch = ~0u;
    unsigned _fillNextEndRow = ~0u;
    int64_t SpriteFillEnd(unsigned y);
    void FillSpriteRow(unsigned y);
    unsigned _nextLine = 0;
    unsigned _nextX = 0;  ///< the pixels of _nextLine already drawn (a MOVE of the copper drew up to its pixel)
    uint8_t _lastTimingClass = 2;
    uint8_t _lastSixty = 0;
    bool _copperBound = false;

    std::atomic<bool> _layerWanted{false};
    bool _layerRecording = false;                 ///< this frame is being recorded (decided at the frame start)
    std::shared_ptr<NextLayer::Frame> _layerDraw; ///< the set the renderer writes (only the emulator thread touches it)
    std::shared_ptr<NextLayer::Frame> _layerLast; ///< the last completed set (readers copy the pointer under _layerMutex)
    mutable std::mutex _layerMutex;
    std::atomic<uint64_t> _layerFrames{0};
    std::atomic<size_t> _layerBytes{0};
};
