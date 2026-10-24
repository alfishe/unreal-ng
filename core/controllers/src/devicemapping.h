#pragma once

/// @file devicemapping.h
/// @brief A device the database does not know (device-database.md §3 step 6, ui-ux.md §3.5): which of its raw
/// controls (`button.<n>`, `axis.<n>`, a half of an axis) is which canonical control. The learn wizard finds it one
/// prompt at a time (LearnSession), a `gamecontrollerdb.txt` line or a RetroArch autoconfig file brings it
/// (ImportMapping), and the result is a user family in `devices.user.json` with a `map` (UserFamilyJson).
/// HostInputService applies a device's map: each raw control also reports as its canonical control, so the play and
/// debug profiles of a pad work on it.
///
/// Raw specs: a control name (`button.3`, `dpad.up`, `hat.0.left`, `south`) read as a button (pressed at 0.5), a
/// control with `+` / `-` (`axis.1-`, `lstick.y+`) read as one half of an axis (0..1), an axis name alone
/// (`axis.0`) as the whole axis.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "devicedatabase.h"
#include "hostdevice.h"
#include "profiles/json.h"

namespace controllers
{

/// One canonical control from one raw control
struct MappedControl
{
    std::string canonical;  ///< "south", "dpad.up", "lstick.x"
    std::string raw;        ///< the raw control's name, without the sign
    int half = 0;           ///< +1 / -1: that half of an axis as a 0..1 value; 0: the value as it is
};

/// canonical -> raw spec, as `map` in devices.json; false with the reason for an unknown canonical control or a
/// malformed raw spec
bool ParseControlMap(const std::map<std::string, std::string>& map, std::vector<MappedControl>& out,
                     std::string& error);

/// The canonical events a raw event makes through @p map (none when the map does not name it)
std::vector<ControlEvent> TranslateControl(const std::vector<MappedControl>& map, const ControlEvent& event);

/// The SDL GUID of a device (bus, VID, PID, version little-endian, as SDL 2 writes them); "" without a VID
std::string SdlGuid(const DeviceIdentity& identity);
/// Bus, VID, PID and version from an SDL GUID (32 hex digits); false when it is not one or carries no VID / PID
bool IdentityFromSdlGuid(const std::string& guid, DeviceIdentity& out);
/// Bus, VID, PID and version from a host device id ("usb:045e:0b00:0517:<serial>", "bt:045e:0b22:<hash>");
/// false for the ids without them (a keyboard, a GameController pad, a fake)
bool IdentityFromDeviceId(const HostDeviceId& id, DeviceIdentity& out);
/// "045e:0b00" -> VID and PID; false when malformed
bool ParseVidPid(const std::string& text, uint16_t& vendor, uint16_t& product);

/// A mapping brought from elsewhere
struct ImportedMapping
{
    std::string source;  ///< "gamecontrollerdb" or "retroarch"
    std::string name;
    DeviceIdentity identity;
    std::map<std::string, std::string> map;  ///< canonical -> raw spec
    std::vector<std::string> skipped;        ///< the entries without a canonical control here, as written
};
/// A `gamecontrollerdb.txt` line (GUID,name,a:b0,...,platform:...) or a RetroArch autoconfig file
/// (input_vendor_id, input_product_id, input_b_btn = "0", input_l_x_plus_axis = "+0", input_up_btn = "h0up"):
/// false with the reason
bool ImportMapping(const std::string& text, ImportedMapping& out, std::string& error);

/// A `gamecontrollerdb.txt` line of @p map (the raw controls SDL names: bN, +aN / -aN / aN, hN.M; the others are
/// left out and listed in @p skipped)
std::string GameControllerDbLine(const DeviceIdentity& identity, const std::string& name,
                                 const std::map<std::string, std::string>& map, std::vector<std::string>* skipped);

/// The user family of a mapped device for devices.user.json: `user-<vid><pid>`, its name, a VID + PID match, the
/// generic layout, the controls, the map, where it came from and its gamecontrollerdb line
JsonValue UserFamilyJson(const DeviceIdentity& identity, const std::string& name,
                         const std::map<std::string, std::string>& map, const std::string& source);
/// `user-045e0b00`
std::string UserFamilyId(const DeviceIdentity& identity);
/// Put @p family into the devices.user.json text @p text (an empty text starts a new one), replacing the family of
/// the same id: the new text; false with the reason when @p text is not a schema 1 database
bool MergeUserFamily(const std::string& text, const JsonValue& family, std::string& out, std::string& error);

/// The learn wizard (ui-ux.md §3.5): it walks the canonical controls, "Press Up..", and takes the first raw edge
/// against the baseline for each (a button pressed, an axis moved more than 0.4 from where it rested), then waits
/// for that control to come back before the next prompt. A raw control is taken once
class LearnSession
{
public:
    /// The controls asked for by default: the D-pad, the face buttons, the shoulders, the triggers, View, Menu
    static const std::vector<std::string>& DefaultControls();
    /// "Press Up", "Press South (the bottom face button)"
    static std::string Prompt(const std::string& control);

    LearnSession() : LearnSession(DefaultControls())
    {
    }
    /// The controls to ask for, in order (canonical names; an unknown one is refused by Make)
    explicit LearnSession(std::vector<std::string> controls) : _controls(std::move(controls))
    {
    }
    /// @p controls comma separated ("dpad.up,dpad.down,dpad.left,dpad.right,south"; "" = the default set)
    static bool Make(const std::string& controls, LearnSession& out, std::string& error);

    /// Where each control rests now (HostInputService::Live): an axis that rests off centre is measured from there
    void Baseline(const std::map<std::string, float>& live);
    /// A raw event of the device: true when it answered the current prompt
    bool Feed(const std::string& control, float value);
    /// Leave the current control unmapped
    void Skip();
    /// Ask the previous control again (its answer is dropped)
    void Back();

    bool Done() const
    {
        return _index >= _controls.size();
    }
    size_t Index() const
    {
        return _index;
    }
    const std::vector<std::string>& Controls() const
    {
        return _controls;
    }
    /// The control asked for now; "" when done
    std::string Current() const
    {
        return Done() ? std::string() : _controls[_index];
    }
    /// The answer to Current waits for its control to come back
    bool WaitingForRelease() const
    {
        return !_held.empty();
    }
    /// canonical -> raw spec of the answers so far
    const std::map<std::string, std::string>& Mapping() const
    {
        return _mapping;
    }

private:
    float Rest(const std::string& control) const;

    std::vector<std::string> _controls;
    size_t _index = 0;
    std::map<std::string, float> _baseline;
    std::map<std::string, std::string> _mapping;
    std::string _held;  ///< the raw control of the last answer, until it comes back
};

}  // namespace controllers
