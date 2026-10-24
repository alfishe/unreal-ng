#include "hostinputcontrol.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>

#include "devicedatabase.h"
#include "devicemapping.h"
#include "haptics/testeffects.h"
#include "hostcontrols.h"
#include "hostinputservice.h"
#include "profiles/yaml.h"

namespace controllers
{

namespace
{
std::mutex& OwnerMutex()
{
    static std::mutex mutex;
    return mutex;
}

IHostInputOwner*& OwnerSlot()
{
    static IHostInputOwner* owner = nullptr;
    return owner;
}

HostInputControl::Runner& RunnerSlot()
{
    static HostInputControl::Runner runner;
    return runner;
}

HostReply Fail(const std::string& error, const std::string& message)
{
    HostReply reply;
    reply.error = error;
    reply.message = message;
    return reply;
}

std::string Option(const HostRequest& request, const std::string& name, const std::string& fallback = {})
{
    const auto it = request.options.find(name);
    return it != request.options.end() ? it->second : fallback;
}

JsonValue Strings(const std::vector<std::string>& values)
{
    std::vector<JsonValue> items;
    for (const std::string& value : values)
        items.push_back(JsonValue::String(value));
    return JsonValue::Array(std::move(items));
}

/// The connected device @p id, or nullptr
const HostDeviceInfo* FindDevice(const std::vector<HostDeviceInfo>& devices, const std::string& id)
{
    for (const HostDeviceInfo& device : devices)
        if (device.id == id)
            return &device;
    return nullptr;
}

const char* KindName(const Profile& profile)
{
    if (profile.scheme)
        return "scheme";
    return profile.kind == ProfileKind::Play ? "play" : "debug";
}

const char* ScopeName(ProfileScope scope)
{
    switch (scope)
    {
        case ProfileScope::Builtin: return "builtin";
        case ProfileScope::Community: return "community";
        case ProfileScope::User: return "user";
    }
    return "builtin";
}

const char* SpecificityName(Specificity specificity)
{
    switch (specificity)
    {
        case Specificity::Global: return "global";
        case Specificity::Machine: return "machine";
        case Specificity::Family: return "family";
        case Specificity::Device: return "device";
        case Specificity::Game: return "game";
    }
    return "global";
}

/// `set`: play (not schemes, not games), debug, scheme, game, all (the default)
bool InSet(const Profile& profile, const std::string& set)
{
    if (set.empty() || set == "all")
        return true;
    if (set == "scheme")
        return profile.scheme;
    if (set == "game")
        return profile.IsGame();
    if (set == "debug")
        return profile.kind == ProfileKind::Debug;
    return set == "play" && profile.kind == ProfileKind::Play && !profile.scheme && !profile.IsGame();
}

bool MachineFits(const std::vector<std::string>& machines, const std::string& machine)
{
    if (machine.empty() || machines.empty())
        return true;
    std::string lower = machine;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    return std::any_of(machines.begin(), machines.end(), [&lower](std::string name) {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
        return name == lower;
    });
}

JsonValue Summary(const Profile& profile, const ResolvedProfiles& resolved)
{
    JsonValue item = JsonValue::Object({{"id", JsonValue::String(profile.id)}});
    item.Set("label", JsonValue::String(profile.label));
    item.Set("kind", JsonValue::String(KindName(profile)));
    item.Set("scope", JsonValue::String(ScopeName(profile.scope)));
    item.Set("specificity", JsonValue::String(SpecificityName(profile.GetSpecificity())));
    item.Set("game", JsonValue::Bool(profile.IsGame()));
    item.Set("machines", Strings(profile.machines));
    item.Set("families", Strings(profile.families));
    item.Set("devices", Strings(profile.devices));
    item.Set("extends", Strings(profile.extends));
    item.Set("path", JsonValue::String(profile.path));
    item.Set("loaded", JsonValue::Bool(true));
    item.Set("applied", JsonValue::Bool(std::find(resolved.applied.begin(), resolved.applied.end(), profile.id) !=
                                        resolved.applied.end()));
    return item;
}

std::vector<std::string> OutputNames(uint8_t outputs)
{
    std::vector<std::string> names;
    if ((outputs & kOutputHaptic) != 0)
        names.emplace_back("haptic");
    if ((outputs & kOutputSteps) != 0)
        names.emplace_back("steps");
    return names;
}

JsonValue FeelValue(const LayerHaptics& feel)
{
    JsonValue marks = JsonValue::Object();
    for (const auto& [kind, pattern] : feel.marks)
        marks.Set(kind, JsonValue::String(HapticPatternName(pattern)));
    return JsonValue::Object({{"steps", JsonValue::Number(feel.steps.perTurn)},
                              {"clicks", JsonValue::Bool(feel.steps.clicks)},
                              {"marks", marks},
                              {"min_interval_ms", JsonValue::Number(feel.minIntervalMs)}});
}

/// {known, text} and, when known, level (absent without one), coarse, charging, wired
JsonValue BatteryValue(const BatteryState& battery)
{
    JsonValue value = JsonValue::Object({{"known", JsonValue::Bool(battery.known)}});
    if (battery.known)
    {
        if (battery.level >= 0)
            value.Set("level", JsonValue::Number(battery.level));
        value.Set("coarse", JsonValue::Bool(battery.coarse));
        value.Set("charging", JsonValue::Bool(battery.charging));
        value.Set("wired", JsonValue::Bool(battery.wired));
    }
    value.Set("text", JsonValue::String(BatteryText(battery)));
    return value;
}

/// "follow-focus", "pinned:<instance>", "all"
std::string PinText(const InputRouter& router, const HostDeviceId& device)
{
    std::string pinned;
    switch (router.GetInstanceMode(device, &pinned))
    {
        case InstanceMode::FollowFocus: return "follow-focus";
        case InstanceMode::Pinned: return "pinned:" + pinned;
        case InstanceMode::All: return "all";
    }
    return "follow-focus";
}

std::string Hex4(uint16_t value)
{
    static const char digits[] = "0123456789abcdef";
    std::string text(4, '0');
    for (int i = 3; i >= 0; i--, value = static_cast<uint16_t>(value >> 4))
        text[static_cast<size_t>(i)] = digits[value & 0xF];
    return text;
}

JsonValue MapValue(const std::map<std::string, std::string>& map)
{
    JsonValue value = JsonValue::Object();
    for (const auto& [canonical, raw] : map)
        value.Set(canonical, JsonValue::String(raw));
    return value;
}

JsonValue MatchValue(const DeviceMatch& match)
{
    JsonValue value = JsonValue::Object({{"family", JsonValue::String(match.family)},
                                         {"name", JsonValue::String(match.name)},
                                         {"vendor", JsonValue::String(match.vendor)},
                                         {"layout", JsonValue::String(match.layout)},
                                         {"role", JsonValue::String(DeviceRoleName(match.role))},
                                         {"step", JsonValue::String(match.step)},
                                         {"score", JsonValue::Number(match.score)}});
    if (!match.mode.empty())
        value.Set("mode", JsonValue::String(match.mode));
    if (match.ignore)
        value.Set("ignore", JsonValue::Bool(true));
    value.Set("controls", Strings(match.controls));
    if (!match.map.empty())
        value.Set("map", MapValue(match.map));
    return value;
}

JsonValue IdentityValue(const DeviceIdentity& identity)
{
    JsonValue value = JsonValue::Object({{"vid", JsonValue::String(Hex4(identity.vendor))},
                                         {"pid", JsonValue::String(Hex4(identity.product))}});
    if (!identity.bus.empty())
        value.Set("bus", JsonValue::String(identity.bus));
    if (identity.version >= 0)
        value.Set("version", JsonValue::String(Hex4(static_cast<uint16_t>(identity.version))));
    if (!identity.name.empty())
        value.Set("name", JsonValue::String(identity.name));
    value.Set("sdl_guid", JsonValue::String(SdlGuid(identity)));
    return value;
}

bool Flag(const std::string& text, bool fallback)
{
    if (text.empty())
        return fallback;
    return text == "true" || text == "1" || text == "yes" || text == "on";
}

std::string Sanitized(const std::string& text)
{
    std::string out = text;
    for (char& c : out)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
            c = '_';
    return out;
}
}  // namespace

JsonValue HostReply::ToValue() const
{
    JsonValue value = JsonValue::Object({{"ok", JsonValue::Bool(Ok())}});
    if (!Ok())
    {
        value.Set("error", JsonValue::String(error));
        value.Set("message", JsonValue::String(message));
    }
    else if (!message.empty())
        value.Set("message", JsonValue::String(message));
    for (const auto& [field, member] : body.GetMembers())
        value.Set(field, member);
    return value;
}

std::string HostReply::ToJson() const
{
    return ToValue().Dump();
}

int HostReply::HttpStatus() const
{
    if (error.empty())
        return 200;
    if (error == "unknown-profile" || error == "unknown-device")
        return 404;
    if (error == "read-only")
        return 409;
    if (error == "unavailable")
        return 503;
    return 400;
}

const std::vector<std::string>& HostInputControl::Verbs()
{
    static const std::vector<std::string> verbs = {
        "devices", "context",  "profiles", "profile", "save",     "delete", "apply",    "bindings",        "bind",
        "unbind",  "scheme",   "haptics",  "rumble",  "output",   "light",  "role",     "pin",             "identify",
        "probe",   "database", "database_import",     "learn"};
    return verbs;
}

const std::vector<std::string>& HostInputControl::OptionsFor(const std::string& verb)
{
    static const std::map<std::string, std::vector<std::string>> options = {
        {"devices", {}},
        {"context", {}},
        {"profiles", {"set", "machine"}},
        {"profile", {"id"}},
        {"save", {"text"}},
        {"delete", {"id"}},
        {"apply", {"id"}},
        {"bindings", {"device", "layer"}},
        {"bind", {"control", "spec", "layer", "family", "device", "profile"}},
        {"unbind", {"control", "layer", "family", "device", "profile"}},
        {"scheme", {"id", "family", "device", "profile"}},
        {"haptics", {"device"}},
        {"rumble", {"device", "effect"}},
        {"output", {"device", "pattern", "steps", "clicks", "marks"}},
        {"light", {"device", "color"}},
        {"role", {"device", "role"}},
        {"pin", {"device", "pin"}},
        {"identify", {"device"}},
        {"probe", {}},
        {"database", {"vidpid", "guid", "device", "name", "version"}},
        {"database_import", {"text", "save"}},
        {"learn", {"device", "step", "controls", "control", "value", "name", "save"}},
    };
    static const std::vector<std::string> none;
    const auto it = options.find(verb);
    return it != options.end() ? it->second : none;
}

void HostInputControl::SetOwner(IHostInputOwner* owner, Runner run)
{
    std::lock_guard<std::mutex> lock(OwnerMutex());
    OwnerSlot() = owner;
    RunnerSlot() = owner != nullptr ? std::move(run) : Runner();
}

HostReply HostInputControl::Process(const HostRequest& request)
{
    Runner run;
    {
        std::lock_guard<std::mutex> lock(OwnerMutex());
        if (OwnerSlot() == nullptr)
            return Fail("unavailable", "no host input in this process: the game controllers live in the Qt client");
        run = RunnerSlot();
    }
    HostReply reply = Fail("unavailable", "the host input owner went away");
    const auto task = [&reply, &request] {
        // On the owner's thread, where SetOwner(nullptr) also runs: the owner cannot go while the request runs
        IHostInputOwner* owner = nullptr;
        {
            std::lock_guard<std::mutex> lock(OwnerMutex());
            owner = OwnerSlot();
        }
        if (owner != nullptr)
            reply = HostInputControl(*owner).Execute(request);
    };
    if (run)
        run(task);
    else
        task();
    return reply;
}

HostReply HostInputControl::Execute(const HostRequest& request)
{
    for (const auto& [name, value] : request.options)
    {
        const std::vector<std::string>& known = OptionsFor(request.verb);
        if (std::find(known.begin(), known.end(), name) == known.end() &&
            std::find(Verbs().begin(), Verbs().end(), request.verb) != Verbs().end())
        {
            std::string list;
            for (const std::string& option : known)
                list += (list.empty() ? "" : ", ") + option;
            return Fail("bad-request", "unknown option '" + name + "' of " + request.verb + " (known: " +
                                           (list.empty() ? "none" : list) + ")");
        }
    }
    const std::string& verb = request.verb;
    if (verb == "devices")
        return Devices();
    if (verb == "context")
        return Context();
    if (verb == "profiles")
        return List(request);
    if (verb == "profile")
        return Get(request);
    if (verb == "save")
        return Save(request);
    if (verb == "delete")
        return Delete(request);
    if (verb == "apply")
        return Apply(request);
    if (verb == "bindings")
        return Bindings(request);
    if (verb == "bind" || verb == "unbind")
        return Bind(request, verb == "unbind");
    if (verb == "scheme")
        return Scheme(request);
    if (verb == "haptics")
        return Haptics(request);
    if (verb == "rumble")
        return Rumble(request);
    if (verb == "output")
        return Output(request);
    if (verb == "light")
        return Light(request);
    if (verb == "role")
        return Role(request);
    if (verb == "pin")
        return Pin(request);
    if (verb == "identify")
        return Identify(request);
    if (verb == "probe")
        return Probe();
    if (verb == "database")
        return Database(request);
    if (verb == "database_import")
        return DatabaseImport(request);
    if (verb == "learn")
        return Learn(request);
    std::string list;
    for (const std::string& known : Verbs())
        list += (list.empty() ? "" : ", ") + known;
    return Fail("bad-request", "unknown verb '" + verb + "' (known: " + list + ")");
}

HostReply HostInputControl::Devices()
{
    HostReply reply;
    std::vector<JsonValue> devices;
    HostInputService* input = _owner.Input();
    for (const HostDeviceInfo& device : _owner.Context().devices)
    {
        JsonValue item = JsonValue::Object({{"id", JsonValue::String(device.id)},
                                            {"name", JsonValue::String(device.name)},
                                            {"family", JsonValue::String(device.family)},
                                            {"layout", JsonValue::String(device.layout)},
                                            {"backend", JsonValue::String(device.backend)}});
        // The family's controls when the device database names them (a Surface Dial: knob, push, touch)
        const DeviceMatch family = DeviceDatabase::Builtin().ForFamily(device.family, device.layout);
        if (!family.controls.empty())
            item.Set("controls", Strings(family.controls));
        if (input != nullptr)
        {
            // The role (the family's default until somebody sets one), the layers up, the context of the instance it
            // acts on, the surface outputs and the feel of its top layer (control surfaces profiles.md §1)
            InputRouter& router = input->Router();
            item.Set("role", JsonValue::String(DeviceRoleName(router.Role(device.id))));
            item.Set("role_set", JsonValue::Bool(router.RoleSet(device.id)));
            item.Set("pin", JsonValue::String(PinText(router, device.id)));
            // An unknown device's control map (the learn wizard's, an import's): canonical -> raw
            const std::vector<MappedControl> map = input->ControlMap(device.id);
            if (!map.empty())
            {
                JsonValue mapped = JsonValue::Object();
                for (const MappedControl& control : map)
                    mapped.Set(control.canonical, JsonValue::String(control.raw + (control.half > 0   ? "+"
                                                                                   : control.half < 0 ? "-"
                                                                                                      : "")));
                item.Set("map", mapped);
            }
            std::vector<std::string> layers;
            for (const std::string& layer : router.Layers(device.id))
                layers.push_back(layer.empty() ? "play" : layer);
            item.Set("layers", Strings(layers));
            item.Set("context", JsonValue::String(router.Context(device.id)));
            item.Set("outputs", Strings(OutputNames(input->Outputs(device.id))));
            LayerHaptics feel;
            if (router.Feel(device.id, feel))
                item.Set("feel", FeelValue(feel));
            // What the device cannot do here and how to fix it (a missing udev rule), its battery, its light
            const std::string notice = input->DeviceNotice(device.id);
            if (!notice.empty())
                item.Set("notice", JsonValue::String(notice));
            item.Set("battery", BatteryValue(input->Battery(device.id)));
            const std::string noLight = input->LightUnavailable(device.id);
            JsonValue light = JsonValue::Object({{"available", JsonValue::Bool(noLight.empty())}});
            if (!noLight.empty())
                light.Set("reason", JsonValue::String(noLight));
            item.Set("light", light);
        }
        devices.push_back(std::move(item));
    }
    reply.body.Set("devices", JsonValue::Array(std::move(devices)));
    return reply;
}

HostReply HostInputControl::Haptics(const HostRequest& request)
{
    const std::vector<HostDeviceInfo> devices = _owner.Context().devices;
    const std::string only = Option(request, "device");
    if (!only.empty() && FindDevice(devices, only) == nullptr)
        return Fail("unknown-device", "no connected host device '" + only + "' (devices lists them)");
    HostInputService* input = _owner.Input();
    HostReply reply;
    std::vector<JsonValue> list;
    for (const HostDeviceInfo& device : devices)
    {
        if (!only.empty() && device.id != only)
            continue;
        const std::string why = input != nullptr ? input->HapticsUnavailable(device.id)
                                                 : std::string("No host haptics output in this process");
        const uint8_t actuators = why.empty() ? input->RumbleActuators(device.id) : 0;
        std::vector<JsonValue> effects;
        for (const TestEffect& test : TestEffects(actuators))
            effects.push_back(JsonValue::Object({{"id", JsonValue::String(test.id)},
                                                 {"label", JsonValue::String(test.label)},
                                                 {"ms", JsonValue::Number(DurationMs(test))}}));
        list.push_back(JsonValue::Object({{"id", JsonValue::String(device.id)},
                                          {"name", JsonValue::String(device.name)},
                                          {"available", JsonValue::Bool(why.empty())},
                                          {"reason", JsonValue::String(why)},
                                          {"actuators", Strings(ActuatorNames(actuators))},
                                          {"effects", JsonValue::Array(std::move(effects))}}));
    }
    reply.body.Set("devices", JsonValue::Array(std::move(list)));
    return reply;
}

HostReply HostInputControl::Rumble(const HostRequest& request)
{
    const std::string id = Option(request, "device");
    if (id.empty())
        return Fail("bad-request", "rumble needs device= (haptics lists the devices and their effects)");
    const std::vector<HostDeviceInfo> devices = _owner.Context().devices;
    const HostDeviceInfo* device = FindDevice(devices, id);
    if (device == nullptr)
        return Fail("unknown-device", "no connected host device '" + id + "' (devices lists them)");
    HostInputService* input = _owner.Input();
    const std::string why = input != nullptr ? input->HapticsUnavailable(id)
                                             : std::string("No host haptics output in this process");
    if (!why.empty())
        return Fail("unavailable", device->name + ": " + why);
    const std::string name = Option(request, "effect", "identify");
    const std::vector<TestEffect> effects = TestEffects(input->RumbleActuators(id));
    for (const TestEffect& test : effects)
        if (test.id == name)
        {
            input->PlayTest(id, test);
            HostReply reply;
            reply.message = test.label + " on " + device->name;
            reply.body.Set("device", JsonValue::String(id));
            reply.body.Set("effect", JsonValue::String(test.id));
            reply.body.Set("ms", JsonValue::Number(DurationMs(test)));
            return reply;
        }
    std::string list;
    for (const TestEffect& test : effects)
        list += (list.empty() ? "" : ", ") + test.id;
    return Fail("bad-request", "no effect '" + name + "' on " + device->name + " (it has: " + list + ")");
}

HostReply HostInputControl::Output(const HostRequest& request)
{
    const std::string id = Option(request, "device");
    if (id.empty())
        return Fail("bad-request", "output needs device= (devices lists the devices and their outputs)");
    const std::vector<HostDeviceInfo> devices = _owner.Context().devices;
    const HostDeviceInfo* device = FindDevice(devices, id);
    if (device == nullptr)
        return Fail("unknown-device", "no connected host device '" + id + "' (devices lists them)");
    HostInputService* input = _owner.Input();
    if (input == nullptr)
        return Fail("unavailable", "No host output in this process");
    const uint8_t outputs = input->Outputs(id);
    const std::string pattern = Option(request, "pattern");
    const std::string marks = Option(request, "marks");
    const bool steps = request.options.count("steps") != 0 || request.options.count("clicks") != 0;
    if ((pattern.empty() ? 0 : 1) + (marks.empty() ? 0 : 1) + (steps ? 1 : 0) != 1)
        return Fail("bad-request",
                    "output takes one of pattern= (tick, double, buzz), steps= / clicks= (the feel), marks= (a seek's "
                    "marks, as the device's layer plays them)");
    HostReply reply;
    reply.body.Set("device", JsonValue::String(id));
    reply.body.Set("outputs", Strings(OutputNames(outputs)));
    if (!pattern.empty())
    {
        HapticPattern parsed;
        if (!HapticPatternFromName(pattern, parsed))
            return Fail("bad-request", "pattern is tick, double or buzz, not '" + pattern + "'");
        if ((outputs & kOutputHaptic) == 0 || !input->Output(id, HapticOut{parsed}))
            return Fail("unavailable", device->name + " plays no haptic patterns");
        reply.message = std::string(HapticPatternName(parsed)) + " on " + device->name;
        reply.body.Set("pattern", JsonValue::String(HapticPatternName(parsed)));
        return reply;
    }
    if (!marks.empty())
    {
        std::vector<std::string> kinds;
        for (size_t start = 0; start <= marks.size();)
        {
            size_t end = marks.find(',', start);
            if (end == std::string::npos)
                end = marks.size();
            const std::string kind = marks.substr(start, end - start);
            if (std::find(MarkKinds().begin(), MarkKinds().end(), kind) == MarkKinds().end())
                return Fail("bad-request", "unknown mark '" + kind + "' (known: second, ten_seconds, bookmark, edge, "
                                           "barrier)");
            kinds.push_back(kind);
            start = end + 1;
        }
        const HostInputService::MarksPlayed played = input->PlayMarks(id, kinds);
        reply.body.Set("played", JsonValue::Bool(played.played));
        if (!played.played)
        {
            reply.message = "nothing played on " + device->name + ": " + played.reason;
            reply.body.Set("reason", JsonValue::String(played.reason));
            return reply;
        }
        reply.message = played.mark + " -> " + HapticPatternName(played.pattern) + " on " + device->name;
        reply.body.Set("mark", JsonValue::String(played.mark));
        reply.body.Set("pattern", JsonValue::String(HapticPatternName(played.pattern)));
        return reply;
    }
    StepsOut feel;
    LayerHaptics current;
    if (input->Router().Feel(id, current))
        feel = current.steps;
    if (const auto it = request.options.find("steps"); it != request.options.end())
    {
        // A JSON number may come as "36" or "36.0"
        char* end = nullptr;
        const double value = std::strtod(it->second.c_str(), &end);
        if (it->second.empty() || *end != '\0' || value < 1 || value > 3600 || value != static_cast<double>(static_cast<long>(value)))
            return Fail("bad-request", "steps is a whole number 1..3600, not '" + it->second + "'");
        feel.perTurn = static_cast<uint16_t>(value);
    }
    if (const auto it = request.options.find("clicks"); it != request.options.end())
    {
        const std::string& value = it->second;
        if (value == "on" || value == "true" || value == "1")
            feel.clicks = true;
        else if (value == "off" || value == "false" || value == "0")
            feel.clicks = false;
        else
            return Fail("bad-request", "clicks is on or off, not '" + value + "'");
    }
    if ((outputs & kOutputSteps) == 0 || !input->Output(id, feel))
        return Fail("unavailable", device->name + " has no steps to set");
    reply.message = std::to_string(feel.perTurn) + " steps per turn, " + (feel.clicks ? "a click at each" : "free wheel") +
                    " on " + device->name + " (until its context or layers change)";
    reply.body.Set("steps", JsonValue::Number(feel.perTurn));
    reply.body.Set("clicks", JsonValue::Bool(feel.clicks));
    return reply;
}

HostReply HostInputControl::Light(const HostRequest& request)
{
    const std::string id = Option(request, "device");
    if (id.empty())
        return Fail("bad-request", "light needs device= (devices lists the devices and whether their light is settable)");
    const std::string text = Option(request, "color");
    LightColor color;
    if (!ParseLightColor(text, color))
        return Fail("bad-request", "light needs color= #RRGGBB, r,g,b (0..255 each) or off, not '" + text + "'");
    const std::vector<HostDeviceInfo> devices = _owner.Context().devices;
    const HostDeviceInfo* device = FindDevice(devices, id);
    if (device == nullptr)
        return Fail("unknown-device", "no connected host device '" + id + "' (devices lists them)");
    HostInputService* input = _owner.Input();
    const std::string why =
        input != nullptr ? input->LightUnavailable(id) : std::string("No host light output in this process");
    if (!why.empty())
        return Fail("unavailable", device->name + ": " + why);
    if (!input->SetLight(id, color))
        return Fail("unavailable", device->name + ": the light did not take the colour");
    HostReply reply;
    const std::string colorText = LightColorText(color);
    reply.message = (color == LightColor{} ? std::string("light off") : "light " + colorText) + " on " + device->name;
    reply.body.Set("device", JsonValue::String(id));
    reply.body.Set("color", JsonValue::String(colorText));
    return reply;
}

HostReply HostInputControl::Role(const HostRequest& request)
{
    const std::string id = Option(request, "device");
    const std::string name = Option(request, "role");
    DeviceRole role;
    if (id.empty() || !DeviceRoleFromName(name, role))
        return Fail("bad-request", "role needs device= and role= player, debugger-remote or disabled" +
                                       (name.empty() ? std::string() : ", not '" + name + "'"));
    const std::vector<HostDeviceInfo> devices = _owner.Context().devices;
    const HostDeviceInfo* device = FindDevice(devices, id);
    if (device == nullptr)
        return Fail("unknown-device", "no connected host device '" + id + "' (devices lists them)");
    HostInputService* input = _owner.Input();
    if (input == nullptr)
        return Fail("unavailable", "No host input router in this process");
    input->Router().SetRole(id, role);
    HostReply reply;
    reply.message = device->name + " is a " + DeviceRoleName(role) +
                    (role == DeviceRole::DebuggerRemote ? " (its debug bindings only, no connector)"
                     : role == DeviceRole::Disabled     ? " (it reaches nothing)"
                                                        : "");
    reply.body.Set("device", JsonValue::String(id));
    reply.body.Set("role", JsonValue::String(DeviceRoleName(role)));
    return reply;
}

HostReply HostInputControl::Pin(const HostRequest& request)
{
    const std::string id = Option(request, "device");
    const std::string pin = Option(request, "pin");
    InstanceMode mode = InstanceMode::FollowFocus;
    std::string instance;
    if (pin == "follow-focus")
        mode = InstanceMode::FollowFocus;
    else if (pin == "all")
        mode = InstanceMode::All;
    else if (pin.rfind("pinned:", 0) == 0 && pin.size() > 7)
    {
        mode = InstanceMode::Pinned;
        instance = pin.substr(7);
    }
    else
        return Fail("bad-request", "pin needs device= and pin= follow-focus, all or pinned:<emulator id>" +
                                       (pin.empty() ? std::string() : ", not '" + pin + "'"));
    if (id.empty())
        return Fail("bad-request", "pin needs device= (devices lists them)");
    const std::vector<HostDeviceInfo> devices = _owner.Context().devices;
    const HostDeviceInfo* device = FindDevice(devices, id);
    if (device == nullptr)
        return Fail("unknown-device", "no connected host device '" + id + "' (devices lists them)");
    HostInputService* input = _owner.Input();
    if (input == nullptr)
        return Fail("unavailable", "No host input router in this process");
    input->Router().SetInstanceMode(id, mode, instance);
    HostReply reply;
    const std::string text = PinText(input->Router(), id);
    reply.message = device->name + (mode == InstanceMode::FollowFocus ? " follows the focused emulator window"
                                    : mode == InstanceMode::All       ? " drives every emulator instance"
                                                                      : " drives emulator " + instance + " only");
    reply.body.Set("device", JsonValue::String(id));
    reply.body.Set("pin", JsonValue::String(text));
    return reply;
}

HostReply HostInputControl::Identify(const HostRequest& request)
{
    const std::string id = Option(request, "device");
    if (id.empty())
        return Fail("bad-request", "identify needs device= (devices lists them)");
    const std::vector<HostDeviceInfo> devices = _owner.Context().devices;
    const HostDeviceInfo* device = FindDevice(devices, id);
    if (device == nullptr)
        return Fail("unknown-device", "no connected host device '" + id + "' (devices lists them)");
    HostInputService* input = _owner.Input();
    if (input == nullptr)
        return Fail("unavailable", "No host output in this process");
    const HostInputService::Identified done = input->Identify(id);
    if (!done.rumble && !done.light)
    {
        const std::string haptics = input->HapticsUnavailable(id);
        const std::string light = input->LightUnavailable(id);
        return Fail("unavailable", device->name + " cannot be identified here: " +
                                       (haptics.empty() ? std::string("no motors") : haptics) + "; " +
                                       (light.empty() ? std::string("no light") : light));
    }
    HostReply reply;
    reply.message = device->name + ": " +
                    (done.rumble && done.light ? "rumbled and blinked its light"
                     : done.rumble             ? "rumbled (no light to blink)"
                                               : "blinked its light (no motors)") +
                    " for " + std::to_string(HostInputService::kIdentifyMs) + " ms";
    reply.body.Set("device", JsonValue::String(id));
    reply.body.Set("rumble", JsonValue::Bool(done.rumble));
    reply.body.Set("light", JsonValue::Bool(done.light));
    reply.body.Set("ms", JsonValue::Number(HostInputService::kIdentifyMs));
    return reply;
}

HostReply HostInputControl::Probe()
{
    HostInputService* input = _owner.Input();
    if (input == nullptr)
        return Fail("unavailable", "No host input backends in this process");
    const std::vector<HostDeviceInfo> devices = input->Router().Devices();
    std::vector<JsonValue> backends;
    for (const BackendProbe& probe : input->Probe())
    {
        JsonValue item = JsonValue::Object({{"name", JsonValue::String(probe.name)},
                                            {"running", JsonValue::Bool(probe.running)},
                                            {"devices", JsonValue::Number(static_cast<double>(probe.devices))}});
        if (!probe.reason.empty())
            item.Set("reason", JsonValue::String(probe.reason));
        // The devices it reported, with what each cannot do here (a permission, a udev rule)
        std::vector<JsonValue> found;
        for (const HostDeviceInfo& device : devices)
        {
            if (device.backend != probe.name)
                continue;
            JsonValue entry = JsonValue::Object({{"id", JsonValue::String(device.id)},
                                                 {"name", JsonValue::String(device.name)},
                                                 {"family", JsonValue::String(device.family)}});
            const DeviceMatch match = input->LookupDevice(device);
            entry.Set("match", JsonValue::Object({{"step", JsonValue::String(match.step)},
                                                  {"score", JsonValue::Number(match.score)}}));
            const std::string notice = input->DeviceNotice(device.id);
            if (!notice.empty())
                entry.Set("notice", JsonValue::String(notice));
            found.push_back(std::move(entry));
        }
        item.Set("found", JsonValue::Array(std::move(found)));
        backends.push_back(std::move(item));
    }
    HostReply reply;
    reply.body.Set("backends", JsonValue::Array(std::move(backends)));
    reply.body.Set("user_devices", JsonValue::String(UserDevicesPath()));
    return reply;
}

std::string HostInputControl::UserDevicesPath() const
{
    HostInputService* input = _owner.Input();
    const std::string loaded = input != nullptr ? input->UserDevicesPath() : std::string();
    if (!loaded.empty())
        return loaded;
    return (std::filesystem::path(_owner.UserProfileDir()).parent_path() / "devices.user.json").string();
}

HostReply HostInputControl::Database(const HostRequest& request)
{
    const std::string vidpid = Option(request, "vidpid");
    const std::string guid = Option(request, "guid");
    const std::string id = Option(request, "device");
    if ((vidpid.empty() ? 0 : 1) + (guid.empty() ? 0 : 1) + (id.empty() ? 0 : 1) != 1)
        return Fail("bad-request", "database takes one of vidpid= (045e:0b00), guid= (an SDL GUID) or device= (a "
                                   "connected device), with name= and version= to narrow it");
    HostInputService* input = _owner.Input();
    const DeviceDatabase database = input != nullptr ? input->Database() : DeviceDatabase::Builtin();
    DeviceIdentity identity;
    if (!vidpid.empty() && !ParseVidPid(vidpid, identity.vendor, identity.product))
        return Fail("bad-request", "vidpid is <vid>:<pid> in hex (045e:0b00), not '" + vidpid + "'");
    if (!guid.empty())
    {
        std::string lower = guid;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (!IdentityFromSdlGuid(lower, identity))
            return Fail("bad-request", "guid is the 32 hex digits of an SDL GUID with a VID and a PID, not '" + guid + "'");
    }
    if (!id.empty())
    {
        const std::vector<HostDeviceInfo> devices = _owner.Context().devices;
        const HostDeviceInfo* device = FindDevice(devices, id);
        if (device == nullptr)
            return Fail("unknown-device", "no connected host device '" + id + "' (devices lists them)");
        IdentityFromDeviceId(device->id, identity);
        identity.name = device->name;
        identity.backend = device->backend;
    }
    if (const std::string name = Option(request, "name"); !name.empty())
        identity.name = name;
    if (const std::string version = Option(request, "version"); !version.empty())
    {
        uint16_t parsed = 0;
        uint16_t unused = 0;
        if (!ParseVidPid(version + ":0", parsed, unused))
            return Fail("bad-request", "version is 4 hex digits (0517), not '" + version + "'");
        identity.version = parsed;
    }
    const DeviceMatch match = database.Lookup(identity);
    HostReply reply;
    reply.message = match.step == "unknown" ? "no family for " + Hex4(identity.vendor) + ":" + Hex4(identity.product) +
                                                  ": raw controls; learn maps them"
                                            : Hex4(identity.vendor) + ":" + Hex4(identity.product) + " is " +
                                                  match.name + " (" + match.family + ", " + match.step + ", score " +
                                                  std::to_string(match.score) + ")";
    reply.body.Set("query", IdentityValue(identity));
    reply.body.Set("match", MatchValue(match));
    return reply;
}

HostReply HostInputControl::SaveUserFamily(const DeviceIdentity& identity, const std::string& name,
                                           const std::map<std::string, std::string>& map, const std::string& source,
                                           HostReply& reply)
{
    const std::string path = UserDevicesPath();
    std::string text;
    if (std::ifstream in(path, std::ios::binary); in)
    {
        std::ostringstream content;
        content << in.rdbuf();
        text = content.str();
    }
    std::string merged;
    std::string error;
    if (!MergeUserFamily(text, UserFamilyJson(identity, name, map, source), merged, error))
        return Fail("bad-request", path + ": " + error);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << merged;
        if (!out)
            return Fail("unavailable", "cannot write " + path);
    }
    if (HostInputService* input = _owner.Input(); input != nullptr && !input->LoadUserDevices(path, error))
        return Fail("bad-request", error);
    reply.body.Set("family", JsonValue::String(UserFamilyId(identity)));
    reply.body.Set("path", JsonValue::String(path));
    return reply;
}

