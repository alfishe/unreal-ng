#pragma once

#include "platform/nativepresenter.h"

/// The native presenter on macOS (nativepresenter.h): the GL renderer draws each picture into one of a few IOSurfaces,
/// and this class shows it on the window's CAMetalLayer with presentDrawable:atTime: at the frame's audio time.
///
/// Every present gets a presented handler: MTLDrawable.presentedTime (CACurrentMediaTime = mach_absolute_time, the
/// steady clock) against the time asked for gives the on-screen error, 0 = the system dropped the frame.
///
/// The handlers run on Metal's threads and touch only the statistics, which they share with the presenter: a handler
/// that comes after the presenter is gone finds them alive. The window's occlusion and its screen's refresh range are
/// read on the GUI thread (AppKit) and cached, so the getters answer from any thread.
class MetalPresenter final : public NativePresenter
{
public:
    /// `nsView`: the NSView of a QWindow with QSurface::MetalSurface. nullptr when it has no CAMetalLayer or Metal is
    /// unavailable
    static std::unique_ptr<MetalPresenter> Create(void* nsView);
    ~MetalPresenter() override;

    unsigned BeginFrame(int width, int height) override;
    void PresentFrame(int64_t atNs, const TraceInfo& trace) override;
    /// NSWindowOcclusionStateVisible (cached)
    bool IsVisible() const override;
    /// The window server composes a refresh ahead, so a drawable needs about a refresh before its vsync, and the
    /// draw (a GL fence wait up to 10 ms on a busy GPU) comes on top. Frames latch some 60 ms before their target, so
    /// a long lead costs nothing (at 60 Hz under load, frames a refresh late: lead 6 ms 65 %, 18 ms 15 %, 23 ms 2 %):
    /// a refresh plus 6 ms to start with, adapted to the load: each present shown a refresh late adds 1 ms (up to a
    /// refresh plus 20 ms), 600 on time in a row take 1 ms back (down to a refresh plus 4 ms)
    int64_t LeadNs(int64_t refreshPeriodNs) const override;
    /// auto: a display with a refresh range (ProMotion, adaptive sync) shows each present at its time, a fixed one at
    /// the first refresh after it; vrr: no grid, the frames at their times; vsync: the grid, a range at its top rate
    void SetPolicy(PresentPolicy policy) override;
    /// The last present's presentedTime and the screen's refresh interval; false while each present shows at its time
    bool RefreshGrid(int64_t& refreshNs, int64_t& periodNs) const override;
    /// A display link on the view (macOS 14+, NSView displayLinkWithTarget:selector:, in the main run loop's common
    /// modes, so it ticks through live resizes and menu tracking): on a fixed grid each tick hands the handler the
    /// link's target timestamp, the refresh the present made now reaches. Not on macOS 12-13, nor with
    /// UNREAL_PRESENT_DISPLAY_LINK=0 (the window's timer paces then, as under VRR)
    bool SetRefreshHandler(RefreshHandler handler, void* context) override;
    void WantRefreshes(bool want) override;
    bool PacesRefreshes() const override;
    uint64_t Presented() const override;
    uint64_t LastLatePresent() const override;
    uint32_t LastLateLag() const override;
    uint64_t LastOnTimePresent() const override;
    Stats GetStats() const override;
    std::string Describe() const override;

    /// Defined in metalpresenter.mm only; named here for the display link's Objective-C target there
    struct Impl;

private:
    MetalPresenter();
    bool Vrr() const;  // each present at its time: the policy, or (auto) the display's refresh range
    std::unique_ptr<Impl> _impl;
};
