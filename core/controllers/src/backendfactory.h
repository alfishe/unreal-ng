#pragma once

/// @file backendfactory.h
/// @brief The native backends of this OS, most capable first (architecture.md §4): IOHID and GameController on
/// macOS, GameInput / Raw Input / XInput on Windows, evdev / hidraw on Linux. None yet: they come with C3, C8, C9.

#include <memory>
#include <vector>

#include "hostbackend.h"

namespace controllers
{

class BackendFactory
{
public:
    static std::vector<std::unique_ptr<IBackend>> CreateNative();
};

}  // namespace controllers