HostReply HostInputControl::DatabaseImport(const HostRequest& request)
{
    ImportedMapping imported;
    std::string error;
    if (!ImportMapping(Option(request, "text"), imported, error))
        return Fail("bad-request", error);
    std::vector<MappedControl> checked;
    if (!ParseControlMap(imported.map, checked, error))
        return Fail("bad-request", error);
    HostReply reply;
    reply.body.Set("source", JsonValue::String(imported.source));
    reply.body.Set("name", JsonValue::String(imported.name));
    reply.body.Set("device", IdentityValue(imported.identity));
    reply.body.Set("map", MapValue(imported.map));
    reply.body.Set("skipped", Strings(imported.skipped));
    reply.body.Set("gamecontrollerdb",
                   JsonValue::String(GameControllerDbLine(imported.identity, imported.name, imported.map, nullptr)));
    const bool save = Flag(Option(request, "save"), true);
    reply.body.Set("saved", JsonValue::Bool(save));
    reply.message = imported.source + ": " + std::to_string(imported.map.size()) + " controls of " +
                    (imported.name.empty() ? Hex4(imported.identity.vendor) + ":" + Hex4(imported.identity.product)
                                           : imported.name) +
                    (imported.skipped.empty() ? std::string()
                                              : ", " + std::to_string(imported.skipped.size()) + " skipped") +
                    (save ? ", saved as " + UserFamilyId(imported.identity) : std::string(", not saved (save=false)"));
    if (!save)
        return reply;
    return SaveUserFamily(imported.identity, imported.name, imported.map, imported.source, reply);
}

