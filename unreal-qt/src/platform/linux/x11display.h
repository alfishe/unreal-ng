#pragma once

#ifdef __linux__

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "platform/linuxpresent.h"

class QScreen;
class QWindow;

/// X11 for the Linux presenter and the display rate switch (platform/linux/): libX11 and libXrandr loaded at run time
/// (dlopen of the runtime libraries every X11 desktop has, no headers and no link dependency), on a connection of its
/// own (XOpenDisplay), so nothing touches Qt's. Get's connection: GUI thread only (Connect: one for another thread). Without X11 (Wayland, no DISPLAY, the libraries
/// missing) Get returns nullptr.
namespace x11
{
/// The window system the application runs on (Qt's platform plugin, WAYLAND_DISPLAY)
linuxpresent::WindowSystem CurrentWindowSystem();

/// The display's VRR for `window` now: linuxpresent::DecideVrr with what the window system tells (RandR vrr_capable
/// on the output under it, the window covering its screen, NVIDIA's environment). `nvidia`: the GL vendor is NVIDIA
linuxpresent::VrrDecision DecideWindowVrr(const QWindow* window, bool nvidia);
/// `_VARIABLE_REFRESH` on the window and its top level (X11; nothing elsewhere)
void OptInVariableRefresh(QWindow* window, bool on);

/// An output's CRTC as RandR has it: where it scans out, its mode and the modes the output offers
struct OutputState
{
    std::string name;
    unsigned long output = 0;
    unsigned long crtc = 0;
    int x = 0;
    int y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t rotation = 1;
    std::vector<unsigned long> outputs;  ///< every output the CRTC drives (clones)
    linuxpresent::XMode current;
    std::vector<linuxpresent::XMode> modes;  ///< the output's own modes
};

class Display
{
public:
    /// The process's connection, opened on the first call; nullptr when X11 is not there
    static Display* Get();
    /// A connection of the caller's own (another thread), nullptr when X11 is not there
    static std::unique_ptr<Display> Connect();
    /// A connection to the X display @p name (":0": in gamescope, Steam's own), nullptr when it cannot be opened
    static std::unique_ptr<Display> ConnectTo(const std::string& name);

    /// The output under `screen` (by its name, Qt's xcb screens are RandR outputs; else the CRTC at its origin)
    bool Output(const QScreen* screen, OutputState& state, std::string& error);
    /// RandR's vrr_capable on the output: 1, 0, or -1 (no such property)
    int VrrCapable(unsigned long output);
    /// The CRTC of `state` on mode `modeId`, everything else as it is
    bool SetMode(const OutputState& state, uint64_t modeId, std::string& error);
    /// `_VARIABLE_REFRESH` on the X window (what Mesa and xf86-video-amdgpu read): 1 opts it into VRR, 0 out
    void SetVariableRefresh(unsigned long window, bool on);
    /// The root window's CARDINAL property @p name (its first value); false when it is not there
    bool RootCardinal(const char* name, uint64_t& value);

    ~Display();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    struct Api;  ///< the resolved functions (x11display.cpp)

private:
    Display() = default;
    bool Open(const char* name);  ///< nullptr: DISPLAY

    Api* _api = nullptr;
    void* _display = nullptr;
};
}  // namespace x11

#endif  // __linux__
