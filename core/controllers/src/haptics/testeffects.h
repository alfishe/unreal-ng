#pragma once

/// @file testeffects.h
/// @brief The haptics test effects of a device (haptics.md §10): what the dialog's Test haptics and the `rumble` verb
/// of every surface play. For motors: Identify first, then one per actuator the device has, then an ADSR swell; for a
/// clicker (the Surface Dial): Identify (two clicks), a click, three clicks, a buzz. All are debug feedback: they play
/// while the game's haptics are held back.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "haptics/hapticmixer.h"

namespace controllers
{

struct TestEffect
{
    std::string id;     ///< identify, low, high, clicks, left-trigger, right-trigger, swell
    std::string label;  ///< for a person: "Identify (both motors)"
    HapticEffect effect;                ///< the motors' envelope (HostInputService::PlayEffect)
    std::optional<HapticClicks> clicks;  ///< a clicker's effect instead (IBackend::PlayClicks)
};

/// The test effects of a device with these RumbleActuator bits (RumbleActuators); none for 0
std::vector<TestEffect> TestEffects(uint8_t actuators);
/// How long a test effect plays: its envelope, or a clicker's clicks and the periods between them
uint32_t DurationMs(const TestEffect& test);

/// The names of RumbleActuator bits: low, high, left-trigger, right-trigger, clicker
std::vector<std::string> ActuatorNames(uint8_t actuators);

}  // namespace controllers
