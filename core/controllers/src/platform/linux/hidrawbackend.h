#pragma once

/// @file hidrawbackend.h
/// @brief Linux hidraw (architecture.md §4): the devices whose controls or actuators evdev does not carry. The Steam
/// Deck's built-in controller (28DE:1205, interface 2): every control (the paddles, both trackpads, the stick touch,
/// motion), both handle motors and the trackpads' voice coils (hid/drivers/steamdeck.h). Reading it works next to
/// Steam: every reader gets every report. Without Steam the lizard mode (the trackpad mouse, the buttons as keys) is
/// switched off and its watchdog fed while the device is open; it comes back on its own when we stop.

#include <memory>

#include "hostbackend.h"

namespace controllers
{

std::unique_ptr<IBackend> CreateHidrawBackend();

}  // namespace controllers
