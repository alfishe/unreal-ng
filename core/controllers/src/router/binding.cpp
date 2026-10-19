#include "router/binding.h"

#include <cctype>

#include "hostcontrols.h"
#include "router/zxkeys.h"

namespace controllers
{

namespace
{
constexpr unsigned kMaxFrames = 50;
constexpr unsigned kMaxBurst = 16;
constexpr size_t kMaxMacroSteps = 64;

std::string Join(const std::vector<std::string>& names)
{
    std::string text;
    for (const std::string& name : names)
    {
        if (!text.empty())
            text += ", ";
        text += name;
    }
    return text;
}

struct TurboEntry
{
    Turbo turbo;
    const char* name;
};
constexpr TurboEntry kTurboNames[] = {{Turbo::Off, "off"},
                                      {Turbo::Classic, "classic"},
                                      {Turbo::Toggle, "toggle"},
                                      {Turbo::Hold, "hold"},
                                      {Turbo::Burst, "burst"}};

struct MouseEntry
{
    MouseControl control;
    const char* name;
};
constexpr MouseEntry kMouseNames[] = {{MouseControl::Left, "left"},   {MouseControl::Right, "right"},
                                      {MouseControl::Middle, "middle"}, {MouseControl::Dx, "dx"},
                                      {MouseControl::Dy, "dy"},       {MouseControl::Wheel, "wheel"}};

struct LayerModeEntry
{
    LayerMode mode;
    const char* name;
};
constexpr LayerModeEntry kLayerModes[] = {
    {LayerMode::Hold, "hold"}, {LayerMode::Toggle, "toggle"}, {LayerMode::Oneshot, "oneshot"}};

/// A digital control: a button, a key, a trigger's click (not a stick, an axis or `dpad.*`)
bool IsDigital(const std::string& source)
{
    ControlInfo info;
    if (!ParseControl(source, info))
        return false;
    if (!info.base.empty())
        return info.direction == 0;  // lt.click, not lstick.left
    return info.type == ControlType::Button || info.type == ControlType::Trigger;
}
}  // namespace

bool ParseRetroTarget(const std::string& text, RetroTarget& out, std::string& error)
{
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size())
    {
        error = "a retro target is <connector>:<control>, got '" + text + "'";
        return false;
    }
    RetroTarget target;
    target.connector = text.substr(0, colon);
    for (char& c : target.connector)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string control = text.substr(colon + 1);
    if (!RetroControlMask(control, target.mask))
    {
        error = "unknown retro control '" + control + "' (known: " + Join(KnownRetroControls()) + ")";
        return false;
    }
    out = target;
    return true;
}

bool ParseRetroTarget(const std::string& text, Target& out, std::string& error)
{
    Target target;
    target.kind = TargetKind::Retro;
    if (!ParseRetroTarget(text, target.retro, error))
        return false;
    out = target;
    return true;
}

bool ParseKeysTarget(const std::string& text, Target& out, std::string& error)
{
    Target target;
    target.kind = TargetKind::Keys;
    if (!ZxKeysFromName(text, target.keys))
    {
        error = "unknown ZX key '" + text + "' (known: " + Join(KnownZxKeyNames()) + ", joined with +)";
        return false;
    }
    out = target;
    return true;
}

bool ParseMacroTarget(const std::vector<std::string>& steps, Target& out, std::string& error)
{
    if (steps.empty() || steps.size() > kMaxMacroSteps)
    {
        error = "a macro is 1.." + std::to_string(kMaxMacroSteps) + " steps";
        return false;
    }
    Target target;
    target.kind = TargetKind::Macro;
    for (size_t i = 0; i < steps.size(); i++)
    {
        std::vector<uint8_t> keys;
        if (!ZxKeysFromName(steps[i], keys))
        {
            error = "macro step " + std::to_string(i) + ": unknown ZX key '" + steps[i] + "'";
            return false;
        }
        target.steps.push_back(keys);
    }
    out = target;
    return true;
}

bool ParseMouseTarget(const std::string& control, float scale, Target& out, std::string& error)
{
    for (const MouseEntry& entry : kMouseNames)
    {
        if (control == entry.name)
        {
            Target target;
            target.kind = TargetKind::Mouse;
            target.mouse = entry.control;
            target.scale = scale;
            out = target;
            return true;
        }
    }
    error = "unknown mouse control '" + control + "' (known: left, right, middle, dx, dy, wheel)";
    return false;
}

Target ActionTarget(const std::string& action)
{
    Target target;
    target.kind = TargetKind::Action;
    target.name = action;
    return target;
}

