#pragma once

/// @file evdevbackend.h
/// @brief The Linux primary backend (architecture.md §4, research-host-apis.md §4): every /dev/input/event* node
/// with gamepad or joystick buttons, read on the library's `controllers-evdev` thread through poll(); hot plug by
/// inotify on /dev/input. The kernel's drivers (xpad, hid-playstation, hid-nintendo) already name the buttons by
/// position (BTN_SOUTH ..), so no per-device driver is needed; generic joysticks report `button.<n>`. Absolute axes
/// are scaled by their EVIOCGABS range. A node the user cannot open (no `input` group) is reported in the probe.

#include <memory>

#include "hostbackend.h"

namespace controllers
{

std::unique_ptr<IBackend> CreateEvdevBackend();

}  // namespace controllers
