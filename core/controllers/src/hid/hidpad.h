#pragma once

/// @file hidpad.h
/// @brief A pad decoded from its HID report descriptor (architecture.md §3, `hid/`): each input report becomes the
/// canonical controls that changed. A family layout names the buttons (`Button 1` is `south` on an Xbox pad, `west`
/// on a PlayStation pad); without one they stay `button.<n>`. The Xbox Bluetooth report adds the paddles and the
/// onboard profile (research-host-apis.md §2.3b).

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "hid/hiddescriptor.h"

namespace controllers
{

/// How a family's HID pad names its controls
struct HidLayout
{
    std::string family;  ///< "xbox", "playstation", "switch", "generic"
    /// Button usage n (from 1) -> control; an empty or missing entry is `button.<n-1>`
    std::vector<std::string> buttons;
    /// Rx / Ry are the triggers (PlayStation), not a second stick
    bool rxRyTriggers = false;
};

/// A device database layout (`xbox`, `playstation`, `nintendo`); anything else is `generic`
HidLayout HidLayoutFor(const std::string& layout);
/// As above, with a family's own button names over it (the database's `buttons`, an imported gamecontrollerdb line or
/// RetroArch autoconfig: index n = HID button n + 1; an empty entry keeps the layout's name)
HidLayout HidLayoutFor(const std::string& layout, const std::vector<std::string>& buttons);

using ControlSink = std::function<void(const std::string& control, float value)>;

class HidPad
{
public:
    HidPad(const HidDescriptor& descriptor, const HidLayout& layout);

    /// No field of the descriptor is a pad control (a keyboard, a vendor device)
    bool Empty() const
    {
        return _slots.empty();
    }
    std::vector<std::string> Controls() const;
    /// One input report; every control that changed goes to @p sink
    void Decode(const uint8_t* report, size_t size, const ControlSink& sink);
    /// The Xbox onboard profile (0 when the pad does not report one)
    uint8_t Profile() const
    {
        return _profile;
    }

private:
    enum class Kind : uint8_t
    {
        Button,
        Axis,
        Trigger,
        Hat,
        Paddles,  ///< Xbox Bluetooth: P1-P4 bits
        Profile,  ///< Xbox Bluetooth: the onboard profile
    };
    struct Slot
    {
        HidField field;
        Kind kind = Kind::Button;
        std::string control;
        int32_t last = 0;
        bool seen = false;
    };

    void Emit(const std::string& control, float value);

    bool _hasReportIds = false;
    std::vector<Slot> _slots;
    uint8_t _profile = 0;
    /// Undoing the onboard profile's mirror (research §2.3b): the face button a paddle press brought along, per
    /// paddle, suppressed while the paddle is held
    std::string _mirrored[4];
    std::vector<std::pair<std::string, float>> _pending;
};

}  // namespace controllers