namespace
{
JsonValue LearnState(const LearnSession& session)
{
    JsonValue value = JsonValue::Object({{"done", JsonValue::Bool(session.Done())},
                                         {"index", JsonValue::Number(static_cast<double>(session.Index()))},
                                         {"count", JsonValue::Number(static_cast<double>(session.Controls().size()))},
                                         {"control", JsonValue::String(session.Current())},
                                         {"waiting_for_release", JsonValue::Bool(session.WaitingForRelease())}});
    value.Set("prompt", JsonValue::String(session.Done() ? std::string("Done: finish saves the map")
                                                         : LearnSession::Prompt(session.Current())));
    value.Set("controls", Strings(session.Controls()));
    JsonValue map = JsonValue::Object();
    for (const auto& [canonical, raw] : session.Mapping())
        map.Set(canonical, JsonValue::String(raw));
    value.Set("map", map);
    return value;
}
}  // namespace

HostReply HostInputControl::Learn(const HostRequest& request)
{
    const std::string id = Option(request, "device");
    const std::string step = Option(request, "step", "status");
    if (id.empty())
        return Fail("bad-request", "learn needs device= and step= start, status, sample, skip, back, finish or cancel");
    const std::vector<HostDeviceInfo> devices = _owner.Context().devices;
    const HostDeviceInfo* device = FindDevice(devices, id);
    if (device == nullptr)
        return Fail("unknown-device", "no connected host device '" + id + "' (devices lists them)");
    HostInputService* input = _owner.Input();
    if (input == nullptr)
        return Fail("unavailable", "No host input in this process");
    HostReply reply;
    reply.body.Set("device", JsonValue::String(id));
    if (step == "start")
    {
        LearnSession session;
        std::string error;
        if (!LearnSession::Make(Option(request, "controls"), session, error))
            return Fail("bad-request", error);
        const std::string first = LearnSession::Prompt(session.Current());
        input->StartLearn(id, std::move(session));
        reply.message = device->name + ": learning; its controls reach nothing else until finish or cancel. " + first;
    }
    else if (step == "cancel")
    {
        if (!input->EndLearn(id))
            return Fail("bad-request", device->name + " is not learning (step=start begins)");
        reply.message = device->name + ": learning cancelled, nothing saved";
        reply.body.Set("learning", JsonValue::Bool(false));
        return reply;
    }
    else if (step == "finish")
    {
        LearnSession session;
        if (!input->Learn(id, [&session](LearnSession& s) { session = s; }))
            return Fail("bad-request", device->name + " is not learning (step=start begins)");
        if (session.Mapping().empty())
            return Fail("bad-request", device->name + ": nothing learned yet (press the prompted controls, or skip)");
        DeviceIdentity identity;
        const bool hasId = IdentityFromDeviceId(device->id, identity);
        const bool save = Flag(Option(request, "save"), true);
        std::vector<MappedControl> map;
        std::string error;
        if (!ParseControlMap(session.Mapping(), map, error))
            return Fail("bad-request", error);
        input->EndLearn(id);
        const std::string name = Option(request, "name", device->name);
        reply.body.Set("learning", JsonValue::Bool(false));
        reply.body.Set("map", MapValue(session.Mapping()));
        std::vector<std::string> skipped;
        reply.body.Set("gamecontrollerdb", JsonValue::String(hasId ? GameControllerDbLine(identity, name,
                                                                                          session.Mapping(), &skipped)
                                                                   : std::string()));
        reply.body.Set("not_in_gamecontrollerdb", Strings(skipped));
        if (!save || !hasId)
        {
            // Without a VID / PID nothing in the database can match it again: the map holds for this connection
            input->SetControlMap(id, std::move(map));
            reply.body.Set("saved", JsonValue::Bool(false));
            reply.message = device->name + ": " + std::to_string(session.Mapping().size()) + " controls mapped" +
                            (hasId ? " (not saved: save=false)" : " for this connection (its id has no VID / PID to save it by)");
            return reply;
        }
        reply.body.Set("saved", JsonValue::Bool(true));
        reply.message = device->name + ": " + std::to_string(session.Mapping().size()) + " controls mapped, saved as " +
                        UserFamilyId(identity);
        return SaveUserFamily(identity, name, session.Mapping(), "learn", reply);
    }
    else if (step == "status" || step == "sample" || step == "skip" || step == "back")
    {
        bool answered = false;
        std::string error;
        float value = 1.0f;
        const std::string control = Option(request, "control");
        if (step == "sample")
        {
            // A scripted press: what the device's own event does (a person presses the control instead)
            ControlInfo info;
            char* end = nullptr;
            const std::string text = Option(request, "value", "1");
            value = std::strtof(text.c_str(), &end);
            if (control.empty() || !ParseControl(control, info) || text.empty() || *end != '\0')
                return Fail("bad-request", "sample needs control= (a raw control: button.3, axis.1) and value= (a "
                                           "number, 1 by default)");
        }
        const bool learning = input->Learn(id, [&](LearnSession& session) {
            if (step == "sample")
                answered = session.Feed(control, value);
            else if (step == "skip")
                session.Skip();
            else if (step == "back")
                session.Back();
        });
        if (!learning)
            return Fail("bad-request", device->name + " is not learning (step=start begins)");
        if (step == "sample")
            reply.body.Set("answered", JsonValue::Bool(answered));
    }
    else
        return Fail("bad-request", "step is start, status, sample, skip, back, finish or cancel, not '" + step + "'");
    input->Learn(id, [&reply](LearnSession& session) {
        reply.body.Set("learning", JsonValue::Bool(true));
        const JsonValue state = LearnState(session);  // named: a range over a temporary's members would dangle
        for (const auto& [field, member] : state.GetMembers())
            reply.body.Set(field, member);
        if (reply.message.empty())
            reply.message = session.Done() ? "all " + std::to_string(session.Controls().size()) +
                                                 " controls asked: step=finish saves the map"
                                           : std::to_string(session.Index() + 1) + "/" +
                                                 std::to_string(session.Controls().size()) + ": " +
                                                 LearnSession::Prompt(session.Current()) +
                                                 (session.WaitingForRelease() ? " (release the last one first)" : "");
    });
    return reply;
}

