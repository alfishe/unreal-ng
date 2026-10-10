#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "emulator/video/next/nextlayerframe.h"
#include "emulator/video/next/nextvideorenderer.h"
#include "emulator/video/screen.h"

class PortDecoder_Next;

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
    void InitRaster() override;
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

    /// The T-state (base, from the frame start) at which visible line `y` (0-255) is complete
    uint32_t LineEndT(unsigned y) const;

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
    static void RecordSegment(NextLayer::Frame& frame, unsigned y, unsigned fromX, unsigned toX, const NextVideoInputs& in, const NextVideoRegs& video);
    void RenderLinesUpTo(unsigned lineExclusive);
    bool FramebufferReady() const;

    mutable PortDecoder_Next* _decoder = nullptr;
    unsigned _nextLine = 0;
    uint8_t _lastTimingClass = 2;
    bool _copperBound = false;

    std::atomic<bool> _layerWanted{false};
    bool _layerRecording = false;                 ///< this frame is being recorded (decided at the frame start)
    std::shared_ptr<NextLayer::Frame> _layerDraw; ///< the set the renderer writes (only the emulator thread touches it)
    std::shared_ptr<NextLayer::Frame> _layerLast; ///< the last completed set (readers copy the pointer under _layerMutex)
    mutable std::mutex _layerMutex;
    std::atomic<uint64_t> _layerFrames{0};
    std::atomic<size_t> _layerBytes{0};
};
