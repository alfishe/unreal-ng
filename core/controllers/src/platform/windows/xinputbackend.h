#pragma once

/// @file xinputbackend.h
/// @brief The Windows fallback backend (architecture.md §4, research-host-apis.md §3): XInput's four slots, polled
/// on the library's `controllers-xinput` thread at 250 Hz (an empty slot once a second: XInputGetState on it is
/// slow). XInput names no VID / PID, so the family is "xbox" and the id the slot ("xinput:0"). The DLL is loaded at
/// run time (xinput1_4, then xinput9_1_0), so a host without it reports why instead of failing to start.

#include <memory>

#include "hostbackend.h"

namespace controllers
{

std::unique_ptr<IBackend> CreateXInputBackend();

}  // namespace controllers
