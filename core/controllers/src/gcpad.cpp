#include "gcpad.h"

#include "hid/drivers/switchpro.h"

namespace controllers
{

namespace
{
const char* const kNames[GcPadSnapshot::kElementCount] = {
    "south",    "east",     "west",   "north",        "dpad.up",      "dpad.down", "dpad.left",
    "dpad.right", "lb",     "rb",     "view",         "menu",         "lstick.click", "rstick.click",
    "guide",    "lt",       "rt",     "lstick.x",     "lstick.y",     "rstick.x",  "rstick.y",
};

/// GameController's y is up positive; -1 is up here (hostcontrols.h)
float Oriented(size_t element, float value)
{
    const bool y = element == GcPadSnapshot::kLeftY || element == GcPadSnapshot::kRightY;
    return y ? -value : value;
}
}  // namespace

float GcPad::SharpnessFor(uint32_t hz)
{
    if (hz == 0)
        return kDefaultSharpness;
    const int code = SwitchRumble::FrequencyCode(hz);
    const int clamped = code < 0x41 ? 0x41 : code > 0xDF ? 0xDF : code;
    return static_cast<float>(clamped - 0x41) / static_cast<float>(0xDF - 0x41);
}

std::vector<std::string> GcPad::Controls()
{
    return std::vector<std::string>(kNames, kNames + GcPadSnapshot::kElementCount);
}

void GcPad::Update(const GcPadSnapshot& snapshot, const ControlSink& sink)
{
    for (size_t element = 0; element < GcPadSnapshot::kElementCount; element++)
    {
        if (!snapshot.present[element])
            continue;
        const bool button = element < GcPadSnapshot::kButtons;
        // A button is pressed or not; its analog pressure (a DualSense's) stays GameController's
        const float value = button ? (snapshot.values[element] > 0.5f ? 1.0f : 0.0f)
                                   : Oriented(element, snapshot.values[element]);
        const float last = button ? (_last.values[element] > 0.5f ? 1.0f : 0.0f)
                                  : Oriented(element, _last.values[element]);
        const bool changed = !_seen || !_last.present[element] || value != last;
        if (!changed || (button && (!_seen || !_last.present[element]) && value == 0))
            continue;
        sink(kNames[element], value);
    }
    _last = snapshot;
    _seen = true;
}

}  // namespace controllers
