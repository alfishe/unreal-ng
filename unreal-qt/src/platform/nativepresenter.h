#pragma once

#include <cstdint>
#include <memory>
#include <string>

/// A native presenter (frame presenter design, docs/inprogress/2026-10-26-frame-presenter, sections 4.1 and 6.2): the
/// GL renderer draws each picture into a texture shared with the platform's API, and the presenter shows it on the
/// window at the frame's audio time. On a VRR display every frame is on screen for its own period; on a fixed one it
/// shows at the first refresh after its time.
///
/// - macOS: IOSurfaces, CAMetalLayer presentDrawable:atTime: (metalpresenter.mm)
/// - Windows: WGL_NV_DX_interop2 onto D3D11 textures, a DXGI flip swap chain presented by its own thread at the time,
///   tearing allowed for VRR (dxgipresenter.cpp)
///
/// BeginFrame, PresentFrame and the destructor on the thread whose GL context draws (the GUI thread); the rest from
/// any thread.
class NativePresenter
{
public:
    /// On-screen timing over the last presents, ms
    struct Stats
    {
        uint64_t presented = 0;  ///< presents since the start
        uint64_t shown = 0;      ///< reached the screen
        uint64_t dropped = 0;    ///< never on screen (replaced, or the system dropped it)
        uint64_t late = 0;       ///< shown more than a refresh after the time asked for (a refresh late)
        double leadMs = 0.0;     ///< the lead in effect (LeadNs at the last refresh period asked)
        uint32_t window = 0;     ///< presents the percentiles cover
        double errorP50 = 0.0;   ///< screen time minus the time asked for
        double errorP99 = 0.0;
        double errorMax = 0.0;
        double gpuWaitP50 = 0.0;  ///< the wait for GL's draw before the native pass
        double gpuWaitMax = 0.0;
        double drawableWaitP50 = 0.0;  ///< the wait for a free drawable / back buffer
        double drawableWaitMax = 0.0;
        double minRefreshHz = 0.0;  ///< the display's range (min = max: fixed)
        double maxRefreshHz = 0.0;
        bool visible = true;  ///< some of the window is on screen
    };

    /// Where a present came from, for UNREAL_PRESENT_TRACE only (steady clock, ns; 0 = unknown)
    struct TraceInfo
    {
        uint64_t serial = 0;
        int64_t latchNs = 0;    ///< the frame was latched
        int64_t plannedNs = 0;  ///< the wake the window planned
        int64_t wokeNs = 0;     ///< the wake that drew it
    };

    /// The platform's presenter for a window (QWindow::winId()), the GL context that will draw current. nullptr: none
    /// here, or the platform lacks what it needs (the window then presents with GL)
    static std::unique_ptr<NativePresenter> Create(void* windowHandle);
    virtual ~NativePresenter() = default;

    /// The GL context current: a framebuffer of `width` x `height` device pixels to draw the next picture into,
    /// bound as GL_FRAMEBUFFER. @return its name, 0 on failure
    virtual unsigned BeginFrame(int width, int height) = 0;
    /// The picture drawn: shown at `atNs` (steady clock; at once when it is 0 or past). GL rows count from the bottom,
    /// the presenter turns them over
    virtual void PresentFrame(int64_t atNs, const TraceInfo& trace) = 0;

    /// Some of the window is on screen: an occluded or minimized window's presents would be dropped
    virtual bool IsVisible() const = 0;
    /// How long before a frame's target the window draws it and hands it over
    virtual int64_t LeadNs(int64_t refreshPeriodNs) const = 0;
    /// A fixed display's refreshes (steady clock, ns): a recent one and the period. Smooth motion draws a picture
    /// for each of them. false: none seen yet, or the display follows the presents (VRR: every frame at its own time)
    virtual bool RefreshGrid(int64_t& refreshNs, int64_t& periodNs) const = 0;

    virtual Stats GetStats() const = 0;
    /// One line for the log
    virtual std::string Describe() const = 0;
};
