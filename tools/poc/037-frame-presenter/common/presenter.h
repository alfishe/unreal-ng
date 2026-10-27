#pragma once

// POC 037: the native presenter on a shell's window (Qt or SDL3). It owns its thread, the refresh callback (display
// link / waitable swap chain) and the presentation feedback, so the shell only supplies the window and pumps events.

#include "lab.h"

#include <memory>
#include <string>

namespace poc037
{

class NativePresenter
{
public:
    virtual ~NativePresenter() = default;
    /// Start presenting by the options' variant; the producer is started by the presenter (ticked for d-locked)
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    /// What the presenter runs on (API, modes), for the log's meta lines
    virtual std::string Describe() const = 0;
};

/// macOS: `handle` is the window's CAMetalLayer; Windows: the HWND
std::unique_ptr<NativePresenter> CreateNativePresenter(void* handle, FrameRing& ring, Producer& producer, PresentLog& log,
                                                       const Options& options);

/// The display the window is on: nominal refresh, the variable range (min = max when fixed), for the log
struct DisplayFacts
{
    double refreshHz = 0;
    double minHz = 0;
    double maxHz = 0;
    std::string name;
    bool visible = true;  ///< macOS: NSWindowOcclusionStateVisible (an occluded window's presents are dropped)
};
DisplayFacts QueryDisplay(void* handle);

#ifdef _WIN32
/// Windows has no per-present feedback for a composed (DWM) swap chain (GetFrameStatistics is all zeros) and
/// IDXGIOutput::WaitForVBlank returns at once in a window, so the screen time comes from the display's vblanks: the
/// kernel's vblank event of the window's monitor (D3DKMTWaitForVerticalBlankEvent) on a thread, cross-checked with
/// DWM's composition clock (DwmGetCompositionTimingInfo, qpcVBlank). Stop() resolves the log by the kernel's vblanks.
class VBlankRecorder
{
public:
    explicit VBlankRecorder(void* hwnd);
    ~VBlankRecorder();
    void Stop(PresentLog& log);

private:
    void Run(void* hwnd);
    std::thread _kernel, _dwm;
    std::atomic<bool> _stop{false};
    std::vector<int64_t> _vblanks, _dwmVBlanks;
    std::string _error;
};
#endif

#ifdef __APPLE__
/// The CAMetalLayer backing an NSView (a Qt MetalSurface window's view), nullptr when it has none
void* MetalLayerOfView(void* nsView);
#endif

}  // namespace poc037
