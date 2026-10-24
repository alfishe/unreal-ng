#pragma once

/// @file deviceimport.h
/// @brief Import of other projects' pad mappings into a user device entry (device-database.md §3 steps 3-4,
/// requirements FR-7): an SDL `gamecontrollerdb.txt` line or a RetroArch joypad autoconfig `.cfg`. The result is a
/// devices.user.json family: matched by the VID:PID the source names, its button indices mapped to canonical controls
/// (`buttons`, HidLayoutFor), the controls it lists. Axes and hats are not taken over: the HID driver names sticks,
/// triggers and the hat by their HID usages already; they only add to `controls`.
///
/// @code
///   DeviceImport result;
///   if (ImportDevice("03000000c82d00000631000000010000,8BitDo Ultimate,a:b0,b:b1,...", result, error))
///       HostRequest{"database-save", {{"text", WriteJson(result.entry)}}};
/// @endcode

#include <string>
#include <vector>

#include "profiles/json.h"

namespace controllers
{

struct DeviceImport
{
    std::string source;  ///< "gamecontrollerdb" or "retroarch"
    JsonValue entry;     ///< one devices.user.json family: id user-<vid><pid>, name, match, buttons, controls
    /// What the source maps that the entry does not take over ("leftx:a0", "input_l_x_plus_axis"), for the reply
    std::vector<std::string> skipped;
};

/// A gamecontrollerdb line (GUID,name,mapping,...); false with the reason (a name-based GUID has no USB ids)
bool ImportGameControllerDb(const std::string& line, DeviceImport& out, std::string& error);
/// A RetroArch autoconfig file's text (input_device, input_vendor_id, input_product_id, input_*_btn)
bool ImportRetroArch(const std::string& text, DeviceImport& out, std::string& error);
/// Either, told apart by its shape: `key = "value"` lines are RetroArch, a comma-separated line a gamecontrollerdb one
/// (the first that is not a comment when a whole gamecontrollerdb.txt is pasted)
bool ImportDevice(const std::string& text, DeviceImport& out, std::string& error);

}  // namespace controllers
