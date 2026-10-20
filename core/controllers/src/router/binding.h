#pragma once

/// @file binding.h
/// @brief A binding: host control(s) -> one target, with options, in a layer (mapping-and-profiles.md §1-§2).

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
    /// The router keeps the sources from the machine's own keyboard (a `key.*` source drives only this target);
    /// false = pass: the key reaches both (mapping-and-profiles.md §4.2)
    bool consume = true;
    AnalogOptions analog;
    /// `curve: scrub`: an action gets the source's level (0..1) whenever it changes, not a press
    /// (debug-and-ttd.md §3.2, the scrub curve)
    bool level = false;
    std::string label;  ///< the game's meaning ("Fire"), shown in the picture
};

/// `<connector>:<control>`: `kempston:fire`, `next.left:c`, `auto:dpad` (the connector this device drives)
struct RetroTarget
{
    std::string connector;  ///< a ControllerManager selector, or "auto"
    uint16_t mask = 0;      ///< RetroControlMask bits
};

enum class TargetKind : uint8_t
{
    None,    ///< explicitly unbound: overrides what a lower layer binds to the same controls
    Retro,   ///< a connector's retro controls
    Keys,    ///< ZX keys held together (a key, a chord, an extended key)
    Macro,   ///< a sequence of ZX key steps, each held one frame and released one frame
    Mouse,   ///< a mouse button, axis or the wheel
    Action,  ///< a host command, run when it happens (never journaled)
    Layer,   ///< switches layers
};

enum class MouseControl : uint8_t
{
    Left,
    Right,
    Middle,
    Dx,     ///< counts per frame: `scale` while a button is held, `scale` at full deflection of an axis
    Dy,     ///< as Dx, + = down (a host mouse's and a stick's sense)
    Wheel,  ///< `scale` notches per press
};

enum class LayerMode : uint8_t
{
    Hold,     ///< the layer is up while the layer key is held
    Toggle,   ///< a press raises it, the next one drops it
    Oneshot,  ///< up until a control of the layer has been pressed and released
};

struct Target
{
    TargetKind kind = TargetKind::None;
    RetroTarget retro;                        ///< Retro
    std::vector<uint8_t> keys;                ///< Keys: plain matrix keys (ZXKeysEnum values, zxkeys.h)
    std::vector<std::vector<uint8_t>> steps;  ///< Macro
    MouseControl mouse = MouseControl::Left;  ///< Mouse
    float scale = 8.0f;                       ///< Mouse: counts (Dx / Dy) or notches (Wheel); negative reverses
    std::string name;                         ///< Action: the action; Layer: the layer
    LayerMode layerMode = LayerMode::Hold;    ///< Layer
};

/// The layer of the bindings with no layer of their own; `debug` is the debug layer (opaque: what it does not bind
/// does nothing while it is up), any other name a profile's shift layer
constexpr const char* kPlayLayer = "";
constexpr const char* kDebugLayer = "debug";

struct Binding
{
    /// Any of them (an OR-list), or with `chord` all of them held together. A control (`south`, `lt.click`,
    /// `lstick.left`, `key.kp0`), `lstick` / `rstick` (the whole stick: its directions), or `dpad.*` (the four
    /// D-pad buttons as directions). A Dx / Dy mouse target also takes one axis (`rstick.x`)
    std::vector<std::string> sources;
    /// The sources pressed together within the chord window (60 ms, in any order); until the window closes the
    /// members' own bindings are held back, and when the chord completes they never see the press
    bool chord = false;
    Target target;
    BindingOptions options;
    std::string layer = kPlayLayer;
};

/// "kempston:fire" -> the target; false with the reason (an unknown control lists the known ones)
bool ParseRetroTarget(const std::string& text, RetroTarget& out, std::string& error);
bool ParseRetroTarget(const std::string& text, Target& out, std::string& error);
/// "SPACE", "CAPS+1", "EDIT" (zxkeys.h)
bool ParseKeysTarget(const std::string& text, Target& out, std::string& error);
/// {"J", "SYM+P", "SYM+P", "ENTER"}
bool ParseMacroTarget(const std::vector<std::string>& steps, Target& out, std::string& error);
/// "left" "right" "middle" "dx" "dy" "wheel"
bool ParseMouseTarget(const std::string& control, float scale, Target& out, std::string& error);
Target ActionTarget(const std::string& action);
/// "hold" "toggle" "oneshot"
bool ParseLayerTarget(const std::string& layer, const std::string& mode, Target& out, std::string& error);

/// Every source a known control (or a stick / `dpad.*`) fitting the target, chord members buttons, a complete
/// target, sane frame counts; false with the reason
bool ValidateBinding(const Binding& binding, std::string& error);

/// "off" "classic" "toggle" "hold" "burst"
bool TurboFromName(const std::string& name, Turbo& out);
const char* TurboName(Turbo turbo);

}  // namespace controllers
