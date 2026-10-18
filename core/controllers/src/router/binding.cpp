#include "router/binding.h"

#include <cctype>

#include "hostcontrols.h"

namespace controllers
{

namespace
{
constexpr unsigned kMaxFrames = 50;
constexpr unsigned kMaxBurst = 16;

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

bool ValidateBinding(const Binding& binding, std::string& error)
{
    if (binding.sources.empty())
    {
        error = "a binding needs at least one source";
        return false;
    }
    for (const std::string& source : binding.sources)
    {
        ControlInfo info;
        if (source == "dpad.*" || !StickOf(source).empty())
            continue;
        if (!ParseControl(source, info))
        {
            error = "unknown control '" + source + "' (known: " + Join(KnownControls()) +
                    ", button.<n>, axis.<n>, hat.<n>.<dir>, key.<name>, lstick, rstick, dpad.*)";
            return false;
        }
        if (info.type == ControlType::Delta || info.type == ControlType::Pointer)
        {
            error = "'" + source + "' is not a button: it cannot drive a retro control";
            return false;
        }
    }
    if (binding.target.connector.empty() || binding.target.mask == 0)
    {
        error = "a binding needs a retro target (<connector>:<control>)";
        return false;
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
