#include "deviceimport.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace controllers
{

namespace
{
std::string Trim(const std::string& text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string Hex4(unsigned value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "%04x", value & 0xFFFFu);
    return text;
}

/// The canonical controls in the order the dialog draws them
const std::vector<std::string>& ControlOrder()
{
    static const std::vector<std::string> order = {
        "dpad.up", "dpad.down", "dpad.left", "dpad.right", "south", "east", "west", "north", "lb", "rb", "lt", "rt",
        "lstick", "rstick", "lstick.click", "rstick.click", "view", "menu", "guide", "share", "paddle.p1",
        "paddle.p2", "paddle.p3", "paddle.p4", "touchpad.click"};
    return order;
}

/// The family entry for @p vendor:@p product with the buttons by index and the controls the source names
JsonValue Entry(uint16_t vendor, uint16_t product, const std::string& bus, const std::string& name,
                const std::map<unsigned, std::string>& buttons, const std::set<std::string>& controls,
                const std::string& source)
{
    std::vector<JsonValue> list;
    unsigned top = 0;
    for (const auto& [index, control] : buttons)
        top = std::max(top, index + 1);
    for (unsigned i = 0; i < top; i++)
    {
        const auto it = buttons.find(i);
        list.push_back(JsonValue::String(it == buttons.end() ? std::string{} : it->second));
    }
    std::vector<JsonValue> names;
    for (const std::string& control : ControlOrder())
        if (controls.count(control) != 0)
            names.push_back(JsonValue::String(control));
    JsonValue match = JsonValue::Object({{"vid", JsonValue::String(Hex4(vendor))},
                                         {"pid", JsonValue::Array({JsonValue::String(Hex4(product))})}});
    if (!bus.empty())
        match.Set("bus", JsonValue::String(bus));
    return JsonValue::Object({{"id", JsonValue::String("user-" + Hex4(vendor) + Hex4(product))},
                              {"name", JsonValue::String(name)},
                              {"layout", JsonValue::String("generic")},
                              {"match", JsonValue::Array({std::move(match)})},
                              {"buttons", JsonValue::Array(std::move(list))},
                              {"controls", JsonValue::Array(std::move(names))},
                              {"source", JsonValue::String(source)}});
}

/// "b12" -> 12
bool ButtonIndex(const std::string& value, unsigned& index)
{
    if (value.size() < 2 || value[0] != 'b' || !std::all_of(value.begin() + 1, value.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        }))
        return false;
    index = static_cast<unsigned>(std::strtoul(value.c_str() + 1, nullptr, 10));
    return index < 128;
}

/// Little-endian 16 bits at byte @p at of a GUID's hex text
unsigned GuidWord(const std::string& guid, size_t at)
{
    const unsigned lo = static_cast<unsigned>(std::strtoul(guid.substr(at * 2, 2).c_str(), nullptr, 16));
    const unsigned hi = static_cast<unsigned>(std::strtoul(guid.substr(at * 2 + 2, 2).c_str(), nullptr, 16));
    return lo | (hi << 8);
}
}  // namespace

bool ImportGameControllerDb(const std::string& line, DeviceImport& out, std::string& error)
{
    std::vector<std::string> fields;
    std::stringstream stream(Trim(line));
    for (std::string field; std::getline(stream, field, ',');)
        fields.push_back(Trim(field));
    if (fields.size() < 3)
    {
        error = "a gamecontrollerdb line is GUID,name,mapping,... (" + std::to_string(fields.size()) + " fields)";
        return false;
    }
    const std::string& guid = fields[0];
    if (guid.size() != 32 || !std::all_of(guid.begin(), guid.end(), [](unsigned char c) { return std::isxdigit(c); }))
    {
        error = "the GUID is 32 hex digits ('" + guid + "')";
        return false;
    }
    // SDL 2.0.5+: bus, CRC16 of the name, vendor, 0, product, 0, version, driver (little-endian words)
    const unsigned bus = GuidWord(guid, 0);
    const unsigned vendor = GuidWord(guid, 4);
    const unsigned product = GuidWord(guid, 8);
    if (GuidWord(guid, 6) != 0 || GuidWord(guid, 10) != 0 || vendor == 0)
    {
        error = "the GUID carries no USB ids (a name-based GUID): the entry could not be matched to a device";
        return false;
    }
    static const std::map<std::string, std::string> kNames = {
        {"a", "south"},           {"b", "east"},          {"x", "west"},           {"y", "north"},
        {"back", "view"},         {"start", "menu"},      {"guide", "guide"},      {"leftshoulder", "lb"},
        {"rightshoulder", "rb"},  {"leftstick", "lstick.click"}, {"rightstick", "rstick.click"},
        {"dpup", "dpad.up"},      {"dpdown", "dpad.down"}, {"dpleft", "dpad.left"}, {"dpright", "dpad.right"},
        {"misc1", "share"},       {"paddle1", "paddle.p1"}, {"paddle2", "paddle.p2"}, {"paddle3", "paddle.p3"},
        {"paddle4", "paddle.p4"}, {"touchpad", "touchpad.click"}, {"lefttrigger", "lt"}, {"righttrigger", "rt"}};
    std::map<unsigned, std::string> buttons;
    std::set<std::string> controls;
    out = DeviceImport{};
    out.source = "gamecontrollerdb";
    for (size_t i = 2; i < fields.size(); i++)
    {
        const size_t colon = fields[i].find(':');
        if (colon == std::string::npos)
            continue;
        const std::string key = fields[i].substr(0, colon);
        const std::string value = fields[i].substr(colon + 1);
        if (key == "platform" || key == "crc" || key == "hint" || key == "sdk>=" || key == "sdk<=")
            continue;
        // Half axes and inverted ones (+a1, -a1, a1~) are axes too
        if (key == "leftx" || key == "lefty")
            controls.insert("lstick");
        else if (key == "rightx" || key == "righty")
            controls.insert("rstick");
        const auto name = kNames.find(key);
        if (name != kNames.end())
        {
            // A button index is taken over; a hat (h0.1) or an axis (a2) is named by its HID usage already
            controls.insert(name->second);
            unsigned index = 0;
            if (ButtonIndex(value, index))
                buttons[index] = name->second;
            continue;
        }
        if (key != "leftx" && key != "lefty" && key != "rightx" && key != "righty")
            out.skipped.push_back(fields[i]);
    }
    if (buttons.empty())
    {
        error = "the line maps no button (bN) to a control";
        return false;
    }
    const std::string busName = bus == 0x03 ? "usb" : bus == 0x05 ? "bt" : "";
    out.entry = Entry(static_cast<uint16_t>(vendor), static_cast<uint16_t>(product), busName, fields[1], buttons,
                      controls, out.source);
    return true;
}

