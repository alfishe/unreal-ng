#include "profiles/profilestore.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "devicedatabase.h"
#include "profiles/yaml.h"
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

/// An item of a profile's `devices` (or a pin's device) is a substring of the id or of one of its old ids (aliases)
bool DeviceMatches(const std::vector<std::string>& devices, const HostDeviceId& device,
                   const std::vector<HostDeviceId>& aliases = {})
{
    return std::any_of(devices.begin(), devices.end(), [&device, &aliases](const std::string& item) {
        return !item.empty() &&
               (device.find(item) != std::string::npos ||
                std::any_of(aliases.begin(), aliases.end(),
                            [&item](const HostDeviceId& alias) { return alias.find(item) != std::string::npos; }));
    });
}

/// A scope's target: every device (""), one family, one device of a family
struct ScopeTarget
{
    std::string family;
    HostDeviceId device;
    std::string layout;
    std::vector<HostDeviceId> aliases;  ///< the device's old ids
};

bool AppliesTo(const Profile& profile, const ScopeTarget& target)
{
    if (!profile.families.empty() && (target.family.empty() || !FamilyMatches(profile.families, target.family, target.layout)))
        return false;
    if (!profile.devices.empty() &&
        (target.device.empty() || !DeviceMatches(profile.devices, target.device, target.aliases)))
        return false;
    return true;
}

/// A debug profile of the keyboard or of families that start as debugger remotes (the Surface Dial, `"role":
/// "debugger-remote"`, control surfaces Q-8) alone: it works without the debug layer (debug-and-ttd.md §4.3, §4.4)
bool RemoteByNature(const Profile& profile)
{
    return profile.devices.empty() && !profile.families.empty() &&
           std::all_of(profile.families.begin(), profile.families.end(), [](const std::string& family) {
               return family == "keyboard" ||
                      DeviceDatabase::Builtin().ForFamily(family).role == DeviceRole::DebuggerRemote;
           });
}