HostReply HostInputControl::Context()
{
    const ProfileContext context = _owner.Context();
    const ResolvedProfiles& resolved = _owner.Resolved();
    HostReply reply;
    reply.body.Set("machine", JsonValue::String(context.machine));
    reply.body.Set("file_sha256", Strings(context.fileSha256));
    reply.body.Set("manual", JsonValue::String(_owner.ManualProfile()));
    reply.body.Set("applied", Strings(resolved.applied));
    reply.body.Set("warnings", Strings(resolved.warnings));
    reply.body.Set("debug_layer", JsonValue::Bool(context.debugLayer));
    reply.body.Set("user_dir", JsonValue::String(_owner.UserProfileDir()));
    return reply;
}

HostReply HostInputControl::List(const HostRequest& request)
{
    const std::string set = Option(request, "set", "all");
    if (set != "all" && set != "play" && set != "debug" && set != "scheme" && set != "game")
        return Fail("bad-request", "set is all, play, debug, scheme or game, not '" + set + "'");
    const std::string machine = Option(request, "machine");
    ProfileStore& store = _owner.Profiles();
    const ResolvedProfiles& resolved = _owner.Resolved();
    std::vector<JsonValue> profiles;
    for (const Profile* profile : store.Profiles())
        if (InSet(*profile, set) && MachineFits(profile->machines, machine))
            profiles.push_back(Summary(*profile, resolved));
    // The shipped games not parsed yet: what the index says of them
    if (set == "all" || set == "game")
        for (const GameEntry& entry : store.Games().Entries())
            if (store.Find(entry.id) == nullptr && MachineFits(entry.machines, machine))
                profiles.push_back(JsonValue::Object({{"id", JsonValue::String(entry.id)},
                                                      {"label", JsonValue::String(entry.label)},
                                                      {"kind", JsonValue::String("play")},
                                                      {"scope", JsonValue::String("builtin")},
                                                      {"specificity", JsonValue::String("game")},
                                                      {"game", JsonValue::Bool(true)},
                                                      {"machines", Strings(entry.machines)},
                                                      {"path", JsonValue::String(entry.file)},
                                                      {"loaded", JsonValue::Bool(false)},
                                                      {"applied", JsonValue::Bool(false)}}));
    HostReply reply;
    reply.body.Set("profiles", JsonValue::Array(std::move(profiles)));
    reply.body.Set("manual", JsonValue::String(_owner.ManualProfile()));
    return reply;
}

