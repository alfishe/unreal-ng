#include "platform/windowsdevices.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace controllers
{

namespace
{
std::string Lower(const std::string& text)
{
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

int HexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/// The hex number after @p key in the lower-cased @p path (`vid_045e`, `vid&0002045e`, `vid&02045e`): its low 16
/// bits, the Bluetooth forms carry the id source in the high digits
bool HexAfter(const std::string& path, const char* key, uint16_t& out)
{
    const size_t at = path.find(key);
    if (at == std::string::npos)
        return false;
    uint32_t value = 0;
    size_t digits = 0;
    for (size_t i = at + std::char_traits<char>::length(key); i < path.size() && digits < 8; ++i, ++digits)
    {
        const int digit = HexDigit(path[i]);
        if (digit < 0)
            break;
        value = (value << 4) | static_cast<uint32_t>(digit);
    }
    if (digits < 4)
        return false;
    out = static_cast<uint16_t>(value & 0xFFFF);
    return true;
}

std::string Hex4(uint16_t value)
{
    char text[8];
    snprintf(text, sizeof(text), "%04x", value);
    return text;
}

/// FNV-1a, as the IOHID backend's: an id never carries a MAC address or a port path as such
std::string Hash(const std::string& text)
{
    uint32_t hash = 2166136261u;
    for (unsigned char c : text)
        hash = (hash ^ c) * 16777619u;
    char out[12];
    snprintf(out, sizeof(out), "%08x", hash);
    return out;
}

struct PadButton
{
    uint32_t mask;
    const char* control;
};
/// GameInputGamepadButtons (GameInput.h v3); the trigger and stick direction bits are derived, not reported
constexpr PadButton kPadButtons[] = {
    {0x00000001, "menu"},         {0x00000002, "view"},         {0x00000004, "south"},     {0x00000008, "east"},
    {0x00000010, "west"},         {0x00000020, "north"},        {0x00000040, "dpad.up"},   {0x00000080, "dpad.down"},
    {0x00000100, "dpad.left"},    {0x00000200, "dpad.right"},   {0x00000400, "lb"},        {0x00000800, "rb"},
    {0x00001000, "lstick.click"}, {0x00002000, "rstick.click"}, {0x10000000, "paddle.p1"}, {0x20000000, "paddle.p2"},
    {0x04000000, "paddle.p3"},    {0x08000000, "paddle.p4"},
};
}  // namespace

WinHidPath ParseWinHidPath(const std::string& path)
{
    WinHidPath out;
    const std::string lower = Lower(path);
    out.haveIds = (HexAfter(lower, "vid_", out.vendor) || HexAfter(lower, "vid&", out.vendor)) &&
                  (HexAfter(lower, "pid_", out.product) || HexAfter(lower, "pid&", out.product));
    if (!out.haveIds)
        out.vendor = out.product = 0;
    // The Bluetooth HID service (classic) and HID over GATT (LE), or the enumerators of either
    if (lower.find("{00001124-") != std::string::npos || lower.find("{00001812-") != std::string::npos ||
        lower.find("bthenum") != std::string::npos || lower.find("bthledevice") != std::string::npos)
        out.bus = "bt";
    else if (out.haveIds && (lower.find("hid#vid_") != std::string::npos || lower.find("usb#") != std::string::npos))
        out.bus = "usb";
    out.xinput = lower.find("&ig_") != std::string::npos;
    return out;
}

WinHidRole WinRawInputRole(uint16_t usagePage, uint16_t usage, const std::string& path)
{
    if (usagePage != 0x01)
        return WinHidRole::Skip;
    if (usage == 0x0E)
        return WinHidRole::Dial;
    if (usage != 0x04 && usage != 0x05 && usage != 0x08)
        return WinHidRole::Skip;
    return ParseWinHidPath(path).xinput ? WinHidRole::Skip : WinHidRole::Pad;
}

std::string WinDeviceId(const std::string& bus, uint16_t vendor, uint16_t product, uint16_t version,
                        const std::string& serial, const std::string& path)
{
    const std::string folded = Lower(path);
    if (bus == "bt")
        return "bt:" + Hex4(vendor) + ":" + Hex4(product) + ":" + Hash(serial.empty() ? folded : serial);
    const std::string prefix = (bus.empty() ? std::string("hid") : bus) + ":" + Hex4(vendor) + ":" + Hex4(product) +
                               ":" + Hex4(version);
    return serial.empty() ? prefix + "@" + Hash(folded) : prefix + ":" + serial;
}

bool LocateHidBits(const uint8_t* cleared, const uint8_t* set, size_t size, uint32_t& bitOffset, uint8_t& bitSize)
{
    if (size < 2)
        return false;
    const uint32_t bits = static_cast<uint32_t>((size - 1) * 8);
    auto differs = [&](uint32_t bit) {
        const size_t byte = 1 + bit / 8;
        return ((cleared[byte] ^ set[byte]) >> (bit % 8)) & 1;
    };
    uint32_t first = bits;
    uint32_t last = 0;
    for (uint32_t bit = 0; bit < bits; ++bit)
    {
        if (!differs(bit))
            continue;
        first = std::min(first, bit);
        last = bit;
    }
    if (first == bits || last - first >= 32)
        return false;
    for (uint32_t bit = first; bit <= last; ++bit)
        if (!differs(bit))
            return false;
    bitOffset = first;
    bitSize = static_cast<uint8_t>(last - first + 1);
    return true;
}

int32_t WinLogicalMax(int32_t logicalMin, int32_t logicalMax, uint8_t bitSize)
{
    if (logicalMin < 0 || logicalMax >= logicalMin || bitSize == 0 || bitSize >= 32)
        return logicalMax;
    return static_cast<int32_t>(static_cast<uint32_t>(logicalMax) & ((1u << bitSize) - 1));
}

std::vector<uint8_t> PadHidReport(const std::vector<uint8_t>& report, size_t length)
{
    std::vector<uint8_t> out = report;
    if (out.size() < length)
        out.resize(length, 0);
    return out;
}

bool GameInputServes(int deviceFamily)
{
    return deviceFamily == kGameInputFamilyXboxOne || deviceFamily == kGameInputFamilyXbox360;
}

std::vector<std::string> GameInputPadControls()
{
    std::vector<std::string> out;
    for (const PadButton& button : kPadButtons)
        out.emplace_back(button.control);
    for (const char* axis : {"lt", "rt", "lstick.x", "lstick.y", "rstick.x", "rstick.y"})
        out.emplace_back(axis);
    return out;
}

void DiffGameInputPad(const GameInputPadState& was, const GameInputPadState& now, bool first, const ControlSink& sink)
{
    for (const PadButton& button : kPadButtons)
    {
        const bool held = (now.buttons & button.mask) != 0;
        if (first ? held : held != ((was.buttons & button.mask) != 0))
            sink(button.control, held ? 1.0f : 0.0f);
    }
    auto axis = [&](const char* control, float before, float value) {
        if (first || before != value)
            sink(control, value);
    };
    axis("lt", was.leftTrigger, now.leftTrigger);
    axis("rt", was.rightTrigger, now.rightTrigger);
    axis("lstick.x", was.leftX, now.leftX);
    axis("lstick.y", -was.leftY, -now.leftY);  // GameInput: + up; canonical: + down
    axis("rstick.x", was.rightX, now.rightX);
    axis("rstick.y", -was.rightY, -now.rightY);
}

}  // namespace controllers
