#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "emulator/video/presentpolicy.h"

/// The Linux presenter's decisions (frame presenter design, sections 4.1, 4.7 and 6.2), apart from X11 and GL so they
/// are tested on every platform: the VRR decision from what the window system tells, the swap mode a policy gives,
/// the timed swap's next step, the refresh grid from the GLX_OML_sync_control vblank counter, and the X11 (RandR) mode
/// of the machine's rate. platform/linux/ uses them with the real X server and GL.
namespace linuxpresent
{
/// The Qt platform the window runs on (QGuiApplication::platformName): xcb is X11 (XWayland when WAYLAND_DISPLAY is
/// set too), wayland a Wayland client
enum class WindowSystem : uint8_t
{
    X11,
    XWayland,
    Wayland,
    Other
};

inline WindowSystem WindowSystemOf(const std::string& platformName, bool waylandDisplay)
{
    if (platformName == "xcb")
        return waylandDisplay ? WindowSystem::XWayland : WindowSystem::X11;
    if (platformName.rfind("wayland", 0) == 0)
        return WindowSystem::Wayland;
    return WindowSystem::Other;
}

/// Whether the display under the window can follow the frames (WindowPresentStats::vrrSupport)
enum class VrrSupport : uint8_t
{
    Unknown,
    Yes,
    No
};

inline const char* VrrSupportName(VrrSupport s)
{
    switch (s)
    {
        case VrrSupport::Yes: return "yes";
        case VrrSupport::No: return "no";
        case VrrSupport::Unknown: break;
    }
    return "unknown";
}

/// What the window system says about the window's display
struct VrrInputs
{
    WindowSystem system = WindowSystem::Other;
    /// RandR's `vrr_capable` on the output under the window (amdgpu, i915, nouveau, modern NVIDIA): -1 not exposed
    int vrrCapable = -1;
    /// The GL vendor is NVIDIA's proprietary driver: G-Sync for GL needs a fullscreen flipping window
    bool nvidia = false;
    /// __GL_GSYNC_ALLOWED and __GL_VRR_ALLOWED as the process got them (nullptr: not set; NVIDIA's default is allowed)
    const char* gsyncAllowed = nullptr;
    const char* vrrAllowed = nullptr;
    /// The window covers its whole screen: X11 drivers flip (and so vary the refresh) only for such a window
    bool coversScreen = false;
};

struct VrrDecision
{
    VrrSupport support = VrrSupport::Unknown;
    std::string reason;  ///< why, one phrase (empty for yes)
};

namespace detail
{
/// An environment value that switches a driver feature off: "0", "false", "no", "off"
inline bool SwitchedOff(const char* value)
{
    if (!value)
        return false;
    std::string v = value;
    for (char& c : v)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v == "0" || v == "false" || v == "no" || v == "off";
}
}  // namespace detail

/// The display's VRR for this window. X11: the output's RandR property says whether the display and driver can, and
/// the driver varies the refresh only for a window it flips (fullscreen, unredirected); Mesa opts the window in with
/// `_VARIABLE_REFRESH`. NVIDIA without the property: unknown unless the environment turns it off. Wayland / XWayland:
/// no client query exists, the compositor decides (fullscreen on KDE, GNOME 46+, gamescope)
inline VrrDecision DecideVrr(const VrrInputs& in)
{
    VrrDecision d;
    switch (in.system)
    {
        case WindowSystem::Wayland:
        case WindowSystem::XWayland:
            d.support = VrrSupport::Unknown;
            d.reason = std::string(in.system == WindowSystem::Wayland ? "Wayland" : "XWayland") +
                       ": no client query for VRR, the compositor decides (fullscreen); present_policy=vrr presents "
                       "each frame at its time";
            return d;
        case WindowSystem::Other:
            d.support = VrrSupport::Unknown;
            d.reason = "the window system has no VRR query";
            return d;
        case WindowSystem::X11:
            break;
    }
    if (in.nvidia && (detail::SwitchedOff(in.gsyncAllowed) || detail::SwitchedOff(in.vrrAllowed)))
    {
        d.support = VrrSupport::No;
        d.reason = std::string("NVIDIA: VRR switched off by ") +
                   (detail::SwitchedOff(in.gsyncAllowed) ? "__GL_GSYNC_ALLOWED" : "__GL_VRR_ALLOWED");
        return d;
    }
    if (in.vrrCapable == 0)
    {
        d.support = VrrSupport::No;
        d.reason = "the display is not VRR capable (RandR vrr_capable 0)";
        return d;
    }
    if (in.vrrCapable < 0)
    {
        d.support = VrrSupport::Unknown;
        d.reason = in.nvidia ? "NVIDIA: the driver does not tell; G-Sync for GL needs a fullscreen window (and "
                               "__GL_GSYNC_ALLOWED / __GL_VRR_ALLOWED not 0); present_policy=vrr tries it"
                             : "the output has no RandR vrr_capable property; present_policy=vrr tries it";
        return d;
    }
    if (!in.coversScreen)
    {
        d.support = VrrSupport::No;
        d.reason = "the display is VRR capable, X11 varies the refresh only for a fullscreen window";
        return d;
    }
    d.support = VrrSupport::Yes;
    return d;
}

/// How the timed swap goes: `vrr` each frame at its own time with swap interval 0 (the display starts a refresh when
/// the frame comes); else interval 1 at the time, the first refresh after it (b-vsync, as Metal does on a fixed
/// display)
struct SwapMode
{
    bool vrr = false;
    int interval = 1;
};

/// [VIDEO] PresentPolicy on Linux, as on macOS / Windows: vrr always each frame at its time (forced: the user knows
/// the compositor flips it), vsync always the refresh grid, auto VRR where the decision says yes and the vblanks
/// follow the frames (`engaged` false: they stayed on the grid, VrrEngaged)
inline SwapMode PickSwapMode(PresentPolicy policy, VrrSupport support, bool engaged = true)
{
    SwapMode m;
    m.vrr = policy == PresentPolicy::Vrr || (policy == PresentPolicy::Auto && support == VrrSupport::Yes && engaged);
    m.interval = m.vrr ? 0 : 1;
    return m;
}

/// Does the window present through the timed swap (GlTimedPresenter) or the plain GL swap loop? The timed swap needs
/// the screen's times for its refresh grid (smooth motion) and its lead: GLX_OML_sync_control. Without them it is
/// worth it only when each frame goes at its own time (policy vrr)
inline bool UseTimedSwap(PresentPolicy policy, bool presentFeedback)
{
    return presentFeedback || policy == PresentPolicy::Vrr;
}

/// The present thread's next step for a frame due at `atNs`
enum class SwapStep : uint8_t
{
    Drop,  ///< a later frame is due already: this one would be replaced before any refresh
    Wait,  ///< until `wakeNs`
    Swap   ///< now
};

/// `nextAtNs` the frame queued after it (0: none), `swapCostNs` how long before its time the swap starts
inline SwapStep PlanSwap(int64_t atNs, int64_t nextAtNs, int64_t nowNs, int64_t swapCostNs, int64_t& wakeNs)
{
    wakeNs = 0;
    if (nextAtNs > 0 && nextAtNs <= nowNs)
        return SwapStep::Drop;
    if (atNs > 0 && atNs - swapCostNs > nowNs)
    {
        wakeNs = atNs - swapCostNs;
        return SwapStep::Wait;
    }
    return SwapStep::Swap;
}

/// GLX_OML_sync_control's UST is CLOCK_MONOTONIC in microseconds on Mesa, the clock of std::chrono::steady_clock on
/// Linux. A driver on another clock (more than a second off now) gives no screen times
inline bool UstOnSteadyClock(int64_t ustNs, int64_t nowNs)
{
    return ustNs > 0 && std::llabs(ustNs - nowNs) < 1'000'000'000;
}

/// The display's vblanks from (UST, MSC) pairs: the newest one and the period, the median of the last pairs' UST step
/// over their MSC step. Not thread-safe: the present thread adds, and publishes what it reads
class VBlankClock
{
public:
    static constexpr size_t kWindow = 64;

