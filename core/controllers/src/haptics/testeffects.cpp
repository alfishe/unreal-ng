#include "testeffects.h"

namespace controllers
{

std::vector<TestEffect> TestEffects(uint8_t actuators)
{
    std::vector<TestEffect> effects;
    if (actuators == 0)
        return effects;
    if (actuators == kHapticClicker)
    {
        using Wave = HapticClicks::Wave;
        // Retrigger 60 ms between clicks: they stay apart on the Dial (control-model modules.md §2.2)
        effects.push_back({"identify", "Identify (two clicks)", {}, HapticClicks{Wave::Click, 2, 60}});
        effects.push_back({"click", "Click", {}, HapticClicks{Wave::Click, 1, 0}});
        effects.push_back({"clicks", "Three clicks", {}, HapticClicks{Wave::Click, 3, 60}});
        effects.push_back({"buzz", "Buzz", {}, HapticClicks{Wave::Buzz, 1, 0}});
        return effects;
    }
    effects.push_back({"identify", "Identify (both motors)", HapticEffect({0.6f, 0.6f, 0.0f, 0.0f}, 300, true), std::nullopt});
    if (actuators & kRumbleLow)
        effects.push_back({"low", "Low-frequency motor", HapticEffect({0.8f, 0.0f, 0.0f, 0.0f}, 400, true), std::nullopt});
    if (actuators & kRumbleHigh)
    {
        effects.push_back({"high", "High-frequency motor", HapticEffect({0.0f, 0.8f, 0.0f, 0.0f}, 400, true), std::nullopt});
        HapticEffect clicks({0.0f, 0.9f, 0.0f, 0.0f}, 360, true);
        clicks.shape = HapticShape::Pattern;
        clicks.pattern = {{30, 1.0f}, {90, 0.0f}};
        effects.push_back({"clicks", "Three clicks", clicks, std::nullopt});
    }
    if (actuators & kRumbleLeftTrigger)
        effects.push_back({"left-trigger", "Left trigger", HapticEffect({0.0f, 0.0f, 0.8f, 0.0f}, 400, true), std::nullopt});
    if (actuators & kRumbleRightTrigger)
        effects.push_back({"right-trigger", "Right trigger", HapticEffect({0.0f, 0.0f, 0.0f, 0.8f}, 400, true), std::nullopt});
    HapticEffect swell({0.9f, 0.5f, 0.0f, 0.0f}, 900, true);
    swell.shape = HapticShape::Adsr;
    swell.attackMs = 300;
    swell.decayMs = 150;
    swell.sustain = 0.5f;
    swell.releaseMs = 300;
    effects.push_back({"swell", "Swell (attack, hold, release)", swell, std::nullopt});
    return effects;
}

uint32_t DurationMs(const TestEffect& test)
{
    if (!test.clicks)
        return test.effect.durationMs;
    return static_cast<uint32_t>(test.clicks->count > 0 ? test.clicks->count - 1 : 0) * test.clicks->periodMs;
}

std::vector<std::string> ActuatorNames(uint8_t actuators)
{
    std::vector<std::string> names;
    if (actuators & kRumbleLow)
        names.push_back("low");
    if (actuators & kRumbleHigh)
        names.push_back("high");
    if (actuators & kRumbleLeftTrigger)
        names.push_back("left-trigger");
    if (actuators & kRumbleRightTrigger)
        names.push_back("right-trigger");
    if (actuators & kHapticClicker)
        names.push_back("clicker");
    return names;
}

}  // namespace controllers
