#include "hid/hidpad.h"

#include <algorithm>

namespace controllers
{

namespace
{
constexpr uint16_t kGenericDesktop = 0x01;
constexpr uint16_t kSimulation = 0x02;
constexpr uint16_t kButtonPage = 0x09;
constexpr uint16_t kConsumer = 0x0C;

const char* const kPaddles[] = {"paddle.p1", "paddle.p2", "paddle.p3", "paddle.p4"};
const char* const kFaces[] = {"south", "east", "west", "north"};

bool IsFace(const std::string& control)
{
    return std::find(std::begin(kFaces), std::end(kFaces), control) != std::end(kFaces);
}

/// Hat 0..7 clockwise from up -> the d-pad bits up, right, down, left
uint8_t HatBits(int32_t value, const HidField& field)
{
    static const uint8_t kBits[8] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xC, 0x8, 0x9};
    const int32_t index = value - field.logicalMin;
    if (index < 0 || index > 7 || value > field.logicalMax)
        return 0;
    return kBits[index];
}

float Normalized(int32_t value, const HidField& field, bool centred)
{
    const double min = field.logicalMin;
    const double max = field.logicalMax;
    if (max <= min)
        return 0;
    double v = std::clamp(static_cast<double>(value), min, max);
    if (!centred)
        return static_cast<float>((v - min) / (max - min));
    // The centre of an even span is the upper middle value (0..65535 rests at 32768)
    const double centre = std::floor((min + max + 1) / 2);
    const double half = v >= centre ? max - centre : centre - min;
    return half > 0 ? static_cast<float>((v - centre) / half) : 0.0f;
}
}  // namespace

HidLayout HidLayoutFor(const std::string& layout)
{
    // Xbox over Bluetooth (the HID layout SDL's HIDAPI driver reads; research-host-apis.md §2.3b)
    if (layout == "xbox")
        return {"xbox",
                {"south", "east", "", "west", "north", "", "lb", "rb", "", "", "view", "menu", "guide", "lstick.click",
                 "rstick.click"},
                false};
    // DualShock 4 and DualSense: Square Cross Circle Triangle L1 R1 L2 R2 Share/Create Options L3 R3 PS Touchpad
    if (layout == "playstation")
        return {"playstation",
                {"west", "south", "east", "north", "lb", "rb", "lt.click", "rt.click", "view", "menu", "lstick.click",
                 "rstick.click", "guide", "touchpad.click"},
                true};
    // Switch Pro and pads in Switch mode: B A Y X L R ZL ZR - + LS RS Home Capture
    if (layout == "nintendo")
        return {"nintendo",
                {"south", "east", "west", "north", "lb", "rb", "lt.click", "rt.click", "view", "menu", "lstick.click",
                 "rstick.click", "guide", "share"},
                false};
    return {"generic", {}, false};
}

HidLayout HidLayoutFor(const std::string& layout, const std::vector<std::string>& buttons)
{
    HidLayout result = HidLayoutFor(layout);
    if (result.buttons.size() < buttons.size())
        result.buttons.resize(buttons.size());
    for (size_t i = 0; i < buttons.size(); i++)
        if (!buttons[i].empty())
            result.buttons[i] = buttons[i];
    return result;
}

HidPad::HidPad(const HidDescriptor& descriptor, const HidLayout& layout) : _hasReportIds(descriptor.hasReportIds)
{
    unsigned axes = 0;
    unsigned hats = 0;
    for (const HidField& field : descriptor.fields)
    {
        if (field.application != 0x04 && field.application != 0x05 && field.application != 0x08)
            continue;
        Slot slot;
        slot.field = field;
        if (field.usagePage == kButtonPage && field.usage >= 1)
        {
            const size_t index = field.usage - 1u;
            slot.control = index < layout.buttons.size() && !layout.buttons[index].empty()
                               ? layout.buttons[index]
                               : "button." + std::to_string(index);
        }
        else if (field.usagePage == kGenericDesktop)
        {
            switch (field.usage)
            {
                case 0x30: slot.kind = Kind::Axis; slot.control = "lstick.x"; break;
                case 0x31: slot.kind = Kind::Axis; slot.control = "lstick.y"; break;
                case 0x32: slot.kind = Kind::Axis; slot.control = "rstick.x"; break;
                case 0x35: slot.kind = Kind::Axis; slot.control = "rstick.y"; break;
                case 0x33:
                case 0x34:
                    if (layout.rxRyTriggers)
                    {
                        slot.kind = Kind::Trigger;
                        slot.control = field.usage == 0x33 ? "lt" : "rt";
                    }
                    else
                    {
                        slot.kind = Kind::Axis;
                        slot.control = "axis." + std::to_string(4 + axes++);
                    }
                    break;
                case 0x39:
                    slot.kind = Kind::Hat;
                    slot.control = hats++ == 0 ? "dpad" : "hat." + std::to_string(hats - 1);
                    break;
                case 0x36:
                case 0x37:
                case 0x38:
                    slot.kind = Kind::Axis;
                    slot.control = "axis." + std::to_string(4 + axes++);
                    break;
                case 0x90: slot.control = "dpad.up"; break;
                case 0x91: slot.control = "dpad.down"; break;
                case 0x92: slot.control = "dpad.right"; break;
                case 0x93: slot.control = "dpad.left"; break;
                default: continue;
            }
        }
        else if (field.usagePage == kSimulation && (field.usage == 0xC4 || field.usage == 0xC5))
        {
            slot.kind = Kind::Trigger;
            slot.control = field.usage == 0xC5 ? "lt" : "rt";  // Brake, Accelerator
        }
        else if (field.usagePage == kConsumer && layout.family == "xbox")
        {
            if (field.usage == 0xB2)
                slot.control = "share";
            else if (field.usage == 0x81)
                slot.kind = Kind::Paddles;
            else if (field.usage == 0x85)
                slot.kind = Kind::Profile;
            else
                continue;
        }
        else
            continue;
        _slots.push_back(std::move(slot));
    }
}

