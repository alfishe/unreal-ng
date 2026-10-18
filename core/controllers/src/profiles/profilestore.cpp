#include "profiles/profilestore.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "router/inputrouter.h"

namespace controllers
{

namespace
{
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool Contains(const std::vector<std::string>& list, const std::string& value)
{
    const std::string lower = Lower(value);
    return std::any_of(list.begin(), list.end(), [&lower](const std::string& item) { return Lower(item) == lower; });
}

/// A profile family names the device's family or a part of it (`xbox-elite` applies to `xbox-elite-2`), or its layout
/// (`playstation` applies to `dualsense`)
bool FamilyMatches(const std::vector<std::string>& families, const std::string& family, const std::string& layout = {})
{
    if (!layout.empty() && Contains(families, layout))
        return true;
    const std::string lower = Lower(family);
    return std::any_of(families.begin(), families.end(), [&lower](const std::string& item) {
        const std::string name = Lower(item);
        return lower == name || (lower.size() > name.size() && lower.compare(0, name.size(), name) == 0 &&
                                 lower[name.size()] == '-');
    });
}

bool DeviceMatches(const std::vector<std::string>& devices, const HostDeviceId& device)
{
    return std::any_of(devices.begin(), devices.end(), [&device](const std::string& item) {
        return !item.empty() && device.find(item) != std::string::npos;
    });
}

/// A scope's target: every device (""), one family, one device of a family
struct ScopeTarget
{
    std::string family;
    HostDeviceId device;
    std::string layout;
};

bool AppliesTo(const Profile& profile, const ScopeTarget& target)
{
    if (!profile.families.empty() && (target.family.empty() || !FamilyMatches(profile.families, target.family, target.layout)))
        return false;
    if (!profile.devices.empty() && (target.device.empty() || !DeviceMatches(profile.devices, target.device)))
        return false;
    return true;
}

bool Selected(const Profile& profile, const ProfileContext& context)
{
    if (profile.kind == ProfileKind::Debug && !context.debugLayer)
        return false;
    if (!profile.machines.empty() && !context.machine.empty() && !Contains(profile.machines, context.machine))
        return false;
    if (!profile.machines.empty() && context.machine.empty())
        return false;
    if (profile.kind == ProfileKind::Play && Contains(context.manual, profile.id))
        return true;
    if (!profile.IsGame())
        return true;
    const auto any = [](const std::vector<std::string>& wanted, const std::vector<std::string>& have) {
        return std::any_of(wanted.begin(), wanted.end(), [&have](const std::string& w) { return Contains(have, w); });
    };
    return any(profile.signatures, context.signatures) || any(profile.fileSha256, context.fileSha256);
}

/// The rank of a selected profile: specificity (a manual choice ranks as a game), then scope
std::pair<int, int> Rank(const Profile& profile, const ProfileContext& context)
{
    Specificity specificity = profile.GetSpecificity();
    if (profile.kind == ProfileKind::Play && Contains(context.manual, profile.id))
        specificity = Specificity::Game;
    return {static_cast<int>(specificity), static_cast<int>(profile.scope)};
}

using Layers = std::vector<std::pair<std::string, JsonValue::Members>>;

JsonValue::Members* FindLayer(Layers& layers, const std::string& name)
{
    for (auto& [layer, members] : layers)
        if (layer == name)
            return &members;
    layers.emplace_back(name, JsonValue::Members{});
    return &layers.back().second;
}

/// mapping-and-profiles.md §6.2: field by field; a target in the later binding replaces every target field before it
void Merge(Layers& into, const Profile& profile)
{
    for (const auto& [layer, bindings] : profile.layers)
    {
        JsonValue::Members& members = *FindLayer(into, layer);
        for (const auto& [control, spec] : bindings)
        {
            JsonValue later = spec;
            for (const auto& [field, value] : profile.options.GetMembers())
                if (later.Find(field) == nullptr)
                    later.Set(field, value);

            auto it = std::find_if(members.begin(), members.end(),
                                   [&control](const auto& member) { return member.first == control; });
            if (it == members.end())
            {
                members.emplace_back(control, later);
                continue;
            }
            const bool retargets = std::any_of(later.GetMembers().begin(), later.GetMembers().end(),
                                               [](const auto& member) { return IsTargetField(member.first); });
            if (retargets)
            {
                std::vector<std::string> drop;
                for (const auto& [field, value] : it->second.GetMembers())
                    if (IsTargetField(field))
                        drop.push_back(field);
                for (const std::string& field : drop)
                    it->second.Remove(field);
            }
            for (const auto& [field, value] : later.GetMembers())
                it->second.Set(field, value);
        }
    }
}

std::vector<Binding> Compile(const Layers& layers, const std::string& scope, std::vector<std::string>& warnings)
{
    std::vector<Binding> bindings;
    const JsonValue none = JsonValue::Object();
    for (const auto& [layer, members] : layers)
    {
        for (const auto& [control, spec] : members)
        {
            Binding binding;
            std::string error;
            if (CompileBinding(control, spec, none, layer, binding, error))
                bindings.push_back(std::move(binding));
            else
                warnings.push_back((scope.empty() ? std::string("every device") : scope) + ", " + layer + "." +
                                   control + ": " + error);
        }
    }
    return bindings;
}

bool IsPad(const HostDeviceInfo& device)
{
    return device.family != "keyboard" && device.family != "mouse";
}

void DumpStrings(JsonValue& object, const char* field, const std::vector<std::string>& values)
{
    if (values.empty())
        return;
    std::vector<JsonValue> items;
    for (const std::string& value : values)
        items.push_back(JsonValue::String(value));
    object.Set(field, JsonValue::Array(std::move(items)));
}
}  // namespace

ProfileStore::ProfileStore()
{
    for (const char* text : BuiltinProfileTexts())
    {
        std::string error;
        AddText(text, ProfileScope::Builtin, error);  // the built-in texts are checked by a test
    }
}

bool ProfileStore::Add(Profile profile, std::string& error)
{
    auto it = std::find_if(_profiles.begin(), _profiles.end(),
                           [&profile](const Profile& kept) { return kept.id == profile.id; });
    if (it == _profiles.end())
    {
        _profiles.push_back(std::move(profile));
        return true;
    }
    if (it->scope > profile.scope)
    {
        error = "'" + profile.id + "' is already a profile of a higher scope";
        return false;
    }
    *it = std::move(profile);
    return true;
}

bool ProfileStore::Remove(const std::string& id)
{
    auto it = std::find_if(_profiles.begin(), _profiles.end(), [&id](const Profile& kept) { return kept.id == id; });
    if (it == _profiles.end())
        return false;
    _profiles.erase(it);
    return true;
}

bool ProfileStore::AddText(const std::string& text, ProfileScope scope, std::string& error)
{
    Profile profile;
    if (!ParseProfile(text, profile, error))
        return false;
    profile.scope = scope;
    return Add(std::move(profile), error);
}

size_t ProfileStore::LoadDirectory(const std::string& root, ProfileScope scope, std::vector<std::string>& errors)
{
    namespace fs = std::filesystem;
    size_t kept = 0;
    for (const char* sub : {"play", "debug"})
    {
        std::error_code ec;
        const fs::path dir = fs::path(root) / sub;
        if (!fs::is_directory(dir, ec))
            continue;
        std::vector<fs::path> files;
        for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec))
            if (entry.is_regular_file(ec) && entry.path().extension() == ".json")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files)
        {
            std::ifstream in(file, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            Profile profile;
            std::string error;
            if (!in || !ParseProfile(text.str(), profile, error))
            {
                errors.push_back(file.string() + ": " + (error.empty() ? "cannot be read" : error));
                continue;
            }
            const char* kind = profile.kind == ProfileKind::Play ? "play" : "debug";
            if (std::string(kind) != sub)
            {
                errors.push_back(file.string() + ": a " + kind + " profile in " + sub + "/");
                continue;
            }
            profile.scope = scope;
            profile.path = file.string();
            if (!Add(std::move(profile), error))
            {
                errors.push_back(file.string() + ": " + error);
                continue;
            }
            ++kept;
        }
    }
    return kept;
}

