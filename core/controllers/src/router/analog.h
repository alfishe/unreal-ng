#pragma once

/// @file analog.h
/// @brief Analog to digital (mapping-and-profiles.md §3): the radial dead zone, the direction threshold with
/// hysteresis, 8-way and 4-way sectors, the trigger click, the stick-to-mouse curve.

#include <cstdint>

namespace controllers
{

struct AnalogOptions
{
    float deadzone = 0.15f;    ///< radial: below it the stick is centred (MAME inputdev.cpp)
    float saturation = 0.95f;  ///< above it the stick is at full deflection
    float on = 0.45f;          ///< a direction turns on at this rescaled deflection (vAmiga)
    float off = 0.35f;         ///< and off below this one
    unsigned ways = 8;         ///< 8: sectors of 45 degrees centred on the axes; 4: sectors of 90 degrees
    float diagonalHold = 55.0f;  ///< 4-way: the previous axis wins until the angle is this far from it
    float triggerClick = 0.5f;   ///< lt.click / rt.click at this share of the travel
    float triggerRelease = 0.4f;
    float mouseGamma = 1.8f;  ///< an axis driving a mouse axis: the response curve past the dead zone
    /// A touchpad driving a mouse axis (relative, like a laptop's pad): the pointer acceleration, 0 = none. The gain is
    /// 1 + accel * kPadAccelRef * |travel per frame|, capped at kPadMaxGain
    float padAccel = 0.25f;
    /// A touchpad driving a mouse axis: the pointer coasts after the finger lifts (a trackball), slowing by
    /// padInertiaDecay each sample until it stops
    bool padInertia = false;
    float padInertiaDecay = 0.9f;
};

/// Touchpad travel per sample (the pad spans -1..1, so 2.0 is its whole width) where the acceleration gain is 1 + accel
constexpr float kPadAccelRef = 10.0f;
constexpr float kPadMaxGain = 5.0f;
/// A pad position that moves farther than this in one sample (a finger cannot: it is 10 pad widths a second at 50 Hz)
/// is not travel but a report catching up with the touch (the contact flag before the position, the centre a pad reads
/// untouched): the pointer re-anchors there instead of leaping
constexpr float kPadMaxStep = 0.4f;
/// Coasting stops below this travel per sample (pad units)
constexpr float kPadCoastStop = 0.002f;

/// The rescaled deflection of a stick, 0..1: 0 inside the dead zone, 1 past the saturation, linear between
float RadialDeflection(float x, float y, const AnalogOptions& options);

/// A stick's digital directions (kRight kLeft kDown kUp bits; y is -1 up). @p previous is the last answer: it keeps a
/// direction on between `off` and `on`, and keeps the 4-way axis on a diagonal
uint16_t StickDirections(float x, float y, uint16_t previous, const AnalogOptions& options);

/// A trigger's click with hysteresis
bool TriggerClick(float value, bool previous, const AnalogOptions& options);

/// An axis driving a mouse axis, -1..1: sign(v) * ((|v| - deadzone) / (1 - deadzone))^gamma, 0 inside the dead zone
float MouseCurve(float value, const AnalogOptions& options);

/// A touchpad finger's travel since the last sample (pad units, -1..1 spans the pad) as pointer travel: linear, with
/// the speed-dependent gain of a host pointer (options.padAccel). Sign kept, 0 for 0
float PadMouseTravel(float travel, const AnalogOptions& options);

}  // namespace controllers
