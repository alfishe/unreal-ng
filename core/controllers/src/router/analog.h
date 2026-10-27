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
};

/// The rescaled deflection of a stick, 0..1: 0 inside the dead zone, 1 past the saturation, linear between
float RadialDeflection(float x, float y, const AnalogOptions& options);

/// A stick's digital directions (kRight kLeft kDown kUp bits; y is -1 up). @p previous is the last answer: it keeps a
/// direction on between `off` and `on`, and keeps the 4-way axis on a diagonal
uint16_t StickDirections(float x, float y, uint16_t previous, const AnalogOptions& options);

/// A trigger's click with hysteresis
bool TriggerClick(float value, bool previous, const AnalogOptions& options);

/// An axis driving a mouse axis, -1..1: sign(v) * ((|v| - deadzone) / (1 - deadzone))^gamma, 0 inside the dead zone
float MouseCurve(float value, const AnalogOptions& options);

/// The scrub curve (debug-and-ttd.md §3.2): a trigger's travel 0..1 -> emulated frames per second. 0 below 0.08 (a
/// resting finger), then 1, 5, 50, 500 and 5000 frames/s at 0.08, 0.30, 0.60, 0.90 and 1.0, exponential between
float ScrubSpeed(float travel);

}  // namespace controllers
