#include "hid/hidpad.h"

#include <algorithm>

#include "devicedatabase.h"

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
    HidLayout out;
    out.family = "generic";
    // Xbox over Bluetooth (the HID layout SDL's HIDAPI driver reads; research-host-apis.md §2.3b)
    if (layout == "xbox")
    {
        out.family = "xbox";
        out.buttons = {"south", "east", "",     "west", "north",        "",
                       "lb",    "rb",   "",     "",     "view",         "menu",
                       "guide", "lstick.click", "rstick.click"};
    }
    // DualShock 4 and DualSense: Square Cross Circle Triangle L1 R1 L2 R2 Share/Create Options L3 R3 PS Touchpad
    // (DualSense: Mic)
    else if (layout == "playstation")
    {
        out.family = "playstation";
        out.buttons = {"west", "south", "east", "north", "lb", "rb", "lt.click", "rt.click", "view", "menu",
                       "lstick.click", "rstick.click", "guide", "touchpad.click", "misc.1"};
        out.rxRyTriggers = true;
    }
    // Switch Pro and pads in Switch mode: B A Y X L R ZL ZR - + LS RS Home Capture
    else if (layout == "nintendo")
    {
        out.family = "nintendo";
        out.buttons = {"south", "east", "west", "north", "lb", "rb", "lt.click", "rt.click", "view", "menu",
                       "lstick.click", "rstick.click", "guide", "share"};
    }
    return out;
}

HidLayout HidLayoutFor(const DeviceMatch& match)
{
    HidLayout layout = HidLayoutFor(match.layout);
    const DeviceMatch::HidMap& map = match.hid;
    if (!map.buttons.empty())
        layout.buttons = map.buttons;
    if (map.rxRyTriggers >= 0)
        layout.rxRyTriggers = map.rxRyTriggers != 0;
    layout.dpadAxes = map.dpadAxes;
    if (!map.hat.empty())
        layout.hat = map.hat;
    layout.consumer = map.consumer;
    for (const DeviceMatch::HidMap::Bit& bit : map.bits)
        layout.bits.push_back({bit.report, bit.byte, bit.bit, bit.control});
    return layout;
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
                case 0x30:
                case 0x31:
                    slot.kind = layout.dpadAxes ? Kind::AxisDpad : Kind::Axis;
                    slot.control = layout.dpadAxes ? (field.usage == 0x30 ? "dpad.x" : "dpad.y")
                                                   : (field.usage == 0x30 ? "lstick.x" : "lstick.y");
                    break;
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
                    slot.control = hats++ == 0 ? layout.hat : "hat." + std::to_string(hats - 1);
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
        else if (field.usagePage == kConsumer &&
                 std::any_of(layout.consumer.begin(), layout.consumer.end(),
                             [&field](const auto& entry) { return entry.first == field.usage; }))
        {
            for (const auto& [usage, control] : layout.consumer)
                if (usage == field.usage)
                    slot.control = control;
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
    if (_slots.empty())
        return;  // not a pad: the extra bits alone make none
    for (const HidBit& bit : layout.bits)
    {
        Slot slot;
        slot.field.reportId = bit.report;
        slot.field.bitOffset = bit.byte * 8u + bit.bit;
        slot.field.bitSize = 1;
        slot.field.usagePage = kButtonPage;
        slot.field.logicalMax = 1;
        slot.control = bit.control;
        _slots.push_back(std::move(slot));
    }
}

std::vector<std::string> HidPad::Controls() const
{
    std::vector<std::string> out;
    for (const Slot& slot : _slots)
    {
        if (slot.kind == Kind::Hat && (slot.control == "lstick" || slot.control == "rstick"))
        {
            out.push_back(slot.control + ".x");
            out.push_back(slot.control + ".y");
        }
        else if (slot.kind == Kind::Hat)
        {
            const std::string base = slot.control == "dpad" ? "dpad." : slot.control + ".";
            for (const char* direction : {"up", "down", "left", "right"})
                out.push_back(base + direction);
        }
        else if (slot.kind == Kind::Paddles)
            out.insert(out.end(), std::begin(kPaddles), std::end(kPaddles));
        else if (slot.kind == Kind::AxisDpad)
        {
            const bool x = slot.control == "dpad.x";
            out.push_back(x ? "dpad.left" : "dpad.up");
            out.push_back(x ? "dpad.right" : "dpad.down");
        }
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
            case Kind::AxisDpad:
            {
                // Past half way is pressed: the two directions of the axis, each sent when it changes
                auto side = [&slot](int32_t raw) {
                    const float v = Normalized(raw, slot.field, true);
                    return v <= -0.5f ? -1 : (v >= 0.5f ? 1 : 0);
                };
                const int was = first ? 0 : side(before);
                const int now = side(value);
                const bool x = slot.control == "dpad.x";
                if (was != now)
                {
                    if (was != 0)
                        Emit(was < 0 ? (x ? "dpad.left" : "dpad.up") : (x ? "dpad.right" : "dpad.down"), 0.0f);
                    if (now != 0)
                        Emit(now < 0 ? (x ? "dpad.left" : "dpad.up") : (x ? "dpad.right" : "dpad.down"), 1.0f);
                }
                break;
            }
            case Kind::Trigger:
                Emit(slot.control, Normalized(value, slot.field, false));
                break;
            case Kind::Hat:
            {
                const uint8_t was = first ? 0 : HatBits(before, slot.field);
                const uint8_t now = HatBits(value, slot.field);
                if (slot.control == "lstick" || slot.control == "rstick")
                {
                    // A stick reported as a hat (a single Joy-Con's simple report): full deflection on the axes
                    const auto axis = [](uint8_t bits, int minus, int plus) {
                        return ((bits >> plus) & 1 ? 1.0f : 0.0f) - ((bits >> minus) & 1 ? 1.0f : 0.0f);
                    };
                    if (axis(was, 3, 1) != axis(now, 3, 1) || first)
                        Emit(slot.control + ".x", axis(now, 3, 1));
                    if (axis(was, 0, 2) != axis(now, 0, 2) || first)
                        Emit(slot.control + ".y", axis(now, 0, 2));
                    break;
                }
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
