#pragma once

/// @file hostcontrols.h
/// @brief The canonical control vocabulary of host devices (architecture.md §6): positional names for pads, so a
/// binding to `south` works on Xbox A, PlayStation Cross and Nintendo B alike; plus the retro control names a
/// binding target uses (`kempston:fire`, `next.left:c`).

#include <cstdint>
#include <string>
#include <vector>

namespace controllers
{

enum class ControlType : uint8_t
{
    Button,   ///< 0 / 1
    Axis,     ///< -1 .. 1; a stick's y is -1 up (the W3C standard gamepad)
    Trigger,  ///< 0 .. 1
    Delta,    ///< a mouse count per event (mouse.dx, mouse.dy, mouse.wheel)
    Pointer,  ///< an absolute screen position (pointer.x, pointer.y)
    Encoder,  ///< a rotary encoder: signed detents since the last event (a Surface Dial's `knob`); not a mouse delta
};

/// What a control name means
struct ControlInfo
{
    std::string name;  ///< canonical, lower case
    ControlType type = ControlType::Button;
    /// A derived digital control (`lstick.left`, `lt.click`): the analog control it is read from, else empty
    std::string base;
    /// For a derived stick direction: the direction bit (kRight, kLeft, kDown, kUp of controllertypes.h)
    uint16_t direction = 0;
};

/// A control name (case-insensitive) -> its meaning. Accepts the fixed names, `button.<n>`, `axis.<n>`,
/// `hat.<n>.<up|down|left|right>`, `key.<PcKey name>` (the name is checked where keys are bound) and the derived
/// digital controls. False for anything else
bool ParseControl(const std::string& text, ControlInfo& out);

/// The fixed control names (for "did you mean" lists): face, D-pad, shoulders, sticks, system, extras, motion, mouse,
/// the control surfaces' `knob` (encoder), `push` and `touch` (the hand on the knob)
const std::vector<std::string>& KnownControls();

/// The stick a name stands for as a whole (`lstick`, `rstick`), for a binding of all four directions; empty otherwise
std::string StickOf(const std::string& name);

/// A retro control name -> its bits in a connector mask: `right` `left` `down` `up` `fire` `fire2` `fire3` `fire4`,
/// the MD pad's `b` `c` `a` `start` (the same bits 4-7 as fire 1-4) and `y` `z` `x` `mode` (bits 8-11, the
/// PadState layout), and `dpad` (the four directions). False for an unknown name
bool RetroControlMask(const std::string& name, uint16_t& mask);

/// The retro control names RetroControlMask takes
const std::vector<std::string>& KnownRetroControls();

}  // namespace controllers