const Profile* ProfileStore::Find(const std::string& id) const
{
    for (const Profile& profile : _profiles)
        if (profile.id == id)
            return &profile;
    return nullptr;
}

std::vector<const Profile*> ProfileStore::Profiles() const
{
    std::vector<const Profile*> out;
    for (const Profile& profile : _profiles)
        out.push_back(&profile);
    return out;
}

ResolvedProfiles ProfileStore::Resolve(const ProfileContext& context) const
{
    ResolvedProfiles resolved;
    std::vector<const Profile*> selected;
    for (const Profile& profile : _profiles)
        if (Selected(profile, context))
            selected.push_back(&profile);
    std::stable_sort(selected.begin(), selected.end(), [&context](const Profile* a, const Profile* b) {
        return Rank(*a, context) < Rank(*b, context);
    });

    const auto mergeFor = [&](const ScopeTarget& target, const std::string& scope, bool top) {
        Layers layers;
        for (const Profile* profile : selected)
        {
            if (!AppliesTo(*profile, target))
                continue;
            Merge(layers, *profile);
            if (!top)
                continue;
            resolved.applied.push_back(profile->id);
            for (const auto& [connector, plan] : profile->connectors)
                resolved.connectors[connector] = plan;
            if (!profile->players.empty())
                resolved.players = profile->players;
            if (!profile->haptics.IsNull())
            {
                resolved.haptics = profile->haptics;
                resolved.hapticsProfile = profile->id;
            }
        }
        resolved.scopes.emplace_back(scope, Compile(layers, scope, resolved.warnings));
    };

    mergeFor({}, "", true);
    // A family or device scope only where a profile names it: elsewhere the "" scope is the whole answer
    std::set<std::string> families;
    for (const HostDeviceInfo& device : context.devices)
    {
        const bool familyNamed = std::any_of(selected.begin(), selected.end(), [&device](const Profile* p) {
            return p->devices.empty() && !p->families.empty() &&
                   FamilyMatches(p->families, device.family, device.layout);
        });
        if (familyNamed && families.insert(device.family).second)
            mergeFor({device.family, {}, device.layout}, "family:" + device.family, false);
    }
    for (const HostDeviceInfo& device : context.devices)
    {
        const bool deviceNamed = std::any_of(selected.begin(), selected.end(), [&device](const Profile* p) {
            return DeviceMatches(p->devices, device.id) && AppliesTo(*p, {device.family, device.id, device.layout});
        });
        if (deviceNamed)
            mergeFor({device.family, device.id, device.layout}, device.id, false);
    }
    return resolved;
}

