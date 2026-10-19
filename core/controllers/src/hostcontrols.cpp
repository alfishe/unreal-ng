#include "hostcontrols.h"

#include <algorithm>
#include <cctype>

#include "emulator/io/controllers/controllertypes.h"

namespace controllers
{

namespace
{
struct FixedControl
{
    const char* name;
    ControlType type;
};

constexpr FixedControl kFixed[] = {
    {"south", ControlType::Button},        {"east", ControlType::Button},         {"west", ControlType::Button},
    {"north", ControlType::Button},        {"dpad.up", ControlType::Button},      {"dpad.down", ControlType::Button},
    {"dpad.left", ControlType::Button},    {"dpad.right", ControlType::Button},   {"lb", ControlType::Button},
    {"rb", ControlType::Button},           {"lt", ControlType::Trigger},          {"rt", ControlType::Trigger},
    {"lstick.x", ControlType::Axis},       {"lstick.y", ControlType::Axis},       {"rstick.x", ControlType::Axis},
    {"rstick.y", ControlType::Axis},       {"lstick.click", ControlType::Button}, {"rstick.click", ControlType::Button},
    {"view", ControlType::Button},         {"menu", ControlType::Button},         {"guide", ControlType::Button},
    {"share", ControlType::Button},        {"profile", ControlType::Button},      {"paddle.p1", ControlType::Button},
    {"paddle.p2", ControlType::Button},    {"paddle.p3", ControlType::Button},    {"paddle.p4", ControlType::Button},
    {"misc.1", ControlType::Button},       {"misc.2", ControlType::Button},       {"misc.3", ControlType::Button},
    {"misc.4", ControlType::Button},       {"misc.5", ControlType::Button},       {"misc.6", ControlType::Button},
    {"touchpad.click", ControlType::Button}, {"touchpad.x", ControlType::Axis},   {"touchpad.y", ControlType::Axis},
    {"gyro.x", ControlType::Axis},         {"gyro.y", ControlType::Axis},         {"gyro.z", ControlType::Axis},
    {"accel.x", ControlType::Axis},        {"accel.y", ControlType::Axis},        {"accel.z", ControlType::Axis},
    {"mouse.dx", ControlType::Delta},      {"mouse.dy", ControlType::Delta},      {"mouse.wheel", ControlType::Delta},
    {"mouse.left", ControlType::Button},   {"mouse.right", ControlType::Button},  {"mouse.middle", ControlType::Button},
    {"mouse.x4", ControlType::Button},     {"mouse.x5", ControlType::Button},     {"pointer.x", ControlType::Pointer},
    {"pointer.y", ControlType::Pointer},   {"knob", ControlType::Encoder},        {"push", ControlType::Button},
    {"touch", ControlType::Button},
};

struct Direction
{
    const char* name;
    uint16_t bit;
};
constexpr Direction kDirections[] = {{"up", kUp}, {"down", kDown}, {"left", kLeft}, {"right", kRight}};

struct RetroControl
{
    const char* name;
    uint16_t mask;
};
// Bits 0-7 are the Kempston byte (R L D U F F2 F3 F4); an MD pad names bits 4-7 B C A START and adds Y Z X MODE in
// bits 8-11 (NextExtendedInput's 12-bit pad state)
constexpr RetroControl kRetroControls[] = {
    {"right", kRight}, {"left", kLeft},   {"down", kDown},    {"up", kUp},      {"fire", kFire},
    {"fire2", kFire2}, {"fire3", kFire3}, {"fire4", kFire4},  {"b", kFire},     {"c", kFire2},
    {"a", kFire3},     {"start", kFire4}, {"y", 0x0100},      {"z", 0x0200},    {"x", 0x0400},
    {"mode", 0x0800},  {"dpad", static_cast<uint16_t>(kRight | kLeft | kDown | kUp)},
};

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool StartsWith(const std::string& text, const char* prefix)
{
    return text.rfind(prefix, 0) == 0;
}

bool AllDigits(const std::string& text)
{
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

bool DirectionBit(const std::string& name, uint16_t& bit)
{
    for (const Direction& direction : kDirections)
    {
        if (name == direction.name)
        {
            bit = direction.bit;
            return true;
        }
    }
    return false;
}
}  // namespace

bool ParseControl(const std::string& text, ControlInfo& out)
{
    const std::string name = Lower(text);
    out = ControlInfo{};
    out.name = name;
    for (const FixedControl& control : kFixed)
    {
        if (name == control.name)
        {
            out.type = control.type;
            return true;
        }
    }

    // Derived digital: a stick direction or a trigger's click
    if (name == "lt.click" || name == "rt.click")
    {
        out.base = name.substr(0, 2);
        return true;
    }
    for (const char* stick : {"lstick.", "rstick."})
    {
        if (StartsWith(name, stick) && DirectionBit(name.substr(7), out.direction))
        {
            out.base = name.substr(0, 6);
            return true;
        }
    }

    // Raw controls of a device with no known layout
    if (StartsWith(name, "button."))
        return AllDigits(name.substr(7));
    if (StartsWith(name, "axis."))
    {
        out.type = ControlType::Axis;
        return AllDigits(name.substr(5));
    }
    if (StartsWith(name, "hat."))
    {
        const size_t dot = name.find('.', 4);
        return dot != std::string::npos && AllDigits(name.substr(4, dot - 4)) &&
               DirectionBit(name.substr(dot + 1), out.direction);
    }
    if (StartsWith(name, "key."))
        return name.size() > 4;
    return false;
}

std::string KeyControl(const std::string& key, uint8_t modifiers)
{
    std::string name = "key.";
    if (modifiers & kKeyCtrl)
        name += "ctrl.";
    if (modifiers & kKeyAlt)
        name += "alt.";
    if (modifiers & kKeyShift)
        name += "shift.";
    if (modifiers & kKeyMeta)
        name += "meta.";
    return name + Lower(key);
}

const std::vector<std::string>& KnownControls()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> list;
        for (const FixedControl& control : kFixed)
            list.emplace_back(control.name);
        return list;
    }();
    return names;
}

std::string StickOf(const std::string& name)
{
    const std::string lower = Lower(name);
    return lower == "lstick" || lower == "rstick" ? lower : std::string{};
}

bool RetroControlMask(const std::string& name, uint16_t& mask)
{
    const std::string lower = Lower(name);
    for (const RetroControl& control : kRetroControls)
    {
        if (lower == control.name)
        {
            mask = control.mask;
            return true;
        }
    }
    return false;
}

const std::vector<std::string>& KnownRetroControls()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> list;
        for (const RetroControl& control : kRetroControls)
            list.emplace_back(control.name);
        return list;
    }();
    return names;
}

}  // namespace controllers
