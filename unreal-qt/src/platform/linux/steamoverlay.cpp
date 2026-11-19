#include "platform/linux/steamoverlay.h"

#ifdef __linux__

#include <cstdlib>

#include "platform/linux/x11display.h"

namespace gamescope
{
namespace
{
/// gamescope's app id of Steam's own windows (its UI, the overlays)
constexpr uint64_t kSteamApp = 769;
}  // namespace

SteamOverlay::SteamOverlay()
{
    const char* wayland = std::getenv("GAMESCOPE_WAYLAND_DISPLAY");
    if (wayland == nullptr || *wayland == '\0')
        return;  // not in gamescope
    // Steam's X display is the first one gamescope starts (GAMESCOPE_XWAYLAND_SERVER_ID 0); the game gets the next
    std::unique_ptr<x11::Display> steam = x11::Display::ConnectTo(":0");
    uint64_t focused = 0;
    if (steam && steam->RootCardinal("GAMESCOPE_FOCUSED_APP", focused))
        _steam = std::move(steam);
}

SteamOverlay::~SteamOverlay() = default;

bool SteamOverlay::Up()
{
    if (!_steam)
        return false;
    uint64_t focused = 0;
    if (!_steam->RootCardinal("GAMESCOPE_FOCUSED_APP", focused))
        return false;
    return focused == kSteamApp;
}
}  // namespace gamescope

#endif  // __linux__
