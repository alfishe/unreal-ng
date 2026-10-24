#include "devicemapping.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "hostcontrols.h"

namespace controllers
{

namespace
{
constexpr float kPressed = 0.5f;
constexpr float kMoved = 0.4f;     ///< ui-ux.md §3.5: an axis moved more than 0.4 from its rest answers
constexpr float kReturned = 0.2f;  ///< and is back within 0.2 of it before the next prompt

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Trim(const std::string& text)
{
    size_t start = 0;
    size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start])))
        ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(start, end - start);
}

std::string Hex4(uint16_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "%04x", static_cast<unsigned>(value));
    return text;
}

bool HexValue(const std::string& text, unsigned long& out)
{
    if (text.empty() || text.size() > 8)
        return false;
    char* end = nullptr;
    out = std::strtoul(text.c_str(), &end, 16);
    return *end == '\0';
}

/// A raw spec into the control and the half; false when the control is not one the backends report
bool ParseRaw(const std::string& spec, std::string& raw, int& half)
{
    raw = Lower(Trim(spec));
    half = 0;
    if (!raw.empty() && (raw.back() == '+' || raw.back() == '-'))
    {
        half = raw.back() == '+' ? 1 : -1;
        raw.pop_back();
    }
    ControlInfo info;
    if (!ParseControl(raw, info) || !info.base.empty())
        return false;
    if (half != 0 && info.type != ControlType::Axis)
        return false;
    return info.type == ControlType::Button || info.type == ControlType::Axis || info.type == ControlType::Trigger;
}

/// The SDL names of the canonical controls (SDL_gamecontroller.c's map_StringForControllerButton / Axis)
const std::vector<std::pair<const char*, const char*>>& SdlNames()
{
    static const std::vector<std::pair<const char*, const char*>> names = {
        {"a", "south"},           {"b", "east"},          {"x", "west"},           {"y", "north"},
        {"back", "view"},         {"start", "menu"},      {"guide", "guide"},      {"leftshoulder", "lb"},
        {"rightshoulder", "rb"},  {"lefttrigger", "lt"},  {"righttrigger", "rt"},  {"leftstick", "lstick.click"},
        {"rightstick", "rstick.click"}, {"dpup", "dpad.up"}, {"dpdown", "dpad.down"}, {"dpleft", "dpad.left"},
        {"dpright", "dpad.right"}, {"leftx", "lstick.x"},  {"lefty", "lstick.y"},   {"rightx", "rstick.x"},
        {"righty", "rstick.y"},   {"misc1", "share"},     {"paddle1", "paddle.p1"}, {"paddle2", "paddle.p2"},
        {"paddle3", "paddle.p3"}, {"paddle4", "paddle.p4"}, {"touchpad", "touchpad.click"},
    };
    return names;
}

/// RetroArch's RetroPad names (input_<name>_btn / _axis): the SNES layout, B at the bottom
const std::vector<std::pair<const char*, const char*>>& RetroArchNames()
{
    static const std::vector<std::pair<const char*, const char*>> names = {
        {"b", "south"},       {"a", "east"},        {"y", "west"},        {"x", "north"},
        {"l", "lb"},          {"r", "rb"},          {"l2", "lt"},         {"r2", "rt"},
        {"l3", "lstick.click"}, {"r3", "rstick.click"}, {"select", "view"}, {"start", "menu"},
        {"up", "dpad.up"},    {"down", "dpad.down"}, {"left", "dpad.left"}, {"right", "dpad.right"},
        {"menu_toggle", "guide"},
    };
    return names;
}

const char* const kHatDirs[] = {"up", "right", "down", "left"};  // SDL hat bits 1, 2, 4, 8

/// "hN.M" / "hNup" hat N: our first hat reports as the D-pad, the next ones as hat.<n> (HidPad)
std::string HatControl(unsigned hat, const std::string& dir)
{
    return hat == 0 ? "dpad." + dir : "hat." + std::to_string(hat) + "." + dir;
}

/// Axis N as HidPad names it: X, Y, Z, Rz (SDL's a0..a3 in the HID descriptor's usual order) are the sticks, the
/// others axis.<n>. A mapping made on another platform may number them otherwise (Linux evdev puts Rz at a5)
std::string AxisControl(unsigned axis)
{
    static const char* const sticks[] = {"lstick.x", "lstick.y", "rstick.x", "rstick.y"};
    return axis < 4 ? sticks[axis] : "axis." + std::to_string(axis);
}

