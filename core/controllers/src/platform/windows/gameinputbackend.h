#pragma once

/// @file gameinputbackend.h
/// @brief The Windows primary backend for Xbox pads (architecture.md §4, D17): GameInput, loaded at run time
/// (GameInputRedist.dll from System32 or the redistributable's folder, then the inbox GameInput.dll; API v3 through
/// GameInputInitialize), so a portable build without the runtime reports why and falls back to XInput. It serves the
/// XInput-class pads only (GameInputServes: GIP and XUSB families) with their paddles and the impulse triggers; HID pads
/// stay with the Raw Input backend. Readings are polled on the library's `controllers-gameinput` thread at 250 Hz;
/// arrivals and removals come from GameInput's device callback and are reported from the same thread.

#include <atomic>
#include <memory>

#include "hostbackend.h"

namespace controllers
{

/// @p xboxServed is set while this backend runs: the XInput backend then stays off (one device once)
std::unique_ptr<IBackend> CreateGameInputBackend(std::shared_ptr<std::atomic<bool>> xboxServed);

}  // namespace controllers