bool ImportRetroArch(const std::string& text, DeviceImport& out, std::string& error)
{
    std::map<std::string, std::string> values;
    std::stringstream stream(text);
    for (std::string line; std::getline(stream, line);)
    {
        line = Trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        std::string value = Trim(line.substr(equals + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        values[Trim(line.substr(0, equals))] = value;
    }
    const auto number = [&values](const char* key, unsigned& result) {
        const auto it = values.find(key);
        if (it == values.end() || it->second.empty())
            return false;
        char* end = nullptr;
        result = static_cast<unsigned>(std::strtoul(it->second.c_str(), &end, 10));
        return *end == '\0';
    };
    unsigned vendor = 0;
    unsigned product = 0;
    if (!number("input_vendor_id", vendor) || !number("input_product_id", product) || vendor == 0 || vendor > 0xFFFF ||
        product > 0xFFFF)
    {
        error = "a RetroArch autoconfig needs input_vendor_id and input_product_id (decimal) to match the device";
        return false;
    }
    // RetroPad names: B is the bottom face button, A the right one, Y the left one, X the top one
    static const std::map<std::string, std::string> kNames = {
        {"b", "south"},  {"a", "east"},           {"y", "west"},          {"x", "north"},       {"l", "lb"},
        {"r", "rb"},     {"l2", "lt"},            {"r2", "rt"},           {"l3", "lstick.click"},
        {"r3", "rstick.click"}, {"select", "view"}, {"start", "menu"},     {"menu_toggle", "guide"},
        {"up", "dpad.up"}, {"down", "dpad.down"}, {"left", "dpad.left"},  {"right", "dpad.right"}};
    std::map<unsigned, std::string> buttons;
    std::set<std::string> controls;
    out = DeviceImport{};
    out.source = "retroarch";
    for (const auto& [key, value] : values)
    {
        if (key.rfind("input_", 0) != 0)
            continue;
        if (key.rfind("input_l_x", 0) == 0 || key.rfind("input_l_y", 0) == 0)
            controls.insert("lstick");
        if (key.rfind("input_r_x", 0) == 0 || key.rfind("input_r_y", 0) == 0)
            controls.insert("rstick");
        const bool button = key.size() > 4 && key.compare(key.size() - 4, 4, "_btn") == 0;
        const bool axis = key.size() > 5 && key.compare(key.size() - 5, 5, "_axis") == 0;
        if (!button && !axis)
            continue;
        const std::string what = key.substr(6, key.size() - 6 - (button ? 4 : 5));
        const auto name = kNames.find(what);
        if (name == kNames.end())
        {
            if (axis && (what.rfind("l_", 0) == 0 || what.rfind("r_", 0) == 0))
                continue;  // stick axes: the HID usages name them
            out.skipped.push_back(key);
            continue;
        }
        // A button index is taken over; a hat direction (h0up) or an axis is named by its HID usage already
        controls.insert(name->second);
        if (button && !value.empty() &&
            std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        {
            const unsigned index = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
            if (index < 128)
                buttons[index] = name->second;
        }
    }
    if (buttons.empty())
    {
        error = "the autoconfig maps no button (input_*_btn = N) to a control";
        return false;
    }
    const auto device = values.find("input_device");
    const std::string name = device != values.end() && !device->second.empty() ? device->second
                                                                               : "USB " + Hex4(vendor) + ":" + Hex4(product);
    out.entry = Entry(static_cast<uint16_t>(vendor), static_cast<uint16_t>(product), "", name, buttons, controls,
                      out.source);
    return true;
}

bool ImportDevice(const std::string& text, DeviceImport& out, std::string& error)
{
    std::stringstream stream(text);
    for (std::string line; std::getline(stream, line);)
    {
        line = Trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (line.rfind("input_", 0) == 0 && line.find('=') != std::string::npos)
            return ImportRetroArch(text, out, error);
        return ImportGameControllerDb(line, out, error);
    }
    error = "nothing to import: paste a gamecontrollerdb line or a RetroArch autoconfig file";
    return false;
}

}  // namespace controllers
