#include "hid/hidrawnode.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include "hid/drivers/dualsenseoutput.h"
#include "hid/drivers/sonypad.h"
#include "hid/drivers/surfacedial.h"
#include "hid/drivers/switchpro.h"
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

LinuxHidDriver LinuxHidDriverOf(const std::string& driver)
{
    if (driver.empty())
        return LinuxHidDriver::Unknown;
    if (driver == "hid-generic")
        return LinuxHidDriver::Generic;
    if (driver == "playstation")
        return LinuxHidDriver::PlayStation;
    if (driver == "sony")
        return LinuxHidDriver::Sony;
    if (driver == "nintendo")
        return LinuxHidDriver::Nintendo;
    if (driver == "microsoft")
        return LinuxHidDriver::Microsoft;
    if (driver == "stadia")
        return LinuxHidDriver::Stadia;
    if (driver == "hid-steam")
        return LinuxHidDriver::Steam;
    return LinuxHidDriver::Other;
}

bool LinuxHidUsageNamed(LinuxHidDriver driver)
{
    return driver == LinuxHidDriver::Generic || driver == LinuxHidDriver::Microsoft ||
           driver == LinuxHidDriver::Stadia;
}

HidrawUse HidrawUseFor(const LinuxPadFacts& facts)
{
    if (SurfaceDial::Handles(facts.vendor, facts.product))
        return HidrawUse::Output;
    if (facts.surface)
        return HidrawUse::Input;
    HidrawUse use = HidrawUse::None;
    // Over USB the Elite is xpad's (a USB driver, no HID device, no hidraw node): its paddles are evdev buttons
    if (facts.bluetooth && facts.family == "xbox-elite-2")
        use = use | HidrawUse::Input;
    if (LinuxRumbleFor(facts.evdevRumble,
                       XboxBluetoothOutput::Handles(facts.bluetooth, facts.vendor, facts.layout, facts.family)) ==
        LinuxRumble::Hidraw)
        use = use | HidrawUse::Rumble;

    const bool sony = SonyPad::ModelOf(facts.vendor, facts.product).has_value();
    const bool nintendo = SwitchProPad::Handles(facts.vendor, facts.product) ||
                          SwitchProPad::IsChargingGrip(facts.vendor, facts.product);
    if (!sony && !nintendo)
    {
        // The HID map's vendor bits (no usage, so no evdev key) come only in the reports
        if (facts.hidExtras && LinuxHidUsageNamed(facts.driver))
            use = use | HidrawUse::Input;
        return use;
    }
    // No driver of its own: the pad's protocol through hidraw, as macOS reads it
    if (facts.driver == LinuxHidDriver::Generic)
        return use | HidrawUse::Protocol;
    if (sony && DualSenseOutput::Handles(facts.vendor, facts.product))
    {
        // hid-playstation drives the motors (compatible vibration), never the trigger effects
        use = use | HidrawUse::Triggers;
        // Its Fn buttons and paddles came to hid-playstation in 7.2 (BTN_TRIGGER_HAPPY1-4); before, the full report
        if (facts.product == 0x0DF2 && !facts.edgeButtons)
            use = use | HidrawUse::Input;
    }
    // hid-nintendo's FF_RUMBLE plays its fixed frequencies; the effect's own need the 0x10 report. The NSO NES, SNES
    // and Genesis pads have no motors
    const std::optional<SwitchLayout> layout = SwitchProPad::LayoutFor(facts.vendor, facts.product, facts.name);
    if (nintendo && (!layout || SwitchProPad::HasRumble(*layout)))
        use = use | HidrawUse::SwitchRumble;
    return use;
}

HidrawUse HidrawUseFor(bool bluetooth, uint16_t vendor, uint16_t product, const std::string& family,
                       const std::string& layout, bool evdevRumble)
{
    LinuxPadFacts facts;
    facts.bluetooth = bluetooth;
    facts.vendor = vendor;
    facts.product = product;
    facts.family = family;
    facts.layout = layout;
    facts.evdevRumble = evdevRumble;
    return HidrawUseFor(facts);
}

bool HidrawUseReads(HidrawUse use)
{
    return HasUse(use, HidrawUse::Input) || HasUse(use, HidrawUse::Protocol);
}

bool HidrawUseWrites(HidrawUse use)
{
    return !HasUse(use, HidrawUse::Input) || HasUse(use, HidrawUse::Protocol);
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
        else if (key == "DRIVER")
            parsed.driver = value;
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

std::string HidrawUnavailable(HidrawUse use, const std::string& node, int error, const std::string& errorText,
                              const std::string& family, bool surface)
{
    std::string what = "The Surface Dial's clicks and detents need";
    std::string without;
    if (HasUse(use, HidrawUse::Input) && surface)
    {
        what = "The control surface's controls need";
        without = " Until then none of them is seen: its evdev node is not read (hid-input loses the return of a "
                  "relative control to rest, such as a shuttle ring's).";
    }
    else if (HasUse(use, HidrawUse::Protocol) && family == "gamecube-adapter")
    {
        what = "The GameCube adapter's controllers need";
        without = " No kernel driver serves the adapter (it has no input device): until then its controllers are not "
                  "seen.";
    }
    else if (HasUse(use, HidrawUse::Protocol))
    {
        what = "The pad's own reports need";
        without = " No kernel driver of its own serves it (hid-generic: hid-playstation or hid-nintendo is not loaded, "
                  "or the kernel predates it). Until then it works from hid-generic's evdev node: the buttons may be "
                  "misnamed and it has no rumble, light or battery.";
    }
    else if (HasUse(use, HidrawUse::Input) && family == "dualsense-edge")
    {
        what = "The Edge's paddles and Fn buttons need";
        without = " Until then they are not seen (hid-playstation reports them through evdev from Linux 7.2).";
    }
    else if (HasUse(use, HidrawUse::Input) && !family.empty() && family != "xbox-elite-2")
    {
        what = "The pad's vendor-defined buttons need";
        without = " Until then they are not seen: hid-input gives bits without a usage no key.";
    }
    else if (HasUse(use, HidrawUse::Input))
    {
        what = "The paddles and the onboard profile need";
        without = " Until then the paddles are not seen (on onboard profiles 1-3 they arrive as the buttons the "
                  "profile assigns).";
    }
    else if (HasUse(use, HidrawUse::Rumble))
    {
        what = "The motors and the impulse triggers need";
        without = " Until then the pad does not rumble: its kernel driver offers no FF_RUMBLE (hid-microsoft or "
                  "xpadneo would).";
    }
    else if (HasUse(use, HidrawUse::Triggers))
    {
        what = "The adaptive triggers need";
        without = " Until then the motors play through hid-playstation (FF_RUMBLE) and the triggers do not resist.";
    }
    else if (HasUse(use, HidrawUse::SwitchRumble))
    {
        what = "The HD rumble (the effects' own frequencies) needs";
        without = " Until then the motors play through hid-nintendo's FF_RUMBLE at its fixed 160 / 320 Hz.";
    }
    if (node.empty())
        return what + " the device's hidraw node, which was not found (is the hidraw module loaded?)." + without;
    if (error == EACCES || error == EPERM)
    {
        const char* access = HasUse(use, HidrawUse::Protocol) ? "read and write"
                             : HidrawUseReads(use)            ? "read"
                                                              : "write";
        return what + " " + access + " access to " + node + ", which this user does not have. " + kHidrawRulesHint +
               "." + without;
    }
    return what + " " + node + ", which did not open (" + errorText + ")." + without;
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