    void Add(int64_t ustNs, int64_t msc)
    {
        if (ustNs <= 0)
            return;
        if (_lastUstNs > 0 && msc > _lastMsc && ustNs > _lastUstNs)
        {
            const int64_t step = (ustNs - _lastUstNs) / (msc - _lastMsc);
            // A step over 100 ms is a pause (a display asleep, no presents), not a refresh
            if (step > 1'000'000 && step < 100'000'000)
            {
                if (_steps.size() < kWindow)
                    _steps.push_back(step);
                else
                    _steps[_next] = step;
                _next = (_next + 1) % kWindow;
            }
        }
        if (msc >= _lastMsc || ustNs > _lastUstNs)
        {
            _lastUstNs = ustNs;
            _lastMsc = msc;
        }
    }

    int64_t LastNs() const { return _lastUstNs; }
    /// 0 until 8 steps are known
    int64_t PeriodNs() const
    {
        if (_steps.size() < 8)
            return 0;
        std::vector<int64_t> sorted = _steps;
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
        return sorted[sorted.size() / 2];
    }

private:
    std::vector<int64_t> _steps;
    size_t _next = 0;
    int64_t _lastUstNs = 0;
    int64_t _lastMsc = 0;
};

/// Did VRR engage (the Windows presenter's check, CheckVrr)? `vblankMs` the vblank period, `presentMs` the frames'
/// interval, `refreshMs` the mode's period. Yes: the vblanks follow the frames off the mode's grid; no: they stay on
/// the grid while the frames are off it; else not known yet (true: nothing changes)
inline bool VrrEngaged(double vblankMs, double presentMs, double refreshMs, bool& known)
{
    known = false;
    if (vblankMs <= 0.0 || presentMs <= 0.0 || refreshMs <= 0.0)
        return true;
    const bool onGrid = std::fabs(vblankMs - refreshMs) < refreshMs * 0.03;
    const bool follows = std::fabs(vblankMs - presentMs) < presentMs * 0.03;
    const bool presentsOffGrid = std::fabs(presentMs - refreshMs) > refreshMs * 0.10;
    if (follows && !onGrid)
    {
        known = true;
        return true;
    }
    if (onGrid && presentsOffGrid)
    {
        known = true;
        return false;
    }
    return true;
}

/// The window's rectangle covers its screen's (global, device-independent pixels; a pixel of rounding allowed)
inline bool CoversScreen(int x, int y, int width, int height, int screenX, int screenY, int screenWidth,
                         int screenHeight)
{
    return std::abs(x - screenX) <= 1 && std::abs(y - screenY) <= 1 && std::abs(width - screenWidth) <= 1 &&
           std::abs(height - screenHeight) <= 1 && screenWidth > 0 && screenHeight > 0;
}

/// An X11 RandR mode (XRRModeInfo's timing)
struct XMode
{
    uint64_t id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t dotClock = 0;  ///< Hz
    uint32_t hTotal = 0;
    uint32_t vTotal = 0;
    uint64_t flags = 0;  ///< RR_Interlace, RR_DoubleScan, ...
};

inline constexpr uint64_t kModeInterlace = 0x10;   // RR_Interlace
inline constexpr uint64_t kModeDoubleScan = 0x20;  // RR_DoubleScan

/// The mode's refresh rate, as xrandr computes it (0: no timing)
inline double ModeRateHz(const XMode& m)
{
    double vTotal = m.vTotal;
    if (m.flags & kModeDoubleScan)
        vTotal *= 2.0;
    if (m.flags & kModeInterlace)
        vTotal /= 2.0;
    if (m.hTotal == 0 || vTotal <= 0.0)
        return 0.0;
    return static_cast<double>(m.dotClock) / (static_cast<double>(m.hTotal) * vTotal);
}

/// The same picture as `current`: size, progressive (an interlaced mode of the same size is another picture)
inline bool SameResolution(const XMode& m, const XMode& current)
{
    return m.width == current.width && m.height == current.height && (m.flags & kModeInterlace) == 0;
}

/// The rates of the output's modes (`modes` the ones it supports) at the current resolution, the current one included,
/// rounded to 0.01 Hz, each once
inline std::vector<double> RatesAtResolution(const std::vector<XMode>& modes, const XMode& current)
{
    std::vector<double> rates;
    for (const XMode& m : modes)
    {
        if (!SameResolution(m, current))
            continue;
        const double hz = std::round(ModeRateHz(m) * 100.0) / 100.0;
        if (hz <= 0.0)
            continue;
        if (std::none_of(rates.begin(), rates.end(), [&](double r) { return std::fabs(r - hz) < 0.005; }))
            rates.push_back(hz);
    }
    return rates;
}

/// The output's mode at the current resolution with the rate `hz` (one of RatesAtResolution's), the current mode's
/// flags preferred among equals; nullptr: none
inline const XMode* ModeForRate(const std::vector<XMode>& modes, const XMode& current, double hz)
{
    const XMode* best = nullptr;
    for (const XMode& m : modes)
    {
        if (!SameResolution(m, current) || std::fabs(std::round(ModeRateHz(m) * 100.0) / 100.0 - hz) >= 0.005)
            continue;
        if (!best || (m.flags == current.flags && best->flags != current.flags))
            best = &m;
    }
    return best;
}

/// Why the display's mode stays on this window system (empty: X11 can switch it)
inline std::string NoModeSwitchReason(WindowSystem system)
{
    switch (system)
    {
        case WindowSystem::Wayland:
            return "Wayland: the compositor owns the display's mode, a client cannot switch it";
        case WindowSystem::XWayland:
            return "XWayland: the compositor owns the display's mode (X11 RandR there is emulated), a client cannot "
                   "switch it";
        case WindowSystem::Other:
            return "this window system cannot change the display's mode";
        case WindowSystem::X11:
            break;
    }
    return {};
}
}  // namespace linuxpresent
