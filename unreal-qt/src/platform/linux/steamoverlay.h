#pragma once

#ifdef __linux__

#include <memory>

namespace x11
{
class Display;
}

/// Steam's own UI over the game in gamescope (the Steam Deck's Game Mode): the Steam menu and the Quick Access menu.
/// While either is open gamescope gives Steam the focus, GAMESCOPE_FOCUSED_APP on Steam's X display (":0", the game
/// runs on another one) turns to 769 (Steam) from the game's app id (SteamAppId). The pads still report to every
/// reader of their devices, so the app must stop reading them itself (HostControllers: not the controllers' owner).
/// Verified 2026-11-13 on a Steam Deck: the Quick Access menu open = 769, closed = 3357196967 (the game).
namespace gamescope
{
class SteamOverlay
{
public:
    SteamOverlay();
    ~SteamOverlay();
    SteamOverlay(const SteamOverlay&) = delete;
    SteamOverlay& operator=(const SteamOverlay&) = delete;

    /// Running under gamescope with its Steam display reachable (else Up is always false)
    bool Available() const { return _steam != nullptr; }
    /// Steam's UI has the focus now (one round trip to Steam's X server); GUI thread only
    bool Up();

private:
    std::unique_ptr<x11::Display> _steam;
};
}  // namespace gamescope

#endif  // __linux__
