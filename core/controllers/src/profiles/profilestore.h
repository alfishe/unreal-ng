#pragma once

/// @file profilestore.h
/// @brief The controller profiles (mapping-and-profiles.md §6-§8): built-in ⊂ community ⊂ user, each a `play/` or
/// `debug/` file; the ones that apply to a machine, a program and the connected devices merge field by field,
/// global → machine → family → device → game, into the router's binding scopes.

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

/// The merge, per router scope
struct ResolvedProfiles
{
    /// "" (every device), "family:<family>", a device id: each the whole merge up to its specificity
    std::vector<std::pair<std::string, std::vector<Binding>>> scopes;
    std::map<std::string, ConnectorPlan> connectors;  ///< what the machine's connectors should hold
    std::vector<PlayerPin> players;
    std::vector<std::string> applied;   ///< the profile ids of the "" scope, lowest first
    std::vector<std::string> warnings;  ///< bindings dropped because their merge does not compile
    /// The haptics of the most specific profile that has a `haptics` section (whole, not merged), and its id
    JsonValue haptics;
    std::string hapticsProfile;
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
    /// `<root>/play/*.json` and `<root>/debug/*.json`; a profile in the wrong directory is refused. The count kept;
    /// every refused file is in @p errors as "<path>: <reason>"
    size_t LoadDirectory(const std::string& root, ProfileScope scope, std::vector<std::string>& errors);

    const Profile* Find(const std::string& id) const;
    std::vector<const Profile*> Profiles() const;

    ResolvedProfiles Resolve(const ProfileContext& context) const;
    /// Resolve, then the router's bindings and player connectors; scopes applied before and gone now are cleared
    ResolvedProfiles Apply(InputRouter& router, const ProfileContext& context);

    /// A profile as JSON (schema 1), for a user file
    static std::string Dump(const Profile& profile);
    /// Write @p profile to `<root>/<play|debug>/<file>.json`, the file named after the id's last part
    static bool Save(const Profile& profile, const std::string& root, std::string& error);

private:
    std::vector<Profile> _profiles;
    std::vector<std::string> _appliedScopes;
};

/// The compiled-in profiles' JSON texts
const std::vector<const char*>& BuiltinProfileTexts();

}  // namespace controllers
