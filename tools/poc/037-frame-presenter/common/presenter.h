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

#ifdef __APPLE__
/// The CAMetalLayer backing an NSView (a Qt MetalSurface window's view), nullptr when it has none
void* MetalLayerOfView(void* nsView);
#endif

}  // namespace poc037
