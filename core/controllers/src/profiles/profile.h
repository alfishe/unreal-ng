#pragma once

/// @file profile.h
/// @brief One controller profile, schema 1 (mapping-and-profiles.md §7), a YAML document: what it applies to, the
/// schemes it extends (§7.6), the connectors and
/// players it wants, and its bindings per layer as JSON objects, kept as written so profiles merge field by field
/// (§6.2) before they become router bindings.

#include <map>
#include <string>
#include <vector>

#include "profiles/json.h"
#include "router/binding.h"

namespace controllers
{

enum class ProfileKind : uint8_t
{
    Play,
    Debug,
};

/// Later wins (mapping-and-profiles.md §6.2)
enum class ProfileScope : uint8_t
{
    Builtin,
    Community,
    User,
};

/// Later wins
enum class Specificity : uint8_t
{
    Global,
    Machine,
    Family,
    Device,
    Game,
};

struct ConnectorPlan
{
    std::string device;  ///< a RetroDeviceKind name (`kempston-joystick`, `md6-pad`)
    JsonValue options;   ///< the rest of the entry as written (`mode`, ...), for the plug
};

struct PlayerPin
{
    std::string device;  ///< `auto:N` (the N-th device connected, from 1) or a substring of a HostDeviceId
    std::string connector;
};

struct Profile
{
    ProfileKind kind = ProfileKind::Play;
    ProfileScope scope = ProfileScope::Builtin;
    std::string id;
    std::string label;
    std::string path;  ///< the file; empty for a compiled-in one
    /// `kind: scheme` (§7.6): a play profile no context selects by itself, merged below the profiles that extend it
    bool scheme = false;
    std::vector<std::string> extends;  ///< scheme ids, merged in this order below this profile

    /// match: a game profile names signatures or file hashes; `machines` narrows any profile to machine models
    std::vector<std::string> machines;
    std::vector<std::string> signatures;
    std::vector<std::string> fileSha256;
    /// match.file_set: the TR-DOS file set as swsignature tokens (`c:<16 hex>` code, `d:<16 hex>` other), matched by
    /// the fuzzy rule (core/swsignature/src/match.h: Jaccard >= 0.8, no code file missing)
    std::vector<std::string> fileSet;
    std::string title;
    /// applies_to
    std::vector<std::string> families;
    std::vector<std::string> devices;

    std::map<std::string, ConnectorPlan> connectors;
    std::vector<PlayerPin> players;
    JsonValue options;  ///< defaults of every binding of the profile (an object, maybe empty)
    /// layer ("play", "debug", a shift layer) -> control (`south`, `view+menu`) -> the binding as written
    std::vector<std::pair<std::string, JsonValue::Members>> layers;
    /// `haptics` as written (haptics.md §7), checked by ParseHapticProfile; null when the profile has none
    JsonValue haptics;

    Specificity GetSpecificity() const;
    bool IsGame() const
    {
        return !signatures.empty() || !fileSha256.empty() || !fileSet.empty();
    }
};

/// Parse and validate a profile (every binding compiles): YAML, or JSON when the text starts with `{`; false with the
/// reason, which names the place
bool ParseProfile(const std::string& text, Profile& out, std::string& error);
/// The same on a parsed tree
bool ParseProfile(const JsonValue& root, Profile& out, std::string& error);

/// The JSON layer name of a router layer and back ("play" <-> kPlayLayer)
std::string RouterLayer(const std::string& jsonLayer);

/// One binding as written -> a router binding. @p control is the JSON key (`south`, `view+menu` = a chord),
/// @p defaults the options of the profile(s); false with the reason
bool CompileBinding(const std::string& control, const JsonValue& spec, const JsonValue& defaults,
                    const std::string& layer, Binding& out, std::string& error);

/// The fields of a binding that name its target: a later profile's target replaces all of them
bool IsTargetField(const std::string& field);

}  // namespace controllers