const Profile* HostInputControl::FindProfile(const std::string& id)
{
    ProfileStore& store = _owner.Profiles();
    if (const Profile* profile = store.Find(id))
        return profile;
    ProfileContext context;
    context.manual = {id};
    std::vector<std::string> errors;
    store.LoadGamesFor(context, errors);
    return store.Find(id);
}

HostReply HostInputControl::Get(const HostRequest& request)
{
    const std::string id = Option(request, "id");
    const Profile* profile = FindProfile(id);
    if (profile == nullptr)
        return Fail("unknown-profile", "no profile '" + id + "' (profiles lists them)");
    HostReply reply;
    reply.body.Set("info", Summary(*profile, _owner.Resolved()));
    reply.body.Set("profile", ProfileStore::ToJson(*profile));
    reply.body.Set("yaml", JsonValue::String(ProfileStore::Dump(*profile)));
    return reply;
}

HostReply HostInputControl::Save(const HostRequest& request)
{
    const std::string text = Option(request, "text");
    if (text.empty())
        return Fail("bad-request", "save takes the profile as 'text' (YAML or JSON, schema 1)");
    Profile profile;
    std::string error;
    if (!ParseProfile(text, profile, error))
        return Fail("invalid-binding", error);
    profile.scope = ProfileScope::User;
    if (!ProfileStore::Save(profile, _owner.UserProfileDir(), error))
        return Fail("bad-request", error);
    profile.path = ProfileStore::SavePath(profile, _owner.UserProfileDir());
    const std::string id = profile.id;
    const std::string path = profile.path;
    if (!_owner.Profiles().Add(std::move(profile), error))
        return Fail("bad-request", error);
    _owner.Reapply();
    HostReply reply;
    reply.message = "saved " + id + " to " + path;
    reply.body.Set("id", JsonValue::String(id));
    reply.body.Set("path", JsonValue::String(path));
    return reply;
}

