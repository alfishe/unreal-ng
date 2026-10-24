#pragma once

/// @file profilestore.h
/// @brief The controller profiles (mapping-and-profiles.md §6-§8): built-in ⊂ community ⊂ user, YAML files under
/// `play/`, `debug/`, `schemes/` and `games/`; the ones that apply to a machine, a program and the connected devices
/// merge field by field, global → machine → family → device → game, into the router's binding scopes. The shipped game
/// profiles stay on disk behind `games/index.yaml` (§7.5) and are parsed on a match.

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "hostdevice.h"
#include "profiles/profile.h"

namespace controllers
{

class InputRouter;

/// What profiles are chosen for
struct ProfileContext
{
    std::string machine;                  ///< the model (`pentagon`, `next`); empty = any
    std::vector<std::string> signatures;  ///< the loaded program's (none until the signature library exists)
    std::vector<std::string> fileSha256;  ///< the loaded media files
    std::vector<std::string> manual;      ///< play profiles chosen by hand: they rank as game profiles
    std::vector<HostDeviceInfo> devices;  ///< the connected devices, in connection order
    /// The debug profiles take part (debug-and-ttd.md §1.6: the Qt client turns them on with Advanced > Debug layer)
    bool debugLayer = true;
};

/// One binding of the merge as written, and the profile its last field came from
struct EffectiveBinding
{
    std::string layer;    ///< "play", "debug", a shift layer
    std::string control;  ///< `south`, `view+menu`, `dpad.*`
    JsonValue spec;       ///< the merged fields (`{key: ENTER, label: Fire}`)
    std::string profile;
};

/// The merge, per router scope
struct ResolvedProfiles
{
    /// "" (every device), "family:<family>", a device id: each the whole merge up to its specificity
    std::vector<std::pair<std::string, std::vector<Binding>>> scopes;
    /// The same scopes as written, for the surfaces and the bindings editor
    std::vector<std::pair<std::string, std::vector<EffectiveBinding>>> effective;
    std::map<std::string, ConnectorPlan> connectors;  ///< what the machine's connectors should hold
    std::vector<PlayerPin> players;
    std::vector<std::string> applied;   ///< the profile ids of the "" scope, lowest first
    std::vector<std::string> warnings;  ///< bindings dropped because their merge does not compile
    /// The game haptics of the most specific play profile that has a `haptics` section (whole, not merged), and its id
    JsonValue haptics;
    std::string hapticsProfile;
    /// Per router scope (as `scopes`), the surface haptics of the most specific debug profile that has a `haptics`
    /// section (control surfaces profiles.md §1: per layer, whole, not merged); a scope without one is absent
    std::vector<std::pair<std::string, JsonValue>> surfaceHaptics;
};

/// One shipped game profile as the index lists it (mapping-and-profiles.md §7.5)
struct GameEntry
{
    std::string id;
    std::string label;
    std::vector<std::string> machines;
    std::string file;  ///< relative to the games directory, `/` separated
    size_t offset = 0;  ///< the profile's YAML document in the file (a bundle holds many)
    size_t length = 0;
    std::vector<std::string> keys;  ///< `sha256:<hex>`, `sig:<signature>`, `zxdb:<id>`; lower case
};

/// The shipped game profiles: the index read at start, a profile parsed when a key of the program or a manual choice
/// names it
class GameLibrary
{
public:
    /// `<gamesDir>/index.yaml`; rebuilt in memory (Build) when it is missing, unreadable or older than a profile file.
    /// False when the directory does not exist; every refused file is in @p errors as "<path>: <reason>"
    bool Open(const std::string& gamesDir, std::vector<std::string>& errors);
    const std::vector<GameEntry>& Entries() const
    {
        return _entries;
    }
    const GameEntry* FindId(const std::string& id) const;
    /// The entries a program's keys name (`sha256:<hex>` and so on), each once
    std::vector<const GameEntry*> Lookup(const std::vector<std::string>& keys) const;
    /// Parse one entry: its slice of the file, which must hold the profile the index names (else the index is rebuilt
    /// once: a checkout that changed the line ends moves the offsets)
    bool Load(const std::string& id, Profile& out, std::string& error);

    /// Every game file and bundle of @p gamesDir (recursively, `*.yaml`, not the index) as the index lists them
    static bool Build(const std::string& gamesDir, std::vector<GameEntry>& out, std::vector<std::string>& errors);
    static std::string DumpIndex(const std::vector<GameEntry>& entries);
    static bool ParseIndex(const std::string& text, std::vector<GameEntry>& out, std::string& error);
    /// The keys a profile is found by
    static std::vector<std::string> Keys(const Profile& profile, const JsonValue& root);

private:
    void Reindex();

