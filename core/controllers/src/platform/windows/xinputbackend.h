#pragma once

/// @file xinputbackend.h
/// @brief The Windows fallback backend (architecture.md §4, research-host-apis.md §3): XInput's four slots, polled
/// on the library's `controllers-xinput` thread at 250 Hz (an empty slot once a second: XInputGetState on it is
/// slow). XInput names no VID / PID, so the family is "xbox" and the id the slot ("xinput:0"). The DLL is loaded at
/// run time (xinput1_4, then xinput9_1_0), so a host without it reports why instead of failing to start. Where the
/// GameInput backend runs it serves the Xbox pads, and this one stays off (one device once, architecture.md §4).

#include <atomic>
#include <memory>

#include "hostbackend.h"

namespace controllers
{

/// @p xboxServed: set while GameInput serves the Xbox pads; Start then declines with that reason
std::unique_ptr<IBackend> CreateXInputBackend(std::shared_ptr<std::atomic<bool>> xboxServed = nullptr);

}  // namespace controllers
