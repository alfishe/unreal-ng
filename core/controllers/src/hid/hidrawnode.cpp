#include "hid/hidrawnode.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "hid/drivers/surfacedial.h"
#include "hid/drivers/xboxbluetoothoutput.h"

namespace controllers
{

namespace
{
/// One hex field of HID_ID; false when it is empty, not hex or above @p max
bool Hex(const std::string& text, unsigned long max, unsigned long& out)
{
    if (text.empty() || text.size() > 8)
        return false;
    char* end = nullptr;
    out = std::strtoul(text.c_str(), &end, 16);
    return end != nullptr && *end == '\0' && out <= max;
}
}  // namespace

LinuxRumble LinuxRumbleFor(bool evdevRumble, bool xboxBluetooth)
{
    if (evdevRumble)
        return LinuxRumble::Evdev;
    return xboxBluetooth ? LinuxRumble::Hidraw : LinuxRumble::None;
}

HidrawUse HidrawUseFor(bool bluetooth, uint16_t vendor, uint16_t product, const std::string& family,
                       const std::string& layout, bool evdevRumble)
{
    if (SurfaceDial::Handles(vendor, product))
        return HidrawUse::Output;
    HidrawUse use = HidrawUse::None;
    // Over USB the Elite is xpad's (a USB driver, no HID device, no hidraw node): its paddles are evdev buttons
    if (bluetooth && family == "xbox-elite-2")
        use = use | HidrawUse::Input;
    if (LinuxRumbleFor(evdevRumble, XboxBluetoothOutput::Handles(bluetooth, vendor, layout, family)) ==
        LinuxRumble::Hidraw)
        use = use | HidrawUse::Rumble;
    return use;
}

bool ParseHidUevent(const std::string& text, HidUevent& out)
{
    HidUevent parsed;
    bool haveId = false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "HID_ID")
        {
            // bus:vendor:product, "%04X:%08X:%08X"
            const size_t first = value.find(':');
            const size_t second = first == std::string::npos ? first : value.find(':', first + 1);
            if (second == std::string::npos)
                return false;
            unsigned long bus = 0;
            unsigned long vendor = 0;
            unsigned long product = 0;
            if (!Hex(value.substr(0, first), 0xFFFF, bus) || !Hex(value.substr(first + 1, second - first - 1), 0xFFFF,
                                                                  vendor) ||
                !Hex(value.substr(second + 1), 0xFFFF, product))
                return false;
            parsed.bus = static_cast<uint16_t>(bus);
            parsed.vendor = static_cast<uint16_t>(vendor);
            parsed.product = static_cast<uint16_t>(product);
            haveId = true;
        }
        else if (key == "HID_NAME")
            parsed.name = value;
        else if (key == "HID_PHYS")
            parsed.phys = value;
        else if (key == "HID_UNIQ")
            parsed.uniq = value;
    }
    if (!haveId)
        return false;
    out = parsed;
    return true;
}

bool FindHidrawNode(const std::string& sysRoot, const std::string& devRoot, const std::string& eventName,
                    uint16_t vendor, uint16_t product, HidrawNode& out)
{
    namespace fs = std::filesystem;
    std::error_code error;
    // eventN/device is the input device (inputM), its device the HID device (0005:045E:0B22.0004) for a HID driver
    const fs::path hid = fs::canonical(fs::path(sysRoot) / "class" / "input" / eventName / "device" / "device", error);
    if (error)
        return false;
    std::ifstream file(hid / "uevent");
    if (!file)
        return false;
    std::stringstream text;
    text << file.rdbuf();
    HidUevent uevent;
    if (!ParseHidUevent(text.str(), uevent) || uevent.vendor != vendor || uevent.product != product)
        return false;
    std::vector<std::string> names;
    for (fs::directory_iterator it(hid / "hidraw", error), end; !error && it != end; it.increment(error))
    {
        const std::string name = it->path().filename().string();
        if (name.rfind("hidraw", 0) == 0 && name.size() > 6)
            names.push_back(name);
    }
    if (names.empty())
        return false;
    std::sort(names.begin(), names.end());  // one per HID device; the order only keeps a broken tree deterministic
    out.node = devRoot + "/" + names.front();
    out.hidDevice = hid.string();
    out.uevent = uevent;
    return true;
}

const char* const kHidrawRulesHint =
    "Install the udev rule: sudo cp data/controllers/70-unreal-ng.rules /etc/udev/rules.d/ && "
    "sudo udevadm control --reload && sudo udevadm trigger, then reconnect the device";

std::string HidrawUnavailable(HidrawUse use, const std::string& node, int error, const std::string& errorText)
{
    std::string what = "The Surface Dial's clicks and detents";
    std::string without;
    if (HasUse(use, HidrawUse::Input))
    {
        what = "The paddles and the onboard profile";
        without = " Until then the paddles are not seen (on onboard profiles 1-3 they arrive as the buttons the "
                  "profile assigns).";
    }
    else if (HasUse(use, HidrawUse::Rumble))
    {
        what = "The motors and the impulse triggers";
        without = " Until then the pad does not rumble: its kernel driver offers no FF_RUMBLE (hid-microsoft or "
                  "xpadneo would).";
    }
    if (node.empty())
        return what + " need the device's hidraw node, which was not found (is the hidraw module loaded?)." + without;
    if (error == EACCES || error == EPERM)
        return what + " need " + (HasUse(use, HidrawUse::Input) ? "read" : "write") + " access to " + node +
               ", which this user does not have. " + kHidrawRulesHint + "." + without;
    return what + " need " + node + ", which did not open (" + errorText + ")." + without;
}

std::string XpadPaddlesNotice()
{
    return "Over USB the xpad driver reports the paddles on onboard profile 0 only (Linux 6.1 and later); on profiles "
           "1-3 they arrive as the buttons the profile assigns. Switch to profile 0 (the profile LED off) to use them "
           "separately, or connect by Bluetooth with the udev rule installed.";
}

void HidrawSourceMerge::Control(Source source, const std::string& control, float value, const ControlSink& sink)
{
    if (source != _active)
        return;
    if (value != 0.0f)
        _held[control] = value;
    else
        _held.erase(control);
    sink(control, value);
}

void HidrawSourceMerge::Switch(Source to, const ControlSink& sink)
{
    if (to == _active)
        return;
    _active = to;
    std::map<std::string, float> held;
    held.swap(_held);
    for (const auto& entry : held)
        sink(entry.first, 0.0f);
}

}  // namespace controllers