    std::string _dir;
    std::vector<GameEntry> _entries;
    std::multimap<std::string, size_t> _byKey;
    std::map<std::string, size_t> _byId;
};

class ProfileStore
{
public:
    /// The compiled-in defaults (§8)
    ProfileStore();

    /// Keep @p profile; it replaces a profile of the same id and no higher scope. False with the reason otherwise
    bool Add(Profile profile, std::string& error);
    bool Remove(const std::string& id);
    /// Parse and keep @p text as one profile of @p scope
    bool AddText(const std::string& text, ProfileScope scope, std::string& error);
    /// `<root>/{play,debug,schemes,games}/` (`games/` recursively), `*.yaml` (a file of `games/` may be a bundle) and
    /// the `*.json` of older versions; a profile in the wrong directory is refused. The count kept; every refused file
    /// is in @p errors as "<path>: <reason>"
    size_t LoadDirectory(const std::string& root, ProfileScope scope, std::vector<std::string>& errors);

    /// The shipped game profiles (GameLibrary::Open)
    bool OpenGames(const std::string& gamesDir, std::vector<std::string>& errors);
    const GameLibrary& Games() const
    {
        return _games;
    }
    /// Parse and keep the shipped game profiles @p context names (file hashes, signatures, manual ids) that are not
    /// kept yet; Apply does it first. Every one refused is in @p errors
    size_t LoadGamesFor(const ProfileContext& context, std::vector<std::string>& errors);

    const Profile* Find(const std::string& id) const;
    std::vector<const Profile*> Profiles() const;

    ResolvedProfiles Resolve(const ProfileContext& context) const;
    /// Resolve, then the router's bindings and player connectors; scopes applied before and gone now are cleared
    ResolvedProfiles Apply(InputRouter& router, const ProfileContext& context);

    /// A profile as a YAML document (schema 1), for a user file
    static std::string Dump(const Profile& profile);
    /// The same tree as JSON, for the WebAPI / MCP
    static JsonValue ToJson(const Profile& profile);
    /// Write @p profile to `<root>/<play|debug|schemes|games>/<file>.yaml` (a game profile to `games/`), the file
    /// named after the id's last part; a `.json` of the same name an older version wrote goes
    static bool Save(const Profile& profile, const std::string& root, std::string& error);
    /// The file Save writes
    static std::string SavePath(const Profile& profile, const std::string& root);

    /// region <Aliases>
    /// A promoted device (architecture.md §5): a `gc:` device GameController alone saw, later matched to its IOHID
    /// endpoint, goes on under the IOHID id; the old id becomes an alias of the new one, so the profiles and player
    /// pins naming the old id apply to the new id (Resolve, Apply). An alias of @p from moves to @p to as well
    void AddAlias(const HostDeviceId& from, const HostDeviceId& to);
    /// The old ids of @p device
    std::vector<HostDeviceId> AliasesOf(const HostDeviceId& device) const;
    /// Old id -> new id
    const std::map<HostDeviceId, HostDeviceId>& Aliases() const
    {
        return _aliases;
    }
    /// `<root>/aliases.yaml`, kept across restarts: LoadAliases adds the file's (a missing file is no error)
    bool LoadAliases(const std::string& root, std::string& error);
    bool SaveAliases(const std::string& root, std::string& error) const;
    static std::string AliasesPath(const std::string& root);
    static std::string DumpAliases(const std::map<HostDeviceId, HostDeviceId>& aliases);
    static bool ParseAliases(const std::string& text, std::map<HostDeviceId, HostDeviceId>& out, std::string& error);
    /// endregion </Aliases>

private:
    std::vector<Profile> _profiles;
    std::map<HostDeviceId, HostDeviceId> _aliases;  ///< old id -> new id
    std::vector<std::string> _appliedScopes;
    std::vector<std::string> _hapticScopes;  ///< the scopes Apply gave surface haptics
    GameLibrary _games;
};

/// A compiled-in profile: data/controllers/profiles/{play,debug,schemes}/*.yaml
struct BuiltinProfileText
{
    const char* name;  ///< `play/pad-default.yaml`
    const char* text;
};
const std::vector<BuiltinProfileText>& BuiltinProfileTexts();

}  // namespace controllers
