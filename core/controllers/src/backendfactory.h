#pragma once

/// @file backendfactory.h
/// @brief The native backends of this OS, most capable first (architecture.md §4): IOHID and GameController on
/// macOS (IOHID since C3), GameInput / Raw Input / XInput on Windows (C8; XInput only where GameInput does not start),
/// evdev / hidraw on Linux (C9).

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