HostReply HostInputControl::Delete(const HostRequest& request)
{
    const std::string id = Option(request, "id");
    ProfileStore& store = _owner.Profiles();
    const Profile* profile = store.Find(id);
    if (profile == nullptr)
        return Fail("unknown-profile", "no profile '" + id + "'");
    if (profile->scope != ProfileScope::User)
        return Fail("read-only", "'" + id + "' is a " + ScopeName(profile->scope) +
                                     " profile: only the user's own are deleted (a user profile of the same id "
                                     "overrides it)");
    std::error_code ec;
    if (!profile->path.empty())
        std::filesystem::remove(profile->path, ec);
    if (ec)
        return Fail("bad-request", profile->path + ": " + ec.message());
    store.Remove(id);
    // A built-in of the same id comes back; a shipped game comes back from the index on its next match
    for (const BuiltinProfileText& builtin : BuiltinProfileTexts())
    {
        Profile shipped;
        std::string error;
        if (ParseProfile(builtin.text, shipped, error) && shipped.id == id)
            store.Add(std::move(shipped), error);
    }
    if (_owner.ManualProfile() == id && store.Find(id) == nullptr && store.Games().FindId(id) == nullptr)
        _owner.SetManualProfile({});
    _owner.Reapply();
    HostReply reply;
    reply.message = "deleted " + id;
    reply.body.Set("id", JsonValue::String(id));
    return reply;
}

