#pragma once

#ifdef __linux__

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// gamescope's own Wayland protocol, gamescope_control (protocol/gamescope-control.xml in gamescope), the one Steam's
/// refresh rate slider uses in the Steam Deck's Game Mode: the display's valid refresh rates and the target refresh
/// cycle request that makes gamescope switch the panel's mode. A client of gamescope's Wayland socket
/// (GAMESCOPE_WAYLAND_DISPLAY, set for every game it runs, the game itself being an XWayland client), with
/// libwayland-client loaded at run time (dlopen, no headers and no link dependency). One thread at a time.
namespace gamescope
{
class Control
{
public:
    /// The connection to the gamescope running this process, nullptr outside gamescope (no GAMESCOPE_WAYLAND_DISPLAY,
    /// libwayland-client missing, no gamescope_control of version 2 or later); `error` says why
    static std::unique_ptr<Control> Connect(std::string& error);

    ~Control();
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;

    /// The active display as gamescope last told (events read now): its connector, the built-in panel or not, and
    /// its valid refresh rates (whole Hz, ascending; empty when gamescope cannot change its rate)
    bool Display(std::string& connector, bool& internal, std::vector<double>& rates, std::string& error);
    /// set_app_target_refresh_cycle: `fps` whole Hz (0: the display's own rate back), the panel's mode switched
    bool SetRefreshCycle(uint32_t fps, bool internal, std::string& error);

    struct Api;  ///< the resolved functions (gamescopecontrol.cpp)
    struct State;

private:
    Control() = default;
    bool Open(std::string& error);
    bool Roundtrip(std::string& error);

    Api* _api = nullptr;
    State* _state = nullptr;
};
}  // namespace gamescope

#endif  // __linux__
