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
        // Retrigger 60 ms between clicks: they stay apart on the Dial (surface-dial.md §2)
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
    // The voice coils: a short pulse on each, then a knock on both (the backend plays an onset, not a held tone)
    auto hd = [](float left, float right, uint16_t frequency, uint32_t ms) {
        RumbleLevels levels;
        levels.hdLeft = left;
        levels.hdRight = right;
        levels.hdFrequency = frequency;
        return HapticEffect(levels, ms, true);
    };
    if (actuators & kRumbleHdLeft)
        effects.push_back({"hd-left", "Left voice coil", hd(0.8f, 0.0f, 160, 100), std::nullopt});
    if (actuators & kRumbleHdRight)
        effects.push_back({"hd-right", "Right voice coil", hd(0.0f, 0.8f, 160, 100), std::nullopt});
    if ((actuators & kRumbleHdLeft) && (actuators & kRumbleHdRight))
    {
        HapticEffect knocks = hd(1.0f, 1.0f, 90, 360);
        knocks.shape = HapticShape::Pattern;
        knocks.pattern = {{30, 1.0f}, {90, 0.0f}};
        effects.push_back({"hd-knocks", "Voice coils: three knocks", knocks, std::nullopt});
    }
    if (actuators & kHapticClicker)
        effects.push_back({"click", "Click", {}, HapticClicks{HapticClicks::Wave::Click, 1, 0}});
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
    if (actuators & kRumbleHdLeft)
        names.push_back("hd-left");
    if (actuators & kRumbleHdRight)
        names.push_back("hd-right");
    return names;
}

}  // namespace controllers
