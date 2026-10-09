#pragma once

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

    /// The T-state (base, from the frame start) at which visible line `y` (0-255) is complete
    uint32_t LineEndT(unsigned y) const;

private:
    PortDecoder_Next* Decoder() const;
    void RenderLinesUpTo(unsigned lineExclusive);
    bool FramebufferReady() const;

    mutable PortDecoder_Next* _decoder = nullptr;
    unsigned _nextLine = 0;
    uint8_t _lastTimingClass = 2;
};
