#pragma once

/// @file binding.h
/// @brief A binding: host control(s) -> one retro target, with options (mapping-and-profiles.md §1).
/// C2a has the retro targets; ZX keys, macros, mouse, actions and layers come with the layers (TODO.md C2b).

#include <cstdint>
#include <string>
#include <vector>

#include "router/analog.h"

namespace controllers
{

/// mapping-and-profiles.md §1.3
enum class Turbo : uint8_t
{
    Off,
    Classic,  ///< pulses while held
    Toggle,   ///< a press starts pulsing, the next one stops it
    Hold,     ///< a press holds the target until the next press (autohold)
    Burst,    ///< `count` pulses per press
};

enum class PressMode : uint8_t
{
    Hold,  ///< the target follows the source
    Tap,   ///< a `frames` pulse on the press edge, however long the source is held
};

struct BindingOptions
{
    Turbo turbo = Turbo::Off;
    unsigned on = 2;   ///< turbo: emulated frames on (deterministic: replays identically)
    unsigned off = 2;  ///< turbo: emulated frames off
    unsigned count = 3;  ///< burst pulses
    PressMode press = PressMode::Hold;
    unsigned frames = 2;  ///< tap length
    AnalogOptions analog;
    std::string label;  ///< the game's meaning ("Fire"), shown in the picture
};

/// `<connector>:<control>`: `kempston:fire`, `next.left:c`, `auto:dpad` (the connector this device drives)
struct RetroTarget
{
    std::string connector;  ///< a ControllerManager selector, or "auto"
    uint16_t mask = 0;      ///< RetroControlMask bits
};

struct Binding
{
    /// Any of them (an OR-list). A control (`south`, `lt.click`, `lstick.left`, `key.kp0`), `lstick` / `rstick` (the
    /// whole stick: its directions), or `dpad.*` (the four D-pad buttons as directions)
    std::vector<std::string> sources;
    RetroTarget target;
    BindingOptions options;
};

/// "kempston:fire" -> the target; false with the reason (an unknown control lists the known ones)
bool ParseRetroTarget(const std::string& text, RetroTarget& out, std::string& error);

/// Every source a known control (or a stick / `dpad.*`), a target with a connector and bits, sane frame counts;
/// false with the reason
bool ValidateBinding(const Binding& binding, std::string& error);

/// "off" "classic" "toggle" "hold" "burst"
bool TurboFromName(const std::string& name, Turbo& out);
const char* TurboName(Turbo turbo);

}  // namespace controllers
