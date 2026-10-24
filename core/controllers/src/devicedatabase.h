#pragma once

/// @file devicedatabase.h
/// @brief What a host device is (device-database.md §1-§4): `data/controllers/devices.json`, compiled into the library,
/// with the user's `devices.user.json` merged on top. A lookup by bus, VID, PID, version and name returns the
/// family, its display name, the layout the HID drivers use, the image set and the glyphs, or that the device is to
/// be ignored (Apple's synthetic pad), with the step and score that chose it.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "hostdevice.h"

namespace controllers
{

struct DeviceIdentity
{
    std::string bus;  ///< "usb", "bt", "" when unknown
    uint16_t vendor = 0;
    uint16_t product = 0;
    int version = -1;  ///< -1 when unknown
    std::string name;
    std::string backend;
};

struct DeviceMatch
{
    std::string family;  ///< "xbox-elite-2"; "generic" when nothing matched
    std::string name;    ///< the display name; the device's own when the database has none
    std::string vendor;
    std::string layout = "generic";  ///< xbox, playstation, nintendo, generic (HidLayoutFor)
    std::string images = "generic-unknown";
    std::string glyphs;
    std::string mode;  ///< the PID's mode ("d-input") when the family names one
    std::vector<std::string> controls;
    /// The family's own HID map (devices.json `hid`, a match's own `hid` replaces it): the controls of its button
    /// usages, its trigger axes, a D-pad on X / Y, controls in vendor bits; HidLayoutFor(DeviceMatch) applies it
    struct HidMap
    {
        struct Bit
        {
            uint8_t report = 0;
            uint32_t byte = 0;
            uint8_t bit = 0;
            std::string control;
        };
        std::vector<std::string> buttons;  ///< usage n (from 1) -> control; "" keeps `button.<n-1>`
        int rxRyTriggers = -1;             ///< -1 the layout's, 0 / 1 overridden
        bool dpadAxes = false;
        std::string hat;  ///< "" the layout's ("dpad")
        std::vector<std::pair<uint16_t, std::string>> consumer;
        std::vector<Bit> bits;
    } hid;
    bool ignore = false;  ///< a device to filter out (the macOS synthetic pad)
    /// It takes a player connector (a pad, a stick); false for a control surface (the Surface Dial): `"player": false`
    bool player = true;
    /// The role a device of the family starts in (`"role"`, control surfaces Q-8): `debugger-remote` for a debug
    /// device such as the Surface Dial (its debug profile acts without the debug layer switch); a user's choice wins
    DeviceRole role = DeviceRole::Player;
    int score = 0;        ///< device-database.md §3: 100 exact, 80 VID + PID, 20 name, 0 unknown
    std::string step;     ///< "exact", "vid-pid", "name", "unknown"
};

class DeviceDatabase
{
public:
    /// Replace the families with @p text (schema 1); false with the reason
    bool Load(const std::string& text, std::string& error);
    /// The user's overrides on top: a family of the same id is replaced, a new one added (§4)
    bool Merge(const std::string& text, std::string& error);

    DeviceMatch Lookup(const DeviceIdentity& device) const;
    /// A family by its id ("xbox-elite-2"); a generic one with images by @p layout when the database has none
    DeviceMatch ForFamily(const std::string& family, const std::string& layout = {}) const;
    /// A control's label on the device ("A", "Cross"); the control's own name when the glyph set has none
    std::string Glyph(const std::string& glyphs, const std::string& control) const;
    size_t FamilyCount() const
    {
        return _families.size();
    }

    /// The built-in database (data/controllers/devices.json as built)
    static const DeviceDatabase& Builtin();

private:
    struct Match
    {
        std::string bus;
        uint16_t vendor = 0;
        std::vector<uint16_t> products;
        int version = -1;
        std::string backend;
        bool hasHid = false;  ///< the match's own `hid` replaces the family's
        DeviceMatch::HidMap hid;
    };
    struct Family
    {
        DeviceMatch info;
        std::vector<Match> matches;
        std::map<uint16_t, std::string> modes;
    };
    struct Template
    {
        std::string name;  ///< lower-case substring of the device name
        DeviceMatch info;
    };

    bool Read(const std::string& text, bool merge, std::string& error);

    std::vector<Family> _families;
    std::vector<Template> _templates;
    std::map<std::string, std::map<std::string, std::string>> _glyphs;
};

/// The text of data/controllers/devices.json, embedded at configure time
const char* BuiltinDevicesJson();

}  // namespace controllers
