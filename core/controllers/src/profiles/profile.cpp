#include "profiles/profile.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "haptics/hapticprofile.h"
#include "haptics/surfacehaptics.h"
#include "match.h"  // core/swsignature: header-only, no core symbols
#include "profiles/yaml.h"

namespace controllers
{

namespace
{
constexpr int kSchema = 1;

const char* const kTargetFields[] = {"retro", "key", "keys", "macro", "mouse", "scale", "action", "layer", "mode", "none"};
/// Known and kept, not acted on yet (TODO.md C2c): `invert`
const char* const kOptionFields[] = {"turbo",        "on",           "off",   "count",    "press",
                                     "frames",       "label",        "consume", "ways",   "deadzone",
                                     "saturation",   "diagonal_hold", "gamma", "invert", "repeat",
                                     "curve"};
const char* const kProfileFields[] = {"schema",  "kind",    "id",     "label",     "match",  "applies_to", "extends",
                                      "connectors", "players", "options", "layers", "layer_key", "haptics",
                                      "events"};

template <size_t N>
bool OneOf(const char* const (&names)[N], const std::string& name)
{
    return std::find_if(std::begin(names), std::end(names), [&name](const char* n) { return name == n; }) !=
           std::end(names);
}

template <size_t N>
std::string List(const char* const (&names)[N])
{
    std::string text;
    for (const char* name : names)
    {
        if (!text.empty())
            text += ", ";
        text += name;
    }
    return text;
}

bool Strings(const JsonValue* value, std::vector<std::string>& out, const std::string& where, std::string& error)
{
    if (value == nullptr)
        return true;
    if (!value->IsArray())
    {
        error = where + ": a list of strings expected";
        return false;
    }
    for (const JsonValue& item : value->Items())
    {
        if (!item.IsString())
        {
            error = where + ": a list of strings expected";
            return false;
        }
        out.push_back(item.AsString());
    }
    return true;
}

bool Unsigned(const JsonValue& value, unsigned& out, const std::string& field, std::string& error)
{
    if (!value.IsNumber() || value.AsNumber() < 0 || value.AsNumber() > 1000 ||
        std::floor(value.AsNumber()) != value.AsNumber())
    {
        error = "'" + field + "' is a whole number";
        return false;
    }
    out = static_cast<unsigned>(value.AsNumber());
    return true;
}

bool Fraction(const JsonValue& value, float& out, const std::string& field, std::string& error, float max = 1.0f)
{
    if (!value.IsNumber() || value.AsNumber() < 0 || value.AsNumber() > max)
    {
        error = "'" + field + "' is a number 0.." + std::to_string(static_cast<int>(max));
        return false;
    }
    out = static_cast<float>(value.AsNumber());
    return true;
}

/// `curve: {unit, min, max, rate_lo, rate_hi}` (control surfaces actions.md §2): kept as the action's parameters, which
/// the action checks against its own ranges; here the fields, their types and their order
bool CurveParams(const JsonValue& value, BindingOptions& options, std::string& error)
{
    for (const auto& [field, member] : value.GetMembers())
    {
        if (field == "unit")
        {
            if (!member.IsString() || (member.AsString() != "frames" && member.AsString() != "seconds"))
            {
                error = "curve.unit is frames or seconds";
                return false;
            }
        }
        else if (field == "min" || field == "max" || field == "rate_lo" || field == "rate_hi")
        {
            if (!member.IsNumber() || !(member.AsNumber() > 0))
            {
                error = "curve." + field + " is a number above 0";
                return false;
            }
        }
        else
        {
            error = "curve: unknown field '" + field + "' (known: unit, min, max, rate_lo, rate_hi)";
            return false;
        }
    }
    const auto number = [&value](const char* field, double fallback) {
        const JsonValue* member = value.Find(field);
        return member != nullptr ? member->AsNumber() : fallback;
    };
    if (value.Find("min") != nullptr && value.Find("max") != nullptr && number("min", 0) > number("max", 0))
    {
        error = "curve.min is at most curve.max";
        return false;
    }
    if (value.Find("rate_lo") != nullptr && value.Find("rate_hi") != nullptr &&
        number("rate_lo", 0) >= number("rate_hi", 0))
    {
        error = "curve.rate_lo is below curve.rate_hi";
        return false;
    }
    options.params = value;
    return true;
}

/// One option field into the binding's options
bool ApplyOption(const std::string& field, const JsonValue& value, BindingOptions& options, std::string& error)
{
    if (field == "turbo")
    {
        if (!value.IsString() || !TurboFromName(value.AsString(), options.turbo))
        {
            error = "'turbo' is one of off, classic, toggle, hold, burst";
            return false;
        }
        return true;
    }
    if (field == "press")
    {
        if (!value.IsString() || (value.AsString() != "hold" && value.AsString() != "tap"))
        {
            error = "'press' is hold or tap";
            return false;
        }
        options.press = value.AsString() == "tap" ? PressMode::Tap : PressMode::Hold;
        return true;
    }
    if (field == "on")
        return Unsigned(value, options.on, field, error);
    if (field == "off")
        return Unsigned(value, options.off, field, error);
    if (field == "count")
        return Unsigned(value, options.count, field, error);
    if (field == "frames")
        return Unsigned(value, options.frames, field, error);
    if (field == "ways")
    {
        if (!Unsigned(value, options.analog.ways, field, error))
            return false;
        if (options.analog.ways != 4 && options.analog.ways != 8)
        {
            error = "'ways' is 4 or 8";
            return false;
        }
        return true;
    }
    if (field == "deadzone")
        return Fraction(value, options.analog.deadzone, field, error);
    if (field == "saturation")
        return Fraction(value, options.analog.saturation, field, error);
    if (field == "diagonal_hold")
        return Fraction(value, options.analog.diagonalHold, field, error, 90.0f);
    if (field == "gamma")
        return Fraction(value, options.analog.mouseGamma, field, error, 5.0f);
    if (field == "label")
    {
        if (!value.IsString())
        {
            error = "'label' is a string";
            return false;
        }
        options.label = value.AsString();
        return true;
    }
    if (field == "consume")
    {
        if (!value.IsBool())
        {
            error = "'consume' is true or false";
            return false;
        }
        options.consume = value.AsBool();
        return true;
    }
    if (field == "curve")
    {
        if (value.IsObject())
            return CurveParams(value, options, error);
        if (!value.IsString() || value.AsString() != "scrub")
        {
            error = "'curve' is scrub (an analog scrub) or an action's scrub curve {unit, min, max, rate_lo, rate_hi}";
            return false;
        }
        options.level = true;
        return true;
    }
    if (field == "repeat")
    {
        // {delay, rate} in frames (mapping-and-profiles.md §1.3); `repeat: none` / false: no repeat
        if ((value.IsString() && value.AsString() == "none") || (value.IsBool() && !value.AsBool()))
        {
            options.repeatDelay = options.repeatRate = 0;
            return true;
        }
        const JsonValue* delay = value.IsObject() ? value.Find("delay") : nullptr;
        const JsonValue* rate = value.IsObject() ? value.Find("rate") : nullptr;
        if (delay == nullptr || rate == nullptr || value.GetMembers().size() != 2 ||
            !Unsigned(*delay, options.repeatDelay, "repeat.delay", error) ||
            !Unsigned(*rate, options.repeatRate, "repeat.rate", error) || options.repeatDelay < 1 ||
            options.repeatDelay > 500 || options.repeatRate < 1 || options.repeatRate > 500)
        {
            error = "'repeat' is {delay, rate}: frames (1..500) before the first repeat and between repeats";
            return false;
        }
        return true;
    }
    return true;  // invert: known, not acted on yet
}

bool StringField(const JsonValue& spec, const char* field, std::string& out, std::string& error)
{
    const JsonValue* value = spec.Find(field);
    if (value == nullptr)
        return false;
    if (!value->IsString())
    {
        error = std::string("'") + field + "' is a string";
        return false;
    }
    out = value->AsString();
    return true;
}

bool CompileTarget(const JsonValue& spec, Target& out, std::string& error)
{
    std::string text;
    error.clear();
    if (StringField(spec, "retro", text, error))
        return ParseRetroTarget(text, out, error);
    // YAML reads `key: 1` as a number: a digit key
    if (const JsonValue* key = spec.Find("key"); key != nullptr && key->IsNumber() && key->AsNumber() >= 0 &&
                                                 key->AsNumber() <= 9 && std::floor(key->AsNumber()) == key->AsNumber())
        return ParseKeysTarget(std::to_string(static_cast<int>(key->AsNumber())), out, error);
    if (StringField(spec, "key", text, error))
        return ParseKeysTarget(text, out, error);
    if (const JsonValue* keys = spec.Find("keys"))
    {
        std::vector<std::string> names;
        if (!Strings(keys, names, "'keys'", error) || names.empty())
        {
            error = error.empty() ? "'keys' names at least one key" : error;
            return false;
        }
        std::string joined;
        for (const std::string& name : names)
            joined += (joined.empty() ? "" : "+") + (name == "+" ? std::string("plus") : name);
        return ParseKeysTarget(joined, out, error);
    }
    if (const JsonValue* macro = spec.Find("macro"))
    {
        std::vector<std::string> steps;
        return Strings(macro, steps, "'macro'", error) && ParseMacroTarget(steps, out, error);
    }
    if (StringField(spec, "mouse", text, error))
    {
        float scale = text == "wheel" ? 1.0f : 8.0f;
        if (const JsonValue* value = spec.Find("scale"))
        {
            if (!value->IsNumber() || std::fabs(value->AsNumber()) > 1000)
            {
                error = "'scale' is a number";
                return false;
            }
            scale = static_cast<float>(value->AsNumber());
        }
        return ParseMouseTarget(text, scale, out, error);
    }
    if (StringField(spec, "action", text, error))
    {
        if (text.empty())
        {
            error = "'action' names an action";
            return false;
        }
        out = ActionTarget(text);
        return true;
    }
    if (StringField(spec, "layer", text, error))
    {
        std::string mode = "hold";
        if (spec.Find("mode") != nullptr && !StringField(spec, "mode", mode, error))
            return false;
        return ParseLayerTarget(RouterLayer(text), mode, out, error);
    }
    if (const JsonValue* none = spec.Find("none"))
    {
        if (!none->IsBool() || !none->AsBool())
        {
            error = "'none' is true";
            return false;
        }
        out = Target{};
        return true;
    }
    if (error.empty())
        error = "no target (one of retro, key, keys, macro, mouse, action, layer, none)";
    return false;
}
}  // namespace

std::string RouterLayer(const std::string& jsonLayer)
{
    return jsonLayer == "play" ? std::string(kPlayLayer) : jsonLayer;
}

bool IsTargetField(const std::string& field)
{
    return OneOf(kTargetFields, field);
}

bool CompileBinding(const std::string& control, const JsonValue& spec, const JsonValue& defaults,
                    const std::string& layer, Binding& out, std::string& error)
{
    if (!spec.IsObject())
    {
        error = "a binding is an object";
        return false;
    }
    Binding binding;
    binding.layer = RouterLayer(layer);
    // `view+menu`: a chord (the `+` key itself is `key.plus` style, never a chord member alone)
    size_t start = 0;
    while (start <= control.size())
    {
        size_t end = control.find('+', start);
        if (end == std::string::npos)
            end = control.size();
        binding.sources.push_back(control.substr(start, end - start));
        start = end + 1;
    }
    binding.chord = binding.sources.size() > 1;

    for (const JsonValue* fields : {&defaults, &spec})
    {
        for (const auto& [field, value] : fields->GetMembers())
        {
            if (IsTargetField(field))
                continue;
            if (!OneOf(kOptionFields, field))
            {
                error = "unknown field '" + field + "' (known: " + List(kTargetFields) + ", " + List(kOptionFields) +
                        ")";
                return false;
            }
            if (!ApplyOption(field, value, binding.options, error))
                return false;
        }
    }
    if (!CompileTarget(spec, binding.target, error))
        return false;
    if (!ValidateBinding(binding, error))
        return false;
    out = binding;
    return true;
}

Specificity Profile::GetSpecificity() const
{
    if (IsGame())
        return Specificity::Game;
    if (!devices.empty())
        return Specificity::Device;
    if (!families.empty())
        return Specificity::Family;
    if (!machines.empty())
        return Specificity::Machine;
    return Specificity::Global;
}

bool ParseProfile(const std::string& text, Profile& out, std::string& error)
{
    JsonValue root;
    const size_t first = text.find_first_not_of(" \t\r\n");
    const bool json = first != std::string::npos && text[first] == '{';
    if (!(json ? ParseJson(text, root, error) : ParseYaml(text, root, error)))
        return false;
    return ParseProfile(root, out, error);
}

bool ParseProfile(const JsonValue& root, Profile& out, std::string& error)
{
    if (!root.IsObject())
    {
        error = "a profile is an object";
        return false;
    }
    for (const auto& [field, value] : root.GetMembers())
    {
        if (!OneOf(kProfileFields, field))
        {
            error = "unknown field '" + field + "' (known: " + List(kProfileFields) + ")";
            return false;
        }
    }
    const JsonValue* schema = root.Find("schema");
    if (schema == nullptr || !schema->IsNumber() || schema->AsNumber() != kSchema)
    {
        error = "'schema' must be " + std::to_string(kSchema);
        return false;
    }
    Profile profile;
    std::string kind;
    if (!StringField(root, "kind", kind, error) || (kind != "play" && kind != "debug" && kind != "scheme"))
    {
        error = "'kind' is play, debug or scheme";
        return false;
    }
    profile.kind = kind == "debug" ? ProfileKind::Debug : ProfileKind::Play;
    profile.scheme = kind == "scheme";
    if (!StringField(root, "id", profile.id, error) || profile.id.empty())
    {
        error = "'id' names the profile";
        return false;
    }
    if (root.Find("label") != nullptr && !StringField(root, "label", profile.label, error))
        return false;

    if (const JsonValue* match = root.Find("match"))
    {
        if (!match->IsObject())
        {
            error = "'match' is an object";
            return false;
        }
        for (const auto& [field, value] : match->GetMembers())
        {
            if (field == "machines")
            {
                if (!Strings(&value, profile.machines, "match.machines", error))
                    return false;
            }
            else if (field == "signatures")
            {
                if (!Strings(&value, profile.signatures, "match.signatures", error))
                    return false;
            }
            else if (field == "file_sha256")
            {
                if (!Strings(&value, profile.fileSha256, "match.file_sha256", error))
                    return false;
            }
            else if (field == "file_set")
            {
                if (!Strings(&value, profile.fileSet, "match.file_set", error))
                    return false;
                for (std::string& token : profile.fileSet)
                {
                    std::transform(token.begin(), token.end(), token.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (!unrealng::swsignature::IsFileSetToken(token))
                    {
                        error = "match.file_set: '" + token + "' is not a file-set token (n:, c: or d: and 16 hex)";
                        return false;
                    }
                }
            }
            else if (field == "title")
            {
                if (!value.IsString())
                {
                    error = "match.title is a string";
                    return false;
                }
                profile.title = value.AsString();
            }
            else if (field != "zxdb_id")
            {
                error = "unknown field 'match." + field + "' (known: machines, signatures, file_sha256, file_set, title, zxdb_id)";
                return false;
            }
        }
    }
    if (const JsonValue* applies = root.Find("applies_to"))
    {
        if (!applies->IsObject() || !Strings(applies->Find("families"), profile.families, "applies_to.families", error) ||
            !Strings(applies->Find("devices"), profile.devices, "applies_to.devices", error))
        {
            error = error.empty() ? "'applies_to' is an object" : error;
            return false;
        }
    }
    if (profile.kind == ProfileKind::Debug && profile.IsGame())
    {
        error = "a debug profile has no game level";
        return false;
    }
    if (profile.scheme && (root.Find("match") != nullptr || root.Find("applies_to") != nullptr))
    {
        error = "a scheme has no 'match' or 'applies_to': the profiles that extend it have";
        return false;
    }
    if (const JsonValue* extends = root.Find("extends"))
    {
        if (extends->IsString())
            profile.extends = {extends->AsString()};
        else if (!Strings(extends, profile.extends, "'extends'", error))
            return false;
        if (std::find(profile.extends.begin(), profile.extends.end(), profile.id) != profile.extends.end())
        {
            error = "'" + profile.id + "' extends itself";
            return false;
        }
    }

    if (const JsonValue* connectors = root.Find("connectors"))
    {
        if (!connectors->IsObject())
        {
            error = "'connectors' is an object";
            return false;
        }
        for (const auto& [connector, entry] : connectors->GetMembers())
        {
            ConnectorPlan plan;
            if (!entry.IsObject() || !StringField(entry, "device", plan.device, error))
            {
                error = "connectors." + connector + ": names its 'device'";
                return false;
            }
            plan.options = entry;
            plan.options.Remove("device");
            profile.connectors[connector] = plan;
        }
    }
    if (const JsonValue* players = root.Find("players"))
    {
        if (!players->IsArray())
        {
            error = "'players' is a list";
            return false;
        }
        for (const JsonValue& entry : players->Items())
        {
            PlayerPin pin;
            if (!entry.IsObject() || !StringField(entry, "device", pin.device, error) ||
                !StringField(entry, "connector", pin.connector, error))
            {
                error = "players: each names its 'device' and 'connector'";
                return false;
            }
            profile.players.push_back(pin);
        }
    }
    profile.options = JsonValue::Object();
    if (const JsonValue* options = root.Find("options"))
    {
        if (!options->IsObject())
        {
            error = "'options' is an object";
            return false;
        }
        BindingOptions probe;
        for (const auto& [field, value] : options->GetMembers())
        {
            if (!OneOf(kOptionFields, field))
            {
                error = "options: unknown field '" + field + "' (known: " + List(kOptionFields) + ")";
                return false;
            }
            if (!ApplyOption(field, value, probe, error))
            {
                error = "options: " + error;
                return false;
            }
        }
        profile.options = *options;
    }

    JsonValue merged;
    const JsonValue* haptics = root.Find("haptics");
    if (const JsonValue* events = root.Find("events"))
    {
        // A play profile's `events` are the game's facts (breakpoint taps README.md §7): the sources of its haptics
        // and of the game.events monitor. Kept as haptics.sources, beside the ones written there
        if (profile.kind != ProfileKind::Play)
        {
            error = "'events' belongs to a play profile";
            return false;
        }
        if (!events->IsObject())
        {
            error = "events: an object expected";
            return false;
        }
        merged = haptics != nullptr ? *haptics : JsonValue::Object();
        if (!merged.IsObject())
        {
            error = "haptics: an object expected";
            return false;
        }
        JsonValue sources = JsonValue::Object();
        if (const JsonValue* written = merged.Find("sources"))
            sources = *written;
        if (!sources.IsObject())
        {
            error = "haptics.sources: an object expected";
            return false;
        }
        for (const auto& [name, spec] : events->GetMembers())
        {
            if (sources.Find(name) != nullptr)
            {
                error = "events: '" + name + "' is also a haptics source";
                return false;
            }
            sources.Set(name, spec);
        }
        merged.Set("sources", std::move(sources));
        haptics = &merged;
    }
    if (haptics != nullptr)
    {
        // A play profile's are the game's (sources and rules); a debug profile's what a surface feels per layer
        HapticProfile game;
        SurfaceHaptics surface;
        if (profile.kind == ProfileKind::Debug ? !ParseSurfaceHaptics(*haptics, surface, error)
                                               : !ParseHapticProfile(*haptics, game, error))
            return false;
        profile.haptics = *haptics;
    }
    if (const JsonValue* layers = root.Find("layers"))
    {
        if (!layers->IsObject())
        {
            error = "'layers' is an object";
            return false;
        }
        for (const auto& [layer, bindings] : layers->GetMembers())
        {
            if (profile.kind == ProfileKind::Debug && layer != "debug" && !IsContextLayer(layer))
            {
                error = "a debug profile binds the debug layer and context layers (@ttd, @debug.*) only, not '" +
                        layer + "'";
                return false;
            }
            if (IsContextLayer(layer) && profile.kind != ProfileKind::Debug)
            {
                error = "layer '" + layer + "': context layers belong to debug profiles";
                return false;
            }
            if (IsContextLayer(layer) && !ValidContextLayer(layer, error))
                return false;
            if (!bindings.IsObject())
            {
                error = "layers." + layer + " is an object";
                return false;
            }
            for (const auto& [control, spec] : bindings.GetMembers())
            {
                Binding binding;
                // A binding with no target changes fields of the one below it (§6.2): its target comes with the merge
                const bool retargets = spec.IsObject() &&
                                       std::any_of(spec.GetMembers().begin(), spec.GetMembers().end(),
                                                   [](const auto& member) { return IsTargetField(member.first); });
                JsonValue probe = spec;
                if (!retargets && spec.IsObject())
                    probe.Set("none", JsonValue::Bool(true));
                if (!CompileBinding(control, probe, profile.options, layer, binding, error))
                {
                    error = "layers." + layer + "." + control + ": " + error;
                    return false;
                }
            }
            profile.layers.emplace_back(layer, bindings.GetMembers());
        }
    }
    if (const JsonValue* key = root.Find("layer_key"))
    {
        std::string control;
        std::string mode = "hold";
        if (profile.kind != ProfileKind::Debug || !key->IsObject() || !StringField(*key, "control", control, error) ||
            (key->Find("mode") != nullptr && !StringField(*key, "mode", mode, error)))
        {
            error = "'layer_key' is a debug profile's {control, mode}";
            return false;
        }
        // The layer key is a play-layer binding to the debug layer
        JsonValue spec = JsonValue::Object({{"layer", JsonValue::String("debug")}, {"mode", JsonValue::String(mode)}});
        Binding binding;
        if (!CompileBinding(control, spec, JsonValue::Object(), "play", binding, error))
        {
            error = "layer_key: " + error;
            return false;
        }
        profile.layers.insert(profile.layers.begin(), {"play", JsonValue::Members{{control, spec}}});
    }
    out = std::move(profile);
    return true;
}

}  // namespace controllers
