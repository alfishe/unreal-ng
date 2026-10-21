#include "haptics/surfacehaptics.h"

#include <algorithm>
#include <cmath>

#include "router/binding.h"

namespace controllers
{

namespace
{
constexpr uint16_t kMaxSteps = 3600;  ///< the Dial's Resolution Multiplier range (devices.md §1.2)
constexpr uint32_t kMaxIntervalMs = 1000;

bool WholeNumber(const JsonValue& value, double min, double max)
{
    return value.IsNumber() && value.AsNumber() >= min && value.AsNumber() <= max &&
           std::floor(value.AsNumber()) == value.AsNumber();
}

std::string Join(const std::vector<std::string>& names)
{
    std::string text;
    for (const std::string& name : names)
        text += (text.empty() ? "" : ", ") + name;
    return text;
}

bool ParseLayer(const JsonValue& spec, LayerHaptics& out, const std::string& where, std::string& error)
{
    if (!spec.IsObject())
    {
        error = where + " is an object {steps, clicks, marks, min_interval_ms}";
        return false;
    }
    LayerHaptics layer;
    for (const auto& [field, value] : spec.GetMembers())
    {
        if (field == "steps")
        {
            if (!WholeNumber(value, 1, kMaxSteps))
            {
                error = where + ".steps is a whole number 1.." + std::to_string(kMaxSteps);
                return false;
            }
            layer.steps.perTurn = static_cast<uint16_t>(value.AsNumber());
        }
        else if (field == "clicks")
        {
            if (!value.IsBool())
            {
                error = where + ".clicks is true (a click at each step) or false (a free wheel)";
                return false;
            }
            layer.steps.clicks = value.AsBool();
        }
        else if (field == "min_interval_ms")
        {
            if (!WholeNumber(value, 0, kMaxIntervalMs))
            {
                error = where + ".min_interval_ms is a whole number 0.." + std::to_string(kMaxIntervalMs);
                return false;
            }
            layer.minIntervalMs = static_cast<uint32_t>(value.AsNumber());
        }
        else if (field == "marks")
        {
            if (!value.IsObject())
            {
                error = where + ".marks is an object: mark -> pattern";
                return false;
            }
            for (const auto& [kind, pattern] : value.GetMembers())
            {
                if (std::find(MarkKinds().begin(), MarkKinds().end(), kind) == MarkKinds().end())
                {
                    error = where + ".marks: unknown mark '" + kind + "' (known: " + Join(MarkKinds()) + ")";
                    return false;
                }
                HapticPattern parsed;
                if (!pattern.IsString() || !HapticPatternFromName(pattern.AsString(), parsed))
                {
                    error = where + ".marks." + kind + " is a pattern: tick, double or buzz";
                    return false;
                }
                layer.marks[kind] = parsed;
            }
        }
        else
        {
            error = where + ": unknown field '" + field + "' (known: steps, clicks, marks, min_interval_ms)";
            return false;
        }
    }
    out = layer;
    return true;
}
}  // namespace

const char* HapticPatternName(HapticPattern pattern)
{
    switch (pattern)
    {
        case HapticPattern::Tick: return "tick";
        case HapticPattern::Double: return "double";
        case HapticPattern::Buzz: return "buzz";
    }
    return "tick";
}

bool HapticPatternFromName(const std::string& name, HapticPattern& out)
{
    for (const HapticPattern pattern : {HapticPattern::Tick, HapticPattern::Double, HapticPattern::Buzz})
        if (name == HapticPatternName(pattern))
        {
            out = pattern;
            return true;
        }
    return false;
}

const std::vector<std::string>& MarkKinds()
{
    static const std::vector<std::string> kinds = {"second", "ten_seconds", "bookmark", "edge", "barrier"};
    return kinds;
}

bool ParseSurfaceHaptics(const JsonValue& section, SurfaceHaptics& out, std::string& error)
{
    if (!section.IsObject())
    {
        error = "haptics of a debug profile is an object: layer -> {steps, clicks, marks, min_interval_ms}";
        return false;
    }
    SurfaceHaptics parsed;
    for (const auto& [layer, spec] : section.GetMembers())
    {
        if (layer != kDebugLayer && !ValidContextLayer(layer, error))
        {
            error = "haptics: " + (IsContextLayer(layer) ? error
                                                         : "'" + layer + "' is not a layer of a debug profile (debug "
                                                                         "or a context layer such as @ttd)");
            return false;
        }
        if (!ParseLayer(spec, parsed.layers[layer], "haptics." + layer, error))
            return false;
    }
    out = std::move(parsed);
    return true;
}

bool PatternForMarks(const LayerHaptics& layer, const std::vector<std::string>& kinds, HapticPattern& pattern,
                     std::string& kind)
{
    for (const std::string& mark : kinds)
    {
        const auto it = layer.marks.find(mark);
        if (it != layer.marks.end())
        {
            pattern = it->second;
            kind = mark;
            return true;
        }
    }
    return false;
}

bool MarkLimiter::Admit(const HostDeviceId& device, const std::string& kind, uint64_t nowMs, uint32_t minIntervalMs)
{
    const bool always = kind == "edge" || kind == "barrier";
    const auto it = _last.find(device);
    if (!always && it != _last.end() && nowMs >= it->second && nowMs - it->second < minIntervalMs)
        return false;
    _last[device] = nowMs;
    return true;
}

}  // namespace controllers
