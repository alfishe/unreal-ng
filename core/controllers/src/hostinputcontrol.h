#pragma once

/// @file hostinputcontrol.h
/// @brief The host side of the controller verbs for every surface (surfaces.md §1-§2): the profiles, the effective
/// bindings, bind / unbind in a user profile, the manual game profile, the connected devices. One per process, as
/// ControllerControl is one per machine: WebAPI (and MCP through it), CLI and Python turn their input into a
/// HostRequest and the HostReply into their output, so the surfaces cannot drift apart.
///
/// The profiles and the router belong to the process's host input owner (the Qt client's HostControllers): it
/// registers itself with SetOwner and a runner that executes a request on its own thread.
///
/// Verbs: devices, context, profiles, profile, save, delete, apply, bindings, bind, unbind, scheme (the scheme a user
/// profile extends: how the pad works, Kempston, Sinclair 1, QAOP + Space), haptics (what a device's motors can do, or
/// why not), rumble (play a test effect on a device, haptics.md §10), output (a control surface's pattern, steps and
/// clicks, or a seek's marks as its layer plays them: control surfaces controls-and-feedback.md §3), light (a pad's
/// light colour: a DualSense / DualShock 4 light bar), aliases (the old ids of promoted devices: a `gc:` device later
/// matched to its IOHID endpoint, architecture.md §5), unalias (forget one), match (the game profiles a program's
/// signatures choose, D7).
///
/// @code
///   HostRequest request{"apply", {{"id", "builtin/game/vradarks-revenge"}}};
///   HostReply reply = HostInputControl::Process(request);
///   http.status = reply.HttpStatus();  http.body = reply.ToJson();
/// @endcode

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "profiles/json.h"
#include "profiles/profilestore.h"

namespace controllers
{

class HostInputService;

struct HostRequest
{
    std::string verb;
    /// Options by name (surfaces.md §2): id, set, text, control, spec, layer, family, device, profile, effect, color
    std::map<std::string, std::string> options;
};

struct HostReply
{
    std::string error;    ///< empty = ok; surfaces.md §4: unknown-profile, unknown-device, unknown-alias,
                          ///< bad-request, invalid-binding, read-only, unavailable
    std::string message;  ///< why, for a person (and an agent that reads only the text)
    JsonValue body = JsonValue::Object();  ///< verb-specific fields, merged into the envelope

    bool Ok() const
    {
        return error.empty();
    }
    /// {ok, error, message, ...body}
    JsonValue ToValue() const;
    std::string ToJson() const;
    int HttpStatus() const;
};

/// What the host input owner gives the control
class IHostInputOwner
{
public:
    virtual ~IHostInputOwner() = default;
    virtual ProfileStore& Profiles() = 0;
    /// The context of the last apply: machine, file hashes, manual choice, devices
    virtual ProfileContext Context() const = 0;
    virtual const ResolvedProfiles& Resolved() const = 0;
    /// `<user config>/unreal-ng/controllers/profiles`
    virtual std::string UserProfileDir() const = 0;
    virtual std::string ManualProfile() const = 0;
    /// Choose the game profile by hand (empty: by the loaded file); applies at once
    virtual void SetManualProfile(const std::string& id) = 0;
    /// Resolve and apply the profiles again (after a change in the store)
    virtual void Reapply() = 0;
    /// The signatures of the file at @p path into @p context (signatures, fileSha256, fileSet): the owner links the
    /// signature library (core/swsignature), this library does not. False with the reason; the default says there is
    /// no signature library in this process
    virtual bool SignaturesOf(const std::string& path, ProfileContext& context, std::string& error)
    {
        (void)path;
        (void)context;
        error = "no signature library in this process";
        return false;
    }
    /// The service whose motors haptics / rumble drive; nullptr: no host output in this process
    virtual HostInputService* Input()
    {
        return nullptr;
    }
};

class HostInputControl
{
public:
    /// Runs a request on the owner's thread and returns when it ran
    using Runner = std::function<void(const std::function<void()>&)>;

    explicit HostInputControl(IHostInputOwner& owner) : _owner(owner)
    {
    }

    /// On the owner's thread
    HostReply Execute(const HostRequest& request);

    static const std::vector<std::string>& Verbs();
    static const std::vector<std::string>& OptionsFor(const std::string& verb);
    /// The user profile bind / unbind / scheme write without a named profile: user/device-<id>, user/<family>,
    /// user/global
    static std::string UserProfileFor(const std::string& device, const std::string& family);

    /// The process's owner (nullptr when it goes); @p run defaults to calling in place
    static void SetOwner(IHostInputOwner* owner, Runner run = {});
    /// Execute on the registered owner's thread; `unavailable` with the reason when there is none
    static HostReply Process(const HostRequest& request);

private:
    HostReply Devices();
    HostReply Context();
    HostReply List(const HostRequest& request);
    HostReply Get(const HostRequest& request);
    HostReply Save(const HostRequest& request);
    HostReply Delete(const HostRequest& request);
    HostReply Apply(const HostRequest& request);
    HostReply Bindings(const HostRequest& request);
    HostReply Bind(const HostRequest& request, bool unbind);
    HostReply Scheme(const HostRequest& request);
    HostReply Haptics(const HostRequest& request);
    HostReply Rumble(const HostRequest& request);
    /// A surface's outputs (control surfaces controls-and-feedback.md §3): a pattern, the steps / clicks, or a seek's
    /// marks as the device's top layer plays them
    HostReply Output(const HostRequest& request);
    /// A pad's light colour (architecture.md §10)
    HostReply Light(const HostRequest& request);
    /// The device aliases: old id -> new id, whether the new one is connected
    HostReply Aliases();
    /// Forget an alias (id= the old id), saved to aliases.yaml and applied at once
    HostReply Unalias(const HostRequest& request);
    /// The game profiles a program's signatures match (D7), the one chosen and the haptics reason (H-3): `path` (the
    /// owner computes its signatures), `signatures` / `file_sha256` / `file_set` (comma lists) or the loaded program
    HostReply Match(const HostRequest& request);
    /// The user profile a request edits (profile=, else by device / family), copied from the store or new
    bool EditProfile(const HostRequest& request, bool mustExist, Profile& profile, std::string& id, HostReply& failure);
    /// Check through the reader, save, keep and apply
    HostReply Store(const Profile& profile, const std::string& message);

    /// The profile @p id, parsing a shipped game from the index when it is not kept yet
    const Profile* FindProfile(const std::string& id);

    IHostInputOwner& _owner;
};

}  // namespace controllers
