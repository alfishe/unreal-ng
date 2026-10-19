#pragma once

/// @file xboxgip.h
/// @brief The Xbox GIP input report (0x20) as macOS's XboxGamepad.dext passes it over USB: 18 bytes, the first 14
/// payload bytes only, so no paddles and no profile (research-host-apis.md §2.3a; layout in
/// testdata/controllers/hid-captures/xbox-elite2/macos-usb-dext/README.md).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hid/hidpad.h"

namespace controllers
{

class XboxGipPad
{
public:
    /// The USB pads the dext publishes a vendor endpoint for: 360, One S, Elite 2, Series X|S
    static bool Handles(uint16_t vendor, uint16_t product);

    static std::vector<std::string> Controls();
    /// One input report; every control that changed goes to @p sink. Other reports are ignored
    void Decode(const uint8_t* report, size_t size, const ControlSink& sink);

private:
    uint16_t _buttons = 0;
    uint16_t _triggers[2] = {};
    int16_t _sticks[4] = {};
    bool _seen = false;
};

}  // namespace controllers
