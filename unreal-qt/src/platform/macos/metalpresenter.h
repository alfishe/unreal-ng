#pragma once

#include <cstdint>
#include <memory>
#include <string>

/// The native presenter on macOS (frame presenter design, docs/inprogress/2026-10-26-frame-presenter, sections 4.1 and
/// 6.2): the GL renderer draws each picture into one of a few IOSurfaces, and this class shows it on the window's
/// CAMetalLayer with presentDrawable:atTime: at the frame's audio time. On a VRR display every frame is on screen for
/// its own period; on a fixed one the system shows it at the first refresh after its time.
///
/// Every present gets a presented handler: MTLDrawable.presentedTime (CACurrentMediaTime = mach_absolute_time, the
/// steady clock) against the time asked for gives the on-screen error, 0 = the system dropped the frame.
///
/// All calls on the thread that owns the GL context (the GUI thread); the handlers run on Metal's threads and only
/// touch the statistics.
class MetalPresenter
{
public:
    /// On-screen timing over the last presents, ms
    struct Stats
    {
        uint64_t presented = 0;  ///< presents since the start
        uint64_t shown = 0;      ///< reached the screen
        uint64_t dropped = 0;    ///< presentedTime 0: never on screen
        uint32_t window = 0;     ///< presents the percentiles cover
        double errorP50 = 0.0;   ///< screen time minus the time asked for
        double errorP99 = 0.0;
        double errorMax = 0.0;
        double gpuWaitP50 = 0.0;  ///< GL fence wait before the Metal pass
        double gpuWaitMax = 0.0;
        double drawableWaitP50 = 0.0;  ///< nextDrawable: all drawables still queued or on screen
        double drawableWaitMax = 0.0;
        double minRefreshHz = 0.0;  ///< the display's range (min = max: fixed)
        double maxRefreshHz = 0.0;
        bool visible = true;  ///< some of the window is on screen (an occluded window's presents are dropped)
    };

    /// `nsView`: the NSView of a QWindow with QSurface::MetalSurface. nullptr when it has no CAMetalLayer or Metal is
    /// unavailable
    static std::unique_ptr<MetalPresenter> Create(void* nsView);
    ~MetalPresenter();

    /// The GL context current: an IOSurface-backed framebuffer of `width` x `height` device pixels to draw the next
    /// picture into, bound as GL_FRAMEBUFFER. @return its name, 0 on failure
    unsigned BeginFrame(int width, int height);
    /// Where a present came from, for UNREAL_PRESENT_TRACE only (steady clock, ns; 0 = unknown)
    struct TraceInfo
    {
        uint64_t serial = 0;
        int64_t latchNs = 0;    ///< the frame was latched
        int64_t plannedNs = 0;  ///< the wake the presenter planned
        int64_t wokeNs = 0;     ///< the wake that drew it
    };
    /// The picture drawn: shown at `atNs` (steady clock; at once when it is past). GL rows count from the bottom, the
    /// pass turns them over
    void PresentFrame(int64_t atNs, const TraceInfo& trace);

    /// Some of the window is on screen: NSWindowOcclusionStateVisible. Occluded, every present would be dropped
    bool IsVisible() const;

    Stats GetStats() const;
    /// One line for the log
    std::string Describe() const;

private:
    MetalPresenter();
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