ResolvedProfiles ProfileStore::Apply(InputRouter& router, const ProfileContext& context)
{
    ResolvedProfiles resolved = Resolve(context);
    std::vector<std::string> scopes;
    for (const auto& [scope, bindings] : resolved.scopes)
    {
        std::string error;
        if (router.SetBindings(scope, bindings, error))
            scopes.push_back(scope);
        else
            resolved.warnings.push_back((scope.empty() ? std::string("every device") : scope) + ": " + error);
    }
    for (const std::string& scope : _appliedScopes)
        if (std::find(scopes.begin(), scopes.end(), scope) == scopes.end())
            router.ClearBindings(scope);
    _appliedScopes = std::move(scopes);

    // players: `auto:N` is the N-th pad connected (keyboards and mice do not count), else an id substring
    std::vector<const HostDeviceInfo*> pads;
    for (const HostDeviceInfo& device : context.devices)
        if (IsPad(device))
            pads.push_back(&device);
    for (const PlayerPin& pin : resolved.players)
    {
        if (pin.device.rfind("auto:", 0) == 0)
        {
            const unsigned n = static_cast<unsigned>(std::strtoul(pin.device.c_str() + 5, nullptr, 10));
            if (n >= 1 && n <= pads.size())
                router.SetPlayerConnector(pads[n - 1]->id, pin.connector);
            continue;
        }
        for (const HostDeviceInfo& device : context.devices)
            if (DeviceMatches({pin.device}, device.id))
                router.SetPlayerConnector(device.id, pin.connector);
    }
    return resolved;
}

