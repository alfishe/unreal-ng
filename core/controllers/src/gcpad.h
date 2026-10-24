#pragma once

/// @file gcpad.h
/// @brief The input of a pad only GameController sees (architecture.md §5, `gc:` devices): GCExtendedGamepad's
/// elements as one snapshot, turned into control events by what changed. Pure, so it is tested on every OS; the
/// macOS companion (gccompanion.mm) fills the snapshot in its valueChangedHandler.
///
/// GameController names the faces by position (buttonA the bottom one, as Xbox's A), the thumbsticks' y up positive,
/// the triggers 0..1. buttonMenu is the right centre button (menu), buttonOptions the left (view), buttonHome the
/// logo (guide).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hid/hidpad.h"

namespace controllers
{

struct GcPadSnapshot
{
    enum Element : size_t
    {
        kButtonA,
        kButtonB,
        kButtonX,
        kButtonY,
        kDpadUp,
        kDpadDown,
        kDpadLeft,
        kDpadRight,
        kLeftShoulder,
        kRightShoulder,
        kButtonOptions,
        kButtonMenu,
        kLeftThumbstickButton,
        kRightThumbstickButton,
        kButtonHome,
        kLeftTrigger,   ///< 0..1
        kRightTrigger,  ///< 0..1
        kLeftX,         ///< -1..1, right positive
        kLeftY,         ///< -1..1, up positive (GameController)
        kRightX,
        kRightY,
        kElementCount,
    };
    static constexpr size_t kButtons = kButtonHome + 1;

    std::array<float, kElementCount> values{};
    /// The pad has the element (buttonHome, the thumbstick buttons and buttonOptions are optional)
    std::array<bool, kElementCount> present{};
};

class GcPad
{
public:
    /// CoreHaptics' sharpness of a continuous event with no frequency asked
    static constexpr float kDefaultSharpness = 0.5f;
    /// CoreHaptics' sharpness 0..1 for an HD frequency (haptics.md §9): Apple gives sharpness no unit, so the Switch's
    /// HD rumble codes (41 Hz..1253 Hz, logarithmic) are laid on 0..1; kDefaultSharpness for 0
    static float SharpnessFor(uint32_t hz);

    /// The control names it reports (the ones every extended gamepad may have)
    static std::vector<std::string> Controls();
    /// The changes since the last snapshot go to @p sink. The first one after construction or Reset: as HidPad's first
    /// report, the axes' positions and the pressed buttons only
    void Update(const GcPadSnapshot& snapshot, const ControlSink& sink);
    void Reset()
    {
        _seen = false;
    }

private:
    GcPadSnapshot _last;
    bool _seen = false;
};

}  // namespace controllers