HostReply HostInputControl::Apply(const HostRequest& request)
{
    std::string id = Option(request, "id");
    if (id == "none")
        id.clear();
    if (!id.empty())
    {
        const Profile* profile = FindProfile(id);
        if (profile == nullptr)
            return Fail("unknown-profile", "no profile '" + id + "' (profiles lists them)");
        if (profile->kind != ProfileKind::Play || profile->scheme)
            return Fail("bad-request", "'" + id + "' is a " + KindName(*profile) +
                                           " profile: apply chooses a play profile as the game's");
    }
    _owner.SetManualProfile(id);
    HostReply reply = Context();
    if (id.empty())
    {
        reply.message = "the game profile follows the loaded file";
        return reply;
    }
    const JsonValue* applied = reply.body.Find("applied");
    const bool active = applied != nullptr && std::any_of(applied->Items().begin(), applied->Items().end(),
                                                          [&id](const JsonValue& item) { return item.AsString() == id; });
    if (active)
        reply.message = "applied " + id + " as the game profile";
    else
    {
        // `machines` narrows a profile: the choice is kept and applies once such a machine runs
        const Profile* profile = FindProfile(id);
        std::string machines;
        if (profile != nullptr)
            for (const std::string& machine : profile->machines)
                machines += (machines.empty() ? "" : ", ") + machine;
        const std::string machine = _owner.Context().machine;
        reply.message = "chose " + id + " as the game profile; it applies on " + machines + " (now: " +
                        (machine.empty() ? std::string("no machine") : machine) + ")";
    }
    return reply;
}