std::string ProfileStore::Dump(const Profile& profile)
{
    JsonValue root = JsonValue::Object();
    root.Set("schema", JsonValue::Number(1));
    root.Set("kind", JsonValue::String(profile.kind == ProfileKind::Play ? "play" : "debug"));
    root.Set("id", JsonValue::String(profile.id));
    if (!profile.label.empty())
        root.Set("label", JsonValue::String(profile.label));

    JsonValue match = JsonValue::Object();
    DumpStrings(match, "machines", profile.machines);
    DumpStrings(match, "signatures", profile.signatures);
    DumpStrings(match, "file_sha256", profile.fileSha256);
    if (!profile.title.empty())
        match.Set("title", JsonValue::String(profile.title));
    if (!match.GetMembers().empty())
        root.Set("match", match);

    JsonValue applies = JsonValue::Object();
    DumpStrings(applies, "families", profile.families);
    DumpStrings(applies, "devices", profile.devices);
    if (!applies.GetMembers().empty())
        root.Set("applies_to", applies);

    if (!profile.connectors.empty())
    {
        JsonValue connectors = JsonValue::Object();
        for (const auto& [connector, plan] : profile.connectors)
        {
            JsonValue entry = JsonValue::Object({{"device", JsonValue::String(plan.device)}});
            for (const auto& [field, value] : plan.options.GetMembers())
                entry.Set(field, value);
            connectors.Set(connector, entry);
        }
        root.Set("connectors", connectors);
    }
    if (!profile.players.empty())
    {
        std::vector<JsonValue> players;
        for (const PlayerPin& pin : profile.players)
            players.push_back(JsonValue::Object(
                {{"device", JsonValue::String(pin.device)}, {"connector", JsonValue::String(pin.connector)}}));
        root.Set("players", JsonValue::Array(std::move(players)));
    }
    if (!profile.options.GetMembers().empty())
        root.Set("options", profile.options);

    // A debug profile's layer key went into its play layer: it goes back to `layer_key`
    JsonValue layers = JsonValue::Object();
    for (const auto& [layer, bindings] : profile.layers)
    {
        if (profile.kind == ProfileKind::Debug && layer == "play")
        {
            for (const auto& [control, spec] : bindings)
            {
                JsonValue key = JsonValue::Object({{"control", JsonValue::String(control)}});
                if (const JsonValue* mode = spec.Find("mode"))
                    key.Set("mode", *mode);
                root.Set("layer_key", key);
            }
            continue;
        }
        layers.Set(layer, JsonValue::Object(bindings));
    }
    root.Set("layers", layers);
    if (!profile.haptics.IsNull())
        root.Set("haptics", profile.haptics);
    return root.Dump(2) + "\n";
}

bool ProfileStore::Save(const Profile& profile, const std::string& root, std::string& error)
{
    namespace fs = std::filesystem;
    std::string name = profile.id.substr(profile.id.find_last_of('/') + 1);
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
            c = '_';
    if (name.empty() || name[0] == '.')
    {
        error = "'" + profile.id + "' does not name a file";
        return false;
    }
    const fs::path dir = fs::path(root) / (profile.kind == ProfileKind::Play ? "play" : "debug");
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
    {
        error = dir.string() + ": " + ec.message();
        return false;
    }
    const fs::path file = dir / (name + ".json");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << Dump(profile);
    if (!out)
    {
        error = file.string() + ": cannot be written";
        return false;
    }
    return true;
}

}  // namespace controllers
