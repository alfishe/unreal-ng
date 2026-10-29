#pragma once

/// @file hidusage.h
/// @brief A HID field a device family reads its own way (devices.json `hid.usages`, device-database.md §2): shared by
/// the device database, which parses it, and HidPad, which decodes it.

#include <cstdint>
#include <string>

namespace controllers
{

/// How a family reads one field itself (devices.json `hid.usages`): a usage the generic mapping does not know, or one
/// whose descriptor says something the device does not do (the ShuttlePRO's shuttle is declared a relative wheel of
/// -128..127 and sends an absolute -7..7)
enum class HidRead : uint8_t
{
    Button,   ///< non-zero is pressed
    Axis,     ///< centred, -1 .. 1
    Trigger,  ///< 0 .. 1
    Split,    ///< centred, read as two triggers `<control>.left` (below the centre) and `<control>.right` (above)
    Encoder,  ///< a relative count: every non-zero report is that many steps (`knob`, `jog`)
    Counter,  ///< an absolute count that wraps at its bit size: the steps are the change since the last report
};

struct HidUsage
{
    uint16_t page = 0;
    uint16_t usage = 0;
    std::string control;
    HidRead read = HidRead::Button;
    bool range = false;  ///< min / max replace the descriptor's logical range
    int32_t min = 0;
    int32_t max = 0;
};

/// "button", "axis", "trigger", "split", "encoder", "counter" -> @p out; false for anything else
bool HidReadFromName(const std::string& name, HidRead& out);

}  // namespace controllers