/// An SDL binding value (b3, a1, +a1, -a1, ~a1, h0.4) as a raw spec; "" when it is none of them
std::string RawFromSdl(const std::string& value)
{
    std::string text = Trim(value);
    if (text.empty())
        return {};
    char sign = 0;
    if (text[0] == '+' || text[0] == '-')
    {
        sign = text[0];
        text.erase(0, 1);
    }
    if (!text.empty() && text.back() == '~')
        text.pop_back();  // an inverted axis: kept as the axis
    if (text.size() < 2)
        return {};
    const char kind = text[0];
    const std::string rest = text.substr(1);
    if (kind == 'h')
    {
        const size_t dot = rest.find('.');
        if (dot == std::string::npos)
            return {};
        const unsigned hat = static_cast<unsigned>(std::atoi(rest.substr(0, dot).c_str()));
        const unsigned mask = static_cast<unsigned>(std::atoi(rest.substr(dot + 1).c_str()));
        for (unsigned bit = 0; bit < 4; ++bit)
            if (mask == (1u << bit))
                return HatControl(hat, kHatDirs[bit]);
        return {};
    }
    if (!std::all_of(rest.begin(), rest.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return {};
    if (kind == 'b' && sign == 0)
        return "button." + rest;
    if (kind == 'a')
        return AxisControl(static_cast<unsigned>(std::atoi(rest.c_str()))) + (sign == '+' ? "+" : sign == '-' ? "-" : "");
    return {};
}

/// A raw spec as SDL writes it; "" when SDL has no name for it. Our generic HID axes X Y Z Rz report as the sticks:
/// a0..a3 in that order (the descriptor's order, as SDL's HID joystick numbers them)
std::string SdlFromRaw(const std::string& spec)
{
    std::string raw;
    int half = 0;
    if (!ParseRaw(spec, raw, half))
        return {};
    const char* sign = half > 0 ? "+" : half < 0 ? "-" : "";
    if (raw.rfind("button.", 0) == 0)
        return half == 0 ? "b" + raw.substr(7) : std::string();
    if (raw.rfind("axis.", 0) == 0)
        return sign + std::string("a") + raw.substr(5);
    static const char* const sticks[] = {"lstick.x", "lstick.y", "rstick.x", "rstick.y"};
    for (unsigned axis = 0; axis < 4; ++axis)
        if (raw == sticks[axis])
            return sign + std::string("a") + std::to_string(axis);
    for (unsigned bit = 0; bit < 4; ++bit)
    {
        if (raw == std::string("dpad.") + kHatDirs[bit])
            return "h0." + std::to_string(1u << bit);
        if (raw.rfind("hat.", 0) == 0 && raw.size() > 4)
        {
            const size_t dot = raw.find('.', 4);
            if (dot != std::string::npos && raw.substr(dot + 1) == kHatDirs[bit])
                return "h" + std::to_string(std::atoi(raw.substr(4, dot - 4).c_str())) + "." +
                       std::to_string(1u << bit);
        }
    }
    return {};
}

bool KnownCanonical(const std::string& name)
{
    ControlInfo info;
    return ParseControl(name, info) && info.base.empty() && info.name.rfind("button.", 0) != 0 &&
           info.name.rfind("axis.", 0) != 0 && info.name.rfind("hat.", 0) != 0 && info.name.rfind("key.", 0) != 0;
}
}  // namespace

bool ParseControlMap(const std::map<std::string, std::string>& map, std::vector<MappedControl>& out,
                     std::string& error)
{
    out.clear();
    for (const auto& [canonical, spec] : map)
    {
        if (!KnownCanonical(canonical))
        {
            error = "map: '" + canonical + "' is not a canonical control (south, dpad.up, lstick.x, ...)";
            return false;
        }
        MappedControl control;
        control.canonical = canonical;
        if (!ParseRaw(spec, control.raw, control.half))
        {
            error = "map: " + canonical + ": '" + spec + "' is not a raw control (button.<n>, axis.<n>, axis.<n>+, "
                                                         "axis.<n>-, hat.<n>.<dir>, a control name)";
            return false;
        }
        out.push_back(control);
    }
    return true;
}

std::vector<ControlEvent> TranslateControl(const std::vector<MappedControl>& map, const ControlEvent& event)
{
    std::vector<ControlEvent> out;
    for (const MappedControl& control : map)
    {
        if (control.raw != event.control || (control.half == 0 && control.canonical == control.raw))
            continue;  // not this control, or already its canonical name
        ControlEvent mapped = event;
        mapped.control = control.canonical;
        if (control.half != 0)
            mapped.value = std::max(0.0f, event.value * static_cast<float>(control.half));
        out.push_back(std::move(mapped));
    }
    return out;
}

std::string SdlGuid(const DeviceIdentity& identity)
{
    if (identity.vendor == 0 && identity.product == 0)
        return {};
    const uint16_t bus = identity.bus == "bt" ? 0x0005 : 0x0003;  // SDL_HARDWARE_BUS_BLUETOOTH / USB
    const uint16_t version = identity.version >= 0 ? static_cast<uint16_t>(identity.version) : 0;
    auto le = [](uint16_t value) {
        char text[8];
        std::snprintf(text, sizeof(text), "%02x%02x", static_cast<unsigned>(value & 0xFF),
                      static_cast<unsigned>(value >> 8));
        return std::string(text);
    };
    return le(bus) + "0000" + le(identity.vendor) + "0000" + le(identity.product) + "0000" + le(version) + "0000";
}

bool IdentityFromSdlGuid(const std::string& guid, DeviceIdentity& out)
{
    if (guid.size() != 32 || !std::all_of(guid.begin(), guid.end(), [](unsigned char c) { return std::isxdigit(c); }))
        return false;
    auto word = [&guid](size_t offset) {
        unsigned long lo = 0;
        unsigned long hi = 0;
        HexValue(guid.substr(offset, 2), lo);
        HexValue(guid.substr(offset + 2, 2), hi);
        return static_cast<uint16_t>(lo | (hi << 8));
    };
    const uint16_t bus = word(0);
    out = DeviceIdentity{};
    out.bus = bus == 0x0005 ? "bt" : bus == 0x0003 ? "usb" : "";
    out.vendor = word(8);
    out.product = word(16);
    out.version = word(24);
    // SDL writes a name hash instead when it has no VID / PID (bytes 4..7 not zero)
    return out.vendor != 0 && guid.substr(12, 4) == "0000";
}

bool IdentityFromDeviceId(const HostDeviceId& id, DeviceIdentity& out)
{
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(id);
    while (std::getline(in, part, ':'))
        parts.push_back(part);
    if (parts.size() < 3 || (parts[0] != "usb" && parts[0] != "bt"))
        return false;
    unsigned long vendor = 0;
    unsigned long product = 0;
    if (parts[1].size() != 4 || parts[2].size() != 4 || !HexValue(parts[1], vendor) || !HexValue(parts[2], product))
        return false;
    out = DeviceIdentity{};
    out.bus = parts[0];
    out.vendor = static_cast<uint16_t>(vendor);
    out.product = static_cast<uint16_t>(product);
    unsigned long version = 0;
    // usb:<vid>:<pid>:<version>:<serial> or usb:<vid>:<pid>:<version>@<location hash>
    if (parts.size() > 3 && parts[0] == "usb" && HexValue(parts[3].substr(0, 4), version) &&
        (parts[3].size() == 4 || parts[3][4] == '@'))
        out.version = static_cast<int>(version);
    return true;
}

bool ParseVidPid(const std::string& text, uint16_t& vendor, uint16_t& product)
{
    const size_t colon = text.find(':');
    unsigned long v = 0;
    unsigned long p = 0;
    if (colon == std::string::npos || colon == 0 || colon > 4 || text.size() - colon - 1 > 4 ||
        !HexValue(text.substr(0, colon), v) || !HexValue(text.substr(colon + 1), p))
        return false;
    vendor = static_cast<uint16_t>(v);
    product = static_cast<uint16_t>(p);
    return true;
}

bool ImportMapping(const std::string& text, ImportedMapping& out, std::string& error)
{
    out = ImportedMapping{};
    const std::string trimmed = Trim(text);
    if (trimmed.empty())
    {
        error = "nothing to import: a gamecontrollerdb.txt line or a RetroArch autoconfig file";
        return false;
    }
    if (trimmed.find("input_") != std::string::npos && trimmed.find('=') != std::string::npos)
    {
        // RetroArch autoconfig: key = "value" lines
        out.source = "retroarch";
        std::istringstream in(trimmed);
        std::string line;
        unsigned long vendor = 0;
        unsigned long product = 0;
        bool haveVendor = false;
        bool haveProduct = false;
        while (std::getline(in, line))
        {
            line = Trim(line);
            const size_t eq = line.find('=');
            if (line.empty() || line[0] == '#' || eq == std::string::npos)
                continue;
            const std::string key = Trim(line.substr(0, eq));
            std::string value = Trim(line.substr(eq + 1));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            if (key == "input_device")
                out.name = value;
            else if (key == "input_vendor_id")
                haveVendor = !value.empty() && (vendor = std::strtoul(value.c_str(), nullptr, 10), true);
            else if (key == "input_product_id")
                haveProduct = !value.empty() && (product = std::strtoul(value.c_str(), nullptr, 10), true);
            else if (key.rfind("input_", 0) == 0 && (key.size() > 4) &&
                     (key.compare(key.size() - 4, 4, "_btn") == 0 || key.compare(key.size() - 5, 5, "_axis") == 0))
            {
                const bool axis = key.compare(key.size() - 5, 5, "_axis") == 0;
                const std::string name = key.substr(6, key.size() - 6 - (axis ? 5 : 4));
                std::string canonical;
                std::string raw;
                for (const auto& [retro, ours] : RetroArchNames())
                    if (name == retro)
                        canonical = ours;
                // The analog sticks: l_x_plus / l_x_minus .. one canonical axis each
                for (const char* stick : {"l", "r"})
                    for (const char* coord : {"x", "y"})
                        for (const char* end : {"_plus", "_minus"})
                            if (name == std::string(stick) + "_" + coord + end)
                                canonical = std::string(stick) + "stick." + coord;
                if (axis && value.size() >= 2 && (value[0] == '+' || value[0] == '-') &&
                    std::all_of(value.begin() + 1, value.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
                    raw = AxisControl(static_cast<unsigned>(std::atoi(value.c_str() + 1))) +
                          (canonical.find("stick.") != std::string::npos ? "" : value.substr(0, 1));
                else if (!axis && value.size() > 1 && value[0] == 'h')
                {
                    const size_t dir = value.find_first_not_of("0123456789", 1);
                    if (dir != std::string::npos && dir > 1)
                        raw = HatControl(static_cast<unsigned>(std::atoi(value.substr(1, dir - 1).c_str())),
                                         value.substr(dir));
                }
                else if (!axis && !value.empty() &&
                         std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
                    raw = "button." + value;
                if (canonical.empty() || raw.empty())
                {
                    if (value != "nul" && !value.empty())
                        out.skipped.push_back(key + " = " + value);
                    continue;
                }
                out.map[canonical] = raw;
            }
        }
        if (!haveVendor || !haveProduct || vendor > 0xFFFF || product > 0xFFFF)
        {
            error = "the RetroArch file names no input_vendor_id and input_product_id (decimal)";
            return false;
        }
        out.identity.vendor = static_cast<uint16_t>(vendor);
        out.identity.product = static_cast<uint16_t>(product);
    }
    else
    {
        // gamecontrollerdb.txt: GUID,name,mapping,...,platform:<os>,
        out.source = "gamecontrollerdb";
        std::vector<std::string> fields;
        std::string field;
        std::istringstream in(trimmed.substr(0, trimmed.find('\n')));
        while (std::getline(in, field, ','))
            fields.push_back(Trim(field));
        if (fields.size() < 3 || !IdentityFromSdlGuid(Lower(fields[0]), out.identity))
        {
            error = "a gamecontrollerdb.txt line starts with the 32-digit SDL GUID of a VID / PID device, then the "
                    "name, then the mapping (a:b0,b:b1,...)";
            return false;
        }
        out.name = fields[1];
        for (size_t i = 2; i < fields.size(); ++i)
        {
            const size_t colon = fields[i].find(':');
            if (fields[i].empty() || colon == std::string::npos)
                continue;
            std::string name = fields[i].substr(0, colon);
            const std::string value = fields[i].substr(colon + 1);
            if (name == "platform" || name == "crc" || name == "hint" || name == "sdk>=" || name == "sdk<=")
                continue;
            int half = 0;  // +leftx / -leftx: a half of an output axis
            if (!name.empty() && (name[0] == '+' || name[0] == '-'))
            {
                half = name[0] == '+' ? 1 : -1;
                name.erase(0, 1);
            }
            std::string canonical;
            for (const auto& [sdl, ours] : SdlNames())
                if (name == sdl)
                    canonical = ours;
            const std::string raw = RawFromSdl(value);
            if (canonical.empty() || raw.empty() || half != 0 || !KnownCanonical(canonical))
            {
                out.skipped.push_back(fields[i]);
                continue;
            }
            out.map[canonical] = raw;
        }
    }
    if (out.map.empty())
    {
        error = "the " + out.source + " mapping names no control this emulator reads";
        return false;
    }
    return true;
}

std::string GameControllerDbLine(const DeviceIdentity& identity, const std::string& name,
                                 const std::map<std::string, std::string>& map, std::vector<std::string>* skipped)
{
    std::string line = SdlGuid(identity) + ",";
    for (char c : name)
        line += c == ',' ? ' ' : c;
    line += ",";
    for (const auto& [sdl, ours] : SdlNames())
    {
        const auto it = map.find(ours);
        if (it == map.end())
            continue;
        const std::string value = SdlFromRaw(it->second);
        if (value.empty())
        {
            if (skipped != nullptr)
                skipped->push_back(it->first + ":" + it->second);
            continue;
        }
        line += std::string(sdl) + ":" + value + ",";
    }
    return line;
}

std::string UserFamilyId(const DeviceIdentity& identity)
{
    return "user-" + Hex4(identity.vendor) + Hex4(identity.product);
}

JsonValue UserFamilyJson(const DeviceIdentity& identity, const std::string& name,
                         const std::map<std::string, std::string>& map, const std::string& source)
{
    JsonValue match = JsonValue::Object({{"vid", JsonValue::String(Hex4(identity.vendor))},
                                         {"pid", JsonValue::Array({JsonValue::String(Hex4(identity.product))})}});
    std::vector<JsonValue> controls;
    JsonValue mapping = JsonValue::Object();
    for (const auto& [canonical, raw] : map)
    {
        controls.push_back(JsonValue::String(canonical));
        mapping.Set(canonical, JsonValue::String(raw));
    }
    JsonValue family = JsonValue::Object({{"id", JsonValue::String(UserFamilyId(identity))}});
    family.Set("name", JsonValue::String(name.empty() ? "Device " + Hex4(identity.vendor) + ":" + Hex4(identity.product)
                                                      : name));
    family.Set("match", JsonValue::Array({match}));
    family.Set("layout", JsonValue::String("generic"));
    family.Set("controls", JsonValue::Array(std::move(controls)));
    family.Set("map", mapping);
    family.Set("source", JsonValue::String(source));
    family.Set("gamecontrollerdb", JsonValue::String(GameControllerDbLine(identity, name, map, nullptr)));
    return family;
}

bool MergeUserFamily(const std::string& text, const JsonValue& family, std::string& out, std::string& error)
{
    JsonValue root = JsonValue::Object({{"schema", JsonValue::Number(1)}, {"families", JsonValue::Array()}});
    if (!Trim(text).empty())
    {
        if (!ParseJson(text, root, error))
            return false;
        const JsonValue* schema = root.Find("schema");
        if (!root.IsObject() || schema == nullptr || !schema->IsNumber() || schema->AsNumber() != 1)
        {
            error = "devices.user.json is not a device database ('schema': 1)";
            return false;
        }
    }
    const JsonValue* id = family.Find("id");
    std::vector<JsonValue> families;
    bool replaced = false;
    if (const JsonValue* list = root.Find("families"); list != nullptr && list->IsArray())
        for (const JsonValue& entry : list->Items())
        {
            const JsonValue* other = entry.Find("id");
            if (other != nullptr && id != nullptr && other->IsString() && other->AsString() == id->AsString())
            {
                families.push_back(family);
                replaced = true;
            }
            else
                families.push_back(entry);
        }
    if (!replaced)
        families.push_back(family);
    root.Set("families", JsonValue::Array(std::move(families)));
    // The merged text must load: the database reads it at the next start
    DeviceDatabase check;
    if (!check.Load(root.Dump(2), error))
        return false;
    out = root.Dump(2) + "\n";
    return true;
}

/// region <LearnSession>

const std::vector<std::string>& LearnSession::DefaultControls()
{
    static const std::vector<std::string> controls = {"dpad.up", "dpad.down", "dpad.left", "dpad.right", "south",
                                                      "east",    "west",      "north",     "lb",         "rb",
                                                      "lt",      "rt",        "view",      "menu"};
    return controls;
}

std::string LearnSession::Prompt(const std::string& control)
{
    static const std::map<std::string, std::string> prompts = {
        {"dpad.up", "Press Up"},
        {"dpad.down", "Press Down"},
        {"dpad.left", "Press Left"},
        {"dpad.right", "Press Right"},
        {"south", "Press South (the bottom face button; Fire on a joystick)"},
        {"east", "Press East (the right face button)"},
        {"west", "Press West (the left face button)"},
        {"north", "Press North (the top face button)"},
        {"lb", "Press the left shoulder button"},
        {"rb", "Press the right shoulder button"},
        {"lt", "Press the left trigger"},
        {"rt", "Press the right trigger"},
        {"view", "Press View / Select / Back"},
        {"menu", "Press Menu / Start"},
        {"guide", "Press the Guide / Home button"},
    };
    const auto it = prompts.find(control);
    return it != prompts.end() ? it->second : "Press " + control;
}

bool LearnSession::Make(const std::string& controls, LearnSession& out, std::string& error)
{
    if (Trim(controls).empty())
    {
        out = LearnSession();
        return true;
    }
    std::vector<std::string> list;
    std::string item;
    std::istringstream in(controls);
    while (std::getline(in, item, ','))
    {
        item = Lower(Trim(item));
        if (item.empty())
            continue;
        if (!KnownCanonical(item))
        {
            error = "learn: '" + item + "' is not a canonical control (dpad.up, south, lb, lstick.x, ...)";
            return false;
        }
        if (std::find(list.begin(), list.end(), item) == list.end())
            list.push_back(item);
    }
    if (list.empty())
    {
        error = "learn: no controls to ask for";
        return false;
    }
    out = LearnSession(std::move(list));
    return true;
}

void LearnSession::Baseline(const std::map<std::string, float>& live)
{
    _baseline = live;
}

float LearnSession::Rest(const std::string& control) const
{
    const auto it = _baseline.find(control);
    return it != _baseline.end() ? it->second : 0.0f;
}

bool LearnSession::Feed(const std::string& control, float value)
{
    ControlInfo info;
    if (!ParseControl(control, info) || !info.base.empty())
        return false;
    const bool axis = info.type == ControlType::Axis;
    if (!axis && info.type != ControlType::Button && info.type != ControlType::Trigger)
        return false;  // a mouse delta, a pointer, an encoder: no answer to a prompt
    const float rest = Rest(control);
    if (!_held.empty())
    {
        // The last answer comes back first: a held button, a stick still pushed, answer nothing
        if (control == _held && (axis ? std::fabs(value - rest) < kReturned : value < kPressed))
            _held.clear();
        return false;
    }
    if (Done())
        return false;
    std::string raw;
    if (axis)
    {
        if (std::fabs(value - rest) <= kMoved)
            return false;
        raw = control + (value > rest ? "+" : "-");
    }
    else
    {
        if (value < kPressed || rest >= kPressed)
            return false;
        raw = control;
    }
    // A raw control answers once
    for (const auto& [canonical, taken] : _mapping)
        if (taken == raw)
            return false;
    _mapping[_controls[_index]] = raw;
    _held = control;
    ++_index;
    return true;
}

void LearnSession::Skip()
{
    if (!Done())
    {
        _mapping.erase(_controls[_index]);
        ++_index;
    }
}

void LearnSession::Back()
{
    if (_index == 0)
        return;
    --_index;
    _mapping.erase(_controls[_index]);
    _held.clear();
}

/// endregion </LearnSession>

}  // namespace controllers