bool ParseLayerTarget(const std::string& layer, const std::string& mode, Target& out, std::string& error)
{
    if (layer.empty())
    {
        error = "a layer target names its layer";
        return false;
    }
    for (const LayerModeEntry& entry : kLayerModes)
    {
        if (mode == entry.name)
        {
            Target target;
            target.kind = TargetKind::Layer;
            target.name = layer;
            target.layerMode = entry.mode;
            out = target;
            return true;
        }
    }
    error = "unknown layer mode '" + mode + "' (known: hold, toggle, oneshot)";
    return false;
}

bool ValidateBinding(const Binding& binding, std::string& error)
{
    if (binding.sources.empty())
    {
        error = "a binding needs at least one source";
        return false;
    }
    const Target& target = binding.target;
    const bool mouseAxis =
        target.kind == TargetKind::Mouse && (target.mouse == MouseControl::Dx || target.mouse == MouseControl::Dy);
    if (binding.chord && binding.sources.size() < 2)
    {
        error = "a chord needs at least two controls";
        return false;
    }
    for (const std::string& source : binding.sources)
    {
        ControlInfo info;
        const bool whole = source == "dpad.*" || !StickOf(source).empty();
        if (!whole && !ParseControl(source, info))
        {
            error = "unknown control '" + source + "' (known: " + Join(KnownControls()) +
                    ", button.<n>, axis.<n>, hat.<n>.<dir>, key.<name>, lstick, rstick, dpad.*)";
            return false;
        }
        if (binding.chord && !IsDigital(source))
        {
            error = "chord member '" + source + "' is not a button";
            return false;
        }
        // An encoder's detents drive actions with the projection to come (control-model TODO T4), not a target
        if (!whole && (info.type == ControlType::Delta || info.type == ControlType::Pointer ||
                       info.type == ControlType::Encoder))
        {
            error = "'" + source + "' is not a button: it cannot drive a target yet";
            return false;
        }
        if (whole && mouseAxis)
        {
            error = "a mouse axis takes one axis ('" + source + ".x'), not a whole stick";
            return false;
        }
    }
    switch (target.kind)
    {
        case TargetKind::None:
            break;
        case TargetKind::Retro:
            if (target.retro.connector.empty() || target.retro.mask == 0)
            {
                error = "a binding needs a retro target (<connector>:<control>)";
                return false;
            }
            break;
        case TargetKind::Keys:
            if (target.keys.empty())
            {
                error = "a key target needs a ZX key";
                return false;
            }
            break;
        case TargetKind::Macro:
            if (target.steps.empty() || target.steps.size() > kMaxMacroSteps)
            {
                error = "a macro is 1.." + std::to_string(kMaxMacroSteps) + " steps";
                return false;
            }
            break;
        case TargetKind::Mouse:
            break;
        case TargetKind::Action:
        case TargetKind::Layer:
            if (target.name.empty())
            {
                error = target.kind == TargetKind::Action ? "an action target names its action"
                                                          : "a layer target names its layer";
                return false;
            }
            if (target.kind == TargetKind::Layer && target.name == binding.layer)
            {
                error = "layer '" + target.name + "' cannot switch itself";
                return false;
            }
            break;
    }
    const BindingOptions& options = binding.options;
    const bool pulses = options.turbo != Turbo::Off && options.turbo != Turbo::Hold;
    if (pulses && (options.on < 1 || options.off < 1 || options.on > kMaxFrames || options.off > kMaxFrames))
    {
        error = "turbo on / off are 1.." + std::to_string(kMaxFrames) + " frames";
        return false;
    }
    if (options.turbo == Turbo::Burst && (options.count < 1 || options.count > kMaxBurst))
    {
        error = "a burst is 1.." + std::to_string(kMaxBurst) + " pulses";
        return false;
    }
    if (options.press == PressMode::Tap && (options.frames < 1 || options.frames > kMaxFrames))
    {
        error = "a tap is 1.." + std::to_string(kMaxFrames) + " frames";
        return false;
    }
    return true;
}

bool TurboFromName(const std::string& name, Turbo& out)
{
    for (const TurboEntry& entry : kTurboNames)
    {
        if (name == entry.name)
        {
            out = entry.turbo;
            return true;
        }
    }
    return false;
}

const char* TurboName(Turbo turbo)
{
    for (const TurboEntry& entry : kTurboNames)
    {
        if (entry.turbo == turbo)
            return entry.name;
    }
    return "off";
}

}  // namespace controllers