std::vector<std::string> HidPad::Controls() const
{
    std::vector<std::string> out;
    for (const Slot& slot : _slots)
    {
        if (slot.kind == Kind::Hat)
        {
            const std::string base = slot.control == "dpad" ? "dpad." : slot.control + ".";
            for (const char* direction : {"up", "down", "left", "right"})
                out.push_back(base + direction);
        }
        else if (slot.kind == Kind::Paddles)
            out.insert(out.end(), std::begin(kPaddles), std::end(kPaddles));
        else if (slot.kind != Kind::Profile)
            out.push_back(slot.control);
    }
    return out;
}

void HidPad::Emit(const std::string& control, float value)
{
    _pending.emplace_back(control, value);
}

void HidPad::Decode(const uint8_t* report, size_t size, const ControlSink& sink)
{
    _pending.clear();
    uint8_t paddlesBefore = 0;
    uint8_t paddlesNow = 0;
    bool havePaddles = false;
    for (Slot& slot : _slots)
    {
        int32_t value = 0;
        if (!ReadHidField(slot.field, _hasReportIds, report, size, value))
            continue;
        const bool first = !slot.seen;
        const int32_t before = slot.last;
        slot.seen = true;
        slot.last = value;
        if (slot.kind == Kind::Profile)
        {
            _profile = static_cast<uint8_t>(value);
            continue;
        }
        if (slot.kind == Kind::Paddles)
        {
            havePaddles = true;
            paddlesBefore = first ? 0 : static_cast<uint8_t>(before & 0x0F);
            paddlesNow = static_cast<uint8_t>(value & 0x0F);
            continue;
        }
        if (!first && before == value)
            continue;
        switch (slot.kind)
        {
            case Kind::Button:
                if (!first || value != 0)
                    Emit(slot.control, value != 0 ? 1.0f : 0.0f);
                break;
            case Kind::Axis:
                Emit(slot.control, Normalized(value, slot.field, true));
                break;
            case Kind::Trigger:
                Emit(slot.control, Normalized(value, slot.field, false));
                break;
            case Kind::Hat:
            {
                const uint8_t was = first ? 0 : HatBits(before, slot.field);
                const uint8_t now = HatBits(value, slot.field);
                const std::string base = slot.control == "dpad" ? "dpad." : slot.control + ".";
                const char* names[] = {"up", "right", "down", "left"};
                for (int bit = 0; bit < 4; ++bit)
                    if (((was ^ now) >> bit) & 1)
                        Emit(base + names[bit], (now >> bit) & 1 ? 1.0f : 0.0f);
                break;
            }
            default:
                break;
        }
    }

    if (havePaddles)
    {
        for (int p = 0; p < 4; ++p)
        {
            const bool was = (paddlesBefore >> p) & 1;
            const bool now = (paddlesNow >> p) & 1;
            if (was == now)
                continue;
            if (now && _profile != 0)
            {
                // A face press in the same report is the profile's mirror of this paddle
                auto it = std::find_if(_pending.begin(), _pending.end(), [](const auto& event) {
                    return IsFace(event.first) && event.second > 0;
                });
                if (it != _pending.end())
                {
                    _mirrored[p] = it->first;
                    _pending.erase(it);
                }
            }
            _pending.emplace_back(kPaddles[p], now ? 1.0f : 0.0f);
            if (!now && !_mirrored[p].empty())
            {
                // The mirror's release: dropped with the paddle's, whenever it comes
                const std::string face = _mirrored[p];
                _pending.erase(std::remove_if(_pending.begin(), _pending.end(),
                                              [&face](const auto& event) { return event.first == face; }),
                               _pending.end());
                _mirrored[p].clear();
            }
        }
    }
    for (const auto& [control, value] : _pending)
    {
        bool held = false;
        for (const std::string& face : _mirrored)
            held = held || (!face.empty() && face == control);
        if (!held)
            sink(control, value);
    }
}

}  // namespace controllers