HostReply HostInputControl::Bindings(const HostRequest& request)
{
    const std::string device = Option(request, "device");
    const std::string layer = Option(request, "layer");
    const ResolvedProfiles& resolved = _owner.Resolved();
    // A device's scope, else its family's, else every device's
    std::string scope;
    if (!device.empty())
    {
        const ProfileContext context = _owner.Context();
        const auto it = std::find_if(context.devices.begin(), context.devices.end(),
                                     [&device](const HostDeviceInfo& info) { return info.id == device; });
        if (it == context.devices.end())
            return Fail("unknown-device", "no connected device '" + device + "' (devices lists them)");
        for (const std::string& candidate : {it->id, "family:" + it->family})
            if (scope.empty() && std::any_of(resolved.effective.begin(), resolved.effective.end(),
                                             [&candidate](const auto& entry) { return entry.first == candidate; }))
                scope = candidate;
    }
    std::vector<JsonValue> bindings;
    for (const auto& [name, effective] : resolved.effective)
    {
        if (name != scope)
            continue;
        for (const EffectiveBinding& binding : effective)
            if (layer.empty() || binding.layer == layer)
                bindings.push_back(JsonValue::Object({{"layer", JsonValue::String(binding.layer)},
                                                      {"control", JsonValue::String(binding.control)},
                                                      {"spec", binding.spec},
                                                      {"profile", JsonValue::String(binding.profile)}}));
    }
    HostReply reply;
    reply.body.Set("scope", JsonValue::String(scope.empty() ? "every device" : scope));
    reply.body.Set("bindings", JsonValue::Array(std::move(bindings)));
    return reply;
}

std::string HostInputControl::UserProfileFor(const std::string& device, const std::string& family)
{
    return !device.empty() ? "user/device-" + Sanitized(device) : !family.empty() ? "user/" + family : "user/global";
}

bool HostInputControl::EditProfile(const HostRequest& request, bool mustExist, Profile& profile, std::string& id,
                                   HostReply& failure)
{
    const std::string family = Option(request, "family");
    const std::string device = Option(request, "device");
    // The user profile it goes in: named, else the device's, the family's, or every device's
    id = Option(request, "profile");
    if (id.empty())
        id = UserProfileFor(device, family);
    if (const Profile* existing = FindProfile(id))
        profile = *existing;  // a shipped one is copied: the user's of the same id overrides it
    else if (mustExist)
    {
        failure = Fail("unknown-profile", "no profile '" + id + "' to change");
        return false;
    }
    else
    {
        profile = Profile{};
        profile.id = id;
        profile.label = !device.empty() ? device : !family.empty() ? family : "Every device";
        if (!device.empty())
            profile.devices = {device};
        else if (!family.empty())
            profile.families = {family};
        profile.options = JsonValue::Object();
    }
    profile.scope = ProfileScope::User;
    return true;
}

HostReply HostInputControl::Store(const Profile& profile, const std::string& message)
{
    // Through the reader the files go through: a binding that does not compile is refused here
    Profile checked;
    std::string error;
    if (!ParseProfile(ProfileStore::Dump(profile), checked, error))
        return Fail("invalid-binding", error);
    checked.scope = ProfileScope::User;
    if (!ProfileStore::Save(checked, _owner.UserProfileDir(), error))
        return Fail("bad-request", error);
    checked.path = ProfileStore::SavePath(checked, _owner.UserProfileDir());
    const std::string id = checked.id;
    const std::string path = checked.path;
    if (!_owner.Profiles().Add(std::move(checked), error))
        return Fail("bad-request", error);
    _owner.Reapply();
    HostReply reply;
    reply.message = message;
    reply.body.Set("profile", JsonValue::String(id));
    reply.body.Set("path", JsonValue::String(path));
    return reply;
}

HostReply HostInputControl::Bind(const HostRequest& request, bool unbind)
{
    const std::string control = Option(request, "control");
    const std::string layer = Option(request, "layer", "play");
    if (control.empty())
        return Fail("bad-request", "name the 'control' (south, lt.click, view+menu, dpad.*)");
    JsonValue spec;
    if (!unbind)
    {
        std::string error;
        if (!ParseYaml(Option(request, "spec"), spec, error) || !spec.IsObject())
            return Fail("invalid-binding", "spec is a binding object ({key: ENTER}, {retro: \"auto:fire\"}, "
                                           "{none: true})" + (error.empty() ? std::string() : ": " + error));
    }
    Profile profile;
    std::string id;
    HostReply failure;
    if (!EditProfile(request, unbind, profile, id, failure))
        return failure;
    if (profile.kind != ProfileKind::Play && layer != "debug")
        return Fail("bad-request", "'" + id + "' is a debug profile: it binds the debug layer only");

    auto entry = std::find_if(profile.layers.begin(), profile.layers.end(),
                              [&layer](const auto& item) { return item.first == layer; });
    if (entry == profile.layers.end())
    {
        if (unbind)
            return Fail("bad-request", "'" + id + "' binds nothing in the " + layer + " layer");
        profile.layers.emplace_back(layer, JsonValue::Members{});
        entry = profile.layers.end() - 1;
    }
    auto member = std::find_if(entry->second.begin(), entry->second.end(),
                               [&control](const auto& item) { return item.first == control; });
    if (unbind)
    {
        if (member == entry->second.end())
            return Fail("bad-request", "'" + id + "' does not bind " + control + " in the " + layer + " layer");
        entry->second.erase(member);
    }
    else if (member == entry->second.end())
        entry->second.emplace_back(control, spec);
    else
        member->second = spec;
    return Store(profile, std::string(unbind ? "unbound " : "bound ") + layer + "." + control + " in " + id);
}

HostReply HostInputControl::Scheme(const HostRequest& request)
{
    std::string scheme = Option(request, "id");
    if (scheme == "none")
        scheme.clear();
    if (!scheme.empty())
    {
        const Profile* found = FindProfile(scheme);
        if (found == nullptr)
            return Fail("unknown-profile", "no scheme '" + scheme + "' (profiles set=scheme lists them)");
        if (!found->scheme)
            return Fail("bad-request", "'" + scheme + "' is not a scheme (kind: scheme)");
    }
    Profile profile;
    std::string id;
    HostReply failure;
    if (!EditProfile(request, false, profile, id, failure))
        return failure;
    if (profile.scheme)
        return Fail("bad-request", "'" + id + "' is a scheme: a scheme does not extend another one");
    profile.extends = scheme.empty() ? std::vector<std::string>{} : std::vector<std::string>{scheme};
    return Store(profile, scheme.empty() ? id + " uses no scheme" : id + " works as " + scheme);
}

}  // namespace controllers