bool Selected(const Profile& profile, const ProfileContext& context)
{
    if (profile.scheme)
        return false;
    if (profile.kind == ProfileKind::Debug && !context.debugLayer && !RemoteByNature(profile))
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

/// layer + '\n' + control -> the profile that changed it last
using Sources = std::map<std::string, std::string>;

/// mapping-and-profiles.md §6.2: field by field; a target in the later binding replaces every target field before it
void Merge(Layers& into, const Profile& profile, Sources& sources)
{
    for (const auto& [layer, bindings] : profile.layers)
    {
        JsonValue::Members& members = *FindLayer(into, layer);
        for (const auto& [control, spec] : bindings)
        {
            sources[layer + "\n" + control] = profile.id;
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

const char* Directory(const Profile& profile)
{
    if (profile.kind == ProfileKind::Debug)
        return "debug";
    if (profile.scheme)
        return "schemes";
    return profile.IsGame() ? "games" : "play";
}

/// What a directory of LoadDirectory holds; `play/` also takes the game profiles older versions wrote there
bool Belongs(const Profile& profile, const std::string& sub)
{
    if (sub == "play")
        return profile.kind == ProfileKind::Play && !profile.scheme;
    if (sub == "debug")
        return profile.kind == ProfileKind::Debug;
    if (sub == "schemes")
        return profile.scheme;
    return profile.kind == ProfileKind::Play && !profile.scheme && profile.IsGame();
}

std::string FileName(const Profile& profile)
{
    std::string name = profile.id.substr(profile.id.find_last_of('/') + 1);
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
            c = '_';
    return name;
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
    for (const BuiltinProfileText& builtin : BuiltinProfileTexts())
    {
        std::string error;
        AddText(builtin.text, ProfileScope::Builtin, error);  // the built-in texts are checked by a test
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
    for (const std::string sub : {"play", "debug", "schemes", "games"})
    {
        std::error_code ec;
        const fs::path dir = fs::path(root) / sub;
        if (!fs::is_directory(dir, ec))
            continue;
        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        {
            const fs::path extension = it->path().extension();
            if (it->is_regular_file(ec) && (extension == ".yaml" || extension == ".yml" || extension == ".json") &&
                it->path().filename() != "index.yaml")
                files.push_back(it->path());
            if (sub != "games" && it->is_directory(ec))
                it.disable_recursion_pending();
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files)
        {
            std::ifstream in(file, std::ios::binary);
            std::stringstream stream;
            stream << in.rdbuf();
            const std::string text = stream.str();
            if (!in)
            {
                errors.push_back(file.string() + ": cannot be read");
                continue;
            }
            // A file of games/ may be a bundle: one profile per YAML document
            std::vector<std::pair<size_t, size_t>> documents = {{0, text.size()}};
            if (sub == "games" && file.extension() != ".json")
                documents = SplitYamlDocuments(text);
            for (const auto& [offset, length] : documents)
            {
                const std::string where = file.string() + (offset != 0 ? " at byte " + std::to_string(offset) : "");
                Profile profile;
                std::string error;
                if (!ParseProfile(text.substr(offset, length), profile, error))
                {
                    errors.push_back(where + ": " + error);
                    continue;
                }
                if (!Belongs(profile, sub))
                {
                    errors.push_back(where + ": belongs in " + Directory(profile) + "/, not " + sub + "/");
                    continue;
                }
                profile.scope = scope;
                profile.path = file.string();
                if (!Add(std::move(profile), error))
                {
                    errors.push_back(where + ": " + error);
                    continue;
                }
                ++kept;
            }
        }
    }
    return kept;
}

bool ProfileStore::OpenGames(const std::string& gamesDir, std::vector<std::string>& errors)
{
    return _games.Open(gamesDir, errors);
}

size_t ProfileStore::LoadGamesFor(const ProfileContext& context, std::vector<std::string>& errors)
{
    std::vector<std::string> keys;
    for (const std::string& sha : context.fileSha256)
        keys.push_back("sha256:" + sha);
    for (const std::string& signature : context.signatures)
        keys.push_back("sig:" + signature);
    std::vector<std::string> ids;
    for (const GameEntry* entry : _games.Lookup(keys))
        ids.push_back(entry->id);
    for (const std::string& id : context.manual)
        if (_games.FindId(id) != nullptr)
            ids.push_back(id);
    size_t kept = 0;
    for (const std::string& id : ids)
    {
        if (Find(id) != nullptr)
            continue;
        Profile profile;
        std::string error;
        if (!_games.Load(id, profile, error))
        {
            errors.push_back(error);
            continue;
        }
        profile.scope = ProfileScope::Builtin;
        if (Add(std::move(profile), error))
            ++kept;
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

    // §7.6: the schemes a profile extends merge just below it, in order, a scheme's own schemes below that scheme
    std::map<const Profile*, std::vector<const Profile*>> schemes;
    const auto expand = [&](const Profile& profile, std::vector<std::string>& chain, std::vector<const Profile*>& out,
                            const auto& self) -> void {
        for (const std::string& id : profile.extends)
        {
            const Profile* scheme = Find(id);
            if (scheme == nullptr || !scheme->scheme)
            {
                resolved.warnings.push_back(profile.id + " extends '" + id + "', which is not a scheme");
                continue;
            }
            if (std::find(chain.begin(), chain.end(), id) != chain.end())
            {
                resolved.warnings.push_back(profile.id + " extends '" + id + "': a cycle");
                continue;
            }
            chain.push_back(id);
            self(*scheme, chain, out, self);
            chain.pop_back();
            out.push_back(scheme);
        }
    };
    for (const Profile* profile : selected)
    {
        std::vector<std::string> chain = {profile->id};
        expand(*profile, chain, schemes[profile], expand);
    }

    const auto mergeFor = [&](const ScopeTarget& target, const std::string& scope, bool top) {
        Layers layers;
        Sources sources;
        JsonValue surface;
        const auto take = [&](const Profile& profile) {
            Merge(layers, profile, sources);
            if (profile.kind == ProfileKind::Debug && !profile.haptics.IsNull())
                surface = profile.haptics;  // the later (more specific) one wins whole
            if (!top)
                return;
            resolved.applied.push_back(profile.id);
            for (const auto& [connector, plan] : profile.connectors)
                resolved.connectors[connector] = plan;
            if (!profile.players.empty())
                resolved.players = profile.players;
            if (profile.kind == ProfileKind::Play && !profile.haptics.IsNull())
            {
                resolved.haptics = profile.haptics;
                resolved.hapticsProfile = profile.id;
            }
        };
        for (const Profile* profile : selected)
        {
            if (!AppliesTo(*profile, target))
                continue;
            for (const Profile* scheme : schemes[profile])
                take(*scheme);
            take(*profile);
        }
        resolved.scopes.emplace_back(scope, Compile(layers, scope, resolved.warnings));
        if (!surface.IsNull())
            resolved.surfaceHaptics.emplace_back(scope, surface);
        std::vector<EffectiveBinding> effective;
        for (const auto& [layer, members] : layers)
            for (const auto& [control, spec] : members)
                effective.push_back({layer, control, spec, sources[layer + "\n" + control]});
        resolved.effective.emplace_back(scope, std::move(effective));
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
            mergeFor({device.family, {}, device.layout, {}}, "family:" + device.family, false);
    }
    for (const HostDeviceInfo& device : context.devices)
    {
        const ScopeTarget target{device.family, device.id, device.layout, AliasesOf(device.id)};
        const bool deviceNamed = std::any_of(selected.begin(), selected.end(), [&target](const Profile* p) {
            return DeviceMatches(p->devices, target.device, target.aliases) && AppliesTo(*p, target);
        });
        if (deviceNamed)
            mergeFor(target, device.id, false);
    }
    return resolved;
}

ResolvedProfiles ProfileStore::Apply(InputRouter& router, const ProfileContext& context)
{
    std::vector<std::string> gameErrors;
    LoadGamesFor(context, gameErrors);
    ResolvedProfiles resolved = Resolve(context);
    resolved.warnings.insert(resolved.warnings.end(), gameErrors.begin(), gameErrors.end());
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

    // What the surfaces feel per layer (the debug profiles' haptics, checked when they were read)
    std::vector<std::string> hapticScopes;
    for (const auto& [scope, section] : resolved.surfaceHaptics)
    {
        SurfaceHaptics haptics;
        std::string error;
        if (!ParseSurfaceHaptics(section, haptics, error))
        {
            resolved.warnings.push_back((scope.empty() ? std::string("every device") : scope) + ": " + error);
            continue;
        }
        router.SetSurfaceHaptics(scope, std::move(haptics));
        hapticScopes.push_back(scope);
    }
    for (const std::string& scope : _hapticScopes)
        if (std::find(hapticScopes.begin(), hapticScopes.end(), scope) == hapticScopes.end())
            router.ClearSurfaceHaptics(scope);
    _hapticScopes = std::move(hapticScopes);

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
            if (DeviceMatches({pin.device}, device.id, AliasesOf(device.id)))
                router.SetPlayerConnector(device.id, pin.connector);
    }
    return resolved;
}

std::string ProfileStore::Dump(const Profile& profile)
{
    return "# " + profile.id +
           ": written by unreal-ng, which does not keep comments in this file when it writes it again\n" +
           DumpYaml(ToJson(profile));
}

JsonValue ProfileStore::ToJson(const Profile& profile)
{
    JsonValue root = JsonValue::Object();
    root.Set("schema", JsonValue::Number(1));
    const char* kind = profile.kind == ProfileKind::Play ? "play" : "debug";
    root.Set("kind", JsonValue::String(profile.scheme ? "scheme" : kind));
    root.Set("id", JsonValue::String(profile.id));
    if (!profile.label.empty())
        root.Set("label", JsonValue::String(profile.label));
    DumpStrings(root, "extends", profile.extends);

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
    return root;
}

std::string ProfileStore::SavePath(const Profile& profile, const std::string& root)
{
    return (std::filesystem::path(root) / Directory(profile) / (FileName(profile) + ".yaml")).string();
}

/// region <Aliases>

void ProfileStore::AddAlias(const HostDeviceId& from, const HostDeviceId& to)
{
    if (from.empty() || to.empty() || from == to)
        return;
    for (auto& [old, current] : _aliases)
        if (current == from)
            current = to;  // promoted twice: the oldest id follows to the newest
    _aliases.erase(to);    // the new id is a device of its own again, not an alias
    _aliases[from] = to;
}

std::vector<HostDeviceId> ProfileStore::AliasesOf(const HostDeviceId& device) const
{
    std::vector<HostDeviceId> out;
    for (const auto& [old, current] : _aliases)
        if (current == device)
            out.push_back(old);
    return out;
}

std::string ProfileStore::AliasesPath(const std::string& root)
{
    return (std::filesystem::path(root) / "aliases.yaml").string();
}

std::string ProfileStore::DumpAliases(const std::map<HostDeviceId, HostDeviceId>& aliases)
{
    std::vector<JsonValue> items;
    for (const auto& [from, to] : aliases)
        items.push_back(JsonValue::Object({{"from", JsonValue::String(from)}, {"to", JsonValue::String(to)}}));
    JsonValue root = JsonValue::Object({{"schema", JsonValue::Number(1)}, {"aliases", JsonValue::Array(items)}});
    return "# The ids of promoted controllers (old -> new): written by unreal-ng\n" + DumpYaml(root);
}

bool ProfileStore::ParseAliases(const std::string& text, std::map<HostDeviceId, HostDeviceId>& out,
                                std::string& error)
{
    JsonValue root;
    if (!ParseYaml(text, root, error))
        return false;
    const JsonValue* items = root.IsObject() ? root.Find("aliases") : nullptr;
    if (items == nullptr || !items->IsArray())
    {
        error = "no 'aliases' list";
        return false;
    }
    for (const JsonValue& item : items->Items())
    {
        const JsonValue* from = item.IsObject() ? item.Find("from") : nullptr;
        const JsonValue* to = item.IsObject() ? item.Find("to") : nullptr;
        if (from == nullptr || to == nullptr || !from->IsString() || !to->IsString() || from->AsString().empty() ||
            to->AsString().empty())
        {
            error = "an alias needs 'from' and 'to' ids";
            return false;
        }
        out[from->AsString()] = to->AsString();
    }
    return true;
}

bool ProfileStore::LoadAliases(const std::string& root, std::string& error)
{
    const std::string path = AliasesPath(root);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return true;
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    std::map<HostDeviceId, HostDeviceId> aliases;
    if (!in || !ParseAliases(text.str(), aliases, error))
    {
        error = path + ": " + (error.empty() ? std::string("cannot be read") : error);
        return false;
    }
    for (const auto& [from, to] : aliases)
        AddAlias(from, to);
    return true;
}

bool ProfileStore::SaveAliases(const std::string& root, std::string& error) const
{
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    const std::string path = AliasesPath(root);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << DumpAliases(_aliases);
    if (!out)
    {
        error = path + ": cannot be written";
        return false;
    }
    return true;
}

/// endregion </Aliases>

bool ProfileStore::Save(const Profile& profile, const std::string& root, std::string& error)
{
    namespace fs = std::filesystem;
    const std::string name = FileName(profile);
    if (name.empty() || name[0] == '.')
    {
        error = "'" + profile.id + "' does not name a file";
        return false;
    }
    const fs::path file = SavePath(profile, root);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec)
    {
        error = file.parent_path().string() + ": " + ec.message();
        return false;
    }
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << Dump(profile);
        if (!out)
        {
            error = file.string() + ": cannot be written";
            return false;
        }
    }
    // The JSON an older version wrote for this profile (play/ held the game profiles then)
    fs::remove(fs::path(root) / Directory(profile) / (name + ".json"), ec);
    if (profile.IsGame())
        fs::remove(fs::path(root) / "play" / (name + ".json"), ec);
    return true;
}

}  // namespace controllers
