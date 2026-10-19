#include "hid/drivers/xboxgip.h"

namespace controllers
{

namespace
{
constexpr uint8_t kGipInput = 0x20;
constexpr size_t kReportSize = 18;

/// Bytes 4 and 5 as one word: byte 4 bits 2..7, then byte 5 (d-pad, shoulders, stick clicks)
struct Bit
{
    uint16_t mask;
    const char* control;
};
constexpr Bit kButtons[] = {
    {0x0004, "menu"},       {0x0008, "view"},         {0x0010, "south"},      {0x0020, "east"},
    {0x0040, "west"},       {0x0080, "north"},        {0x0100, "dpad.up"},    {0x0200, "dpad.down"},
    {0x0400, "dpad.left"},  {0x0800, "dpad.right"},   {0x1000, "lb"},         {0x2000, "rb"},
    {0x4000, "lstick.click"}, {0x8000, "rstick.click"},
};
const char* const kSticks[] = {"lstick.x", "lstick.y", "rstick.x", "rstick.y"};

float Stick(int16_t value, bool y)
{
    // GIP y is + up; the vocabulary's is - up
    const float v = value >= 0 ? value / 32767.0f : value / 32768.0f;
    return y ? -v : v;
}
}  // namespace

bool XboxGipPad::Handles(uint16_t vendor, uint16_t product)
{
    return vendor == 0x045E &&
           (product == 0x028E || product == 0x02EA || product == 0x0B00 || product == 0x0B12);
}

std::vector<std::string> XboxGipPad::Controls()
{
    std::vector<std::string> out;
    for (const Bit& bit : kButtons)
        out.emplace_back(bit.control);
    out.insert(out.end(), {"lt", "rt", "lstick.x", "lstick.y", "rstick.x", "rstick.y"});
    return out;
}

void XboxGipPad::Decode(const uint8_t* report, size_t size, const ControlSink& sink)
{
    if (size < kReportSize || report[0] != kGipInput)
        return;
    const uint16_t buttons = static_cast<uint16_t>((report[4] & 0xFC) | (report[5] << 8));
    for (const Bit& bit : kButtons)
        if (((buttons ^ _buttons) & bit.mask) || (!_seen && (buttons & bit.mask)))
            sink(bit.control, buttons & bit.mask ? 1.0f : 0.0f);
    _buttons = buttons;

    for (int t = 0; t < 2; ++t)
    {
        const uint16_t value = static_cast<uint16_t>((report[6 + 2 * t] | (report[7 + 2 * t] << 8)) & 0x3FF);
        if (!_seen || value != _triggers[t])
            sink(t == 0 ? "lt" : "rt", value / 1023.0f);
        _triggers[t] = value;
    }
    for (int s = 0; s < 4; ++s)
    {
        const int16_t value = static_cast<int16_t>(report[10 + 2 * s] | (report[11 + 2 * s] << 8));
        if (!_seen || value != _sticks[s])
            sink(kSticks[s], Stick(value, s % 2 == 1));
        _sticks[s] = value;
    }
    _seen = true;
}

}  // namespace controllers
