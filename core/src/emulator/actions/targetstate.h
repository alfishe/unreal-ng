#pragma once

/// @file targetstate.h
/// @brief Which instance each control surface drives (Stream Deck integration targeting.md, decision D5): the views
/// of the host (a main window, the video wall, ...), each with one active instance, in the order the user last
/// focused them; the followed instance; the surface sessions (a deck, a REST surface) and their pins.
///
/// Rules (targeting.md §3):
/// 1. A view has one active instance; the client that owns the view reports it (SetActive).
/// 2. A view moves to the front of the focus order when it gets keyboard focus inside the host (Activated); focus
///    moving to another program does not call it.
/// 3. The followed instance is the active instance of the first view in focus order; a view without one is skipped.
/// 4. No view with an active instance (headless, or the views are empty): the selected instance, else the sole one.
/// 5. Automation selecting an instance does not change the focus order: the GUI's reaction (it adopts the selected
///    instance) changes its view's active instance, and through it the followed one.
/// 6. Every surface uses the same rule: following (the followed instance) or a pin.
///
/// Pins (targeting.md §4): `view:<id>` follows that view's active instance; `instance:<id>` stays on that instance.
/// A pin on a view that closes, or an instance that is destroyed, falls back to following, with a notice.
///
/// A change of the followed instance, of the views or of a session's target posts NC_SURFACE_TARGET
/// (SurfaceTargetPayload) on the default message center. This is the hook of the WebSocket `surface` topic (control
/// surfaces S4): on the notification it reads TargetFor(session) for its sessions and sends `target` messages.
///
/// Process-wide (Instance()), thread-safe, headless as well: the core owns it, the Qt client and the video wall
/// register their views. Reads never touch a machine.

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace actions
{

/// What a surface session follows
struct Pin
{
    enum class Kind : uint8_t
    {
        Follow,   ///< the followed instance
        View,     ///< the active instance of a view
        Instance  ///< one instance
    };
    Kind kind = Kind::Follow;
    std::string id;  ///< the view or instance id; "" for Follow

    static Pin Follow() { return {}; }
    static Pin OnView(std::string view) { return {Kind::View, std::move(view)}; }
    static Pin OnInstance(std::string instance) { return {Kind::Instance, std::move(instance)}; }

    /// "follow", "view", "instance" (the protocol's `mode`)
    const char* ModeName() const;
    /// "follow", "view:<id>", "instance:<id>"
    std::string ToText() const;
    /// "follow" (or ""), "view:<id>", "instance:<id>"; false with `error` otherwise
    static bool FromText(std::string_view text, Pin& pin, std::string& error);

    bool operator==(const Pin& other) const { return kind == other.kind && id == other.id; }
    bool operator!=(const Pin& other) const { return !(*this == other); }
};

struct ViewInfo
{
    std::string id;              ///< "v1", "v2", ... (never reused in a process)
    std::string name;            ///< "Main window", "Video wall"
    std::string activeInstance;  ///< "" = none
    std::string owner;           ///< who registered it: "unreal-qt", "videowall", "api"
};

/// How the followed instance was found
enum class FollowSource : uint8_t
{
    View,      ///< rule 3: the active instance of the front view that has one
    Selected,  ///< rule 4: the selected instance
    Sole,      ///< rule 4: the only instance
    None       ///< nothing to follow
};
const char* FollowSourceName(FollowSource source);

/// A surface's target: the instance and the view it reaches it through
struct TargetInfo
{
    std::string instance;  ///< "" = no machine
    std::string view;      ///< the view showing it ("" headless, or an instance no view shows)
    std::string viewName;
    Pin pin;               ///< what the surface follows (Follow for the followed instance itself)
    FollowSource source = FollowSource::None;

    bool operator==(const TargetInfo& other) const
    {
        return instance == other.instance && view == other.view && pin == other.pin;
    }
    bool operator!=(const TargetInfo& other) const { return !(*this == other); }
};

/// A surface that drives an instance: a deck (one session per serial, the `surface` topic's join), a REST surface
struct SurfaceSessionSpec
{
    std::string name;    ///< "Stream Deck + XL"
    std::string kind;    ///< "companion", "host_device", "api"
    std::string serial;  ///< a deck's serial ("" when none)
    std::string owner;   ///< the companion id behind it ("" when none)
};

struct SurfaceSessionInfo
{
    std::string id;  ///< "s1", "s2", ...
    SurfaceSessionSpec spec;
    Pin pin;
    TargetInfo target;
    /// Why the pin fell back to following ("the view 'Window 2' closed", "the instance is gone"); "" when it did not.
    /// Cleared by the next SetPin
    std::string notice;
};

class TargetState;

/// A registration of a view or a session: unregisters when it dies. Move-only
class TargetHandle
{
public:
    TargetHandle() = default;
    TargetHandle(TargetHandle&& other) noexcept;
    TargetHandle& operator=(TargetHandle&& other) noexcept;
    TargetHandle(const TargetHandle&) = delete;
    TargetHandle& operator=(const TargetHandle&) = delete;
    ~TargetHandle();

    void Reset();
    const std::string& Id() const { return _id; }
    explicit operator bool() const { return _state != nullptr; }

private:
    friend class TargetState;
    enum class Kind : uint8_t
    {
        View,
        Session
    };
    TargetHandle(TargetState* state, Kind kind, std::string id) : _state(state), _kind(kind), _id(std::move(id)) {}

    TargetState* _state = nullptr;
    Kind _kind = Kind::View;
    std::string _id;
};
using ViewHandle = TargetHandle;
using SessionHandle = TargetHandle;

class TargetState
{
public:
    /// Rule 4: the instance to follow without a view (default: the selected instance, else the sole one, from
    /// EmulatorManager). Called outside the state's lock
    using Fallback = std::function<std::pair<std::string, FollowSource>()>;

    /// The process's state (never destroyed); follows the selection and instance creation (NC_EMULATOR_*)
    static TargetState& Instance();
    /// Does an instance exist (an `instance:` pin is checked; default: EmulatorManager)
    using Exists = std::function<bool(const std::string& instanceId)>;
    /// `post`: changes post NC_SURFACE_TARGET (the process's state does; a test's may)
    explicit TargetState(Fallback fallback = {}, bool post = false, Exists exists = {});
    ~TargetState();
    TargetState(const TargetState&) = delete;
    TargetState& operator=(const TargetState&) = delete;

    /// region <Views>
    /// A new view, at the back of the focus order (it moves to the front when Activated)
    [[nodiscard]] ViewHandle RegisterView(std::string name, std::string owner = {});
    /// The view's active instance ("" = none)
    void SetActive(const ViewHandle& view, std::string instanceId);
    /// The view got keyboard focus inside the host: it moves to the front of the focus order
    void Activated(const ViewHandle& view);
    /// By id, for views registered over an automation plane; false when there is no such view
    bool SetActiveById(const std::string& viewId, std::string instanceId);
    bool ActivatedById(const std::string& viewId);
    /// Every view in focus order (front first)
    std::vector<ViewInfo> Views() const;
    std::optional<ViewInfo> View(const std::string& viewId) const;
    bool HasViews() const;
    /// endregion </Views>

    /// region <Targets>
    /// Rules 3 and 4: the followed instance ("" = none)
    std::string Followed() const;
    TargetInfo FollowedTarget() const;
    /// A pin's target now (an unknown view or instance: no instance)
    TargetInfo Resolve(const Pin& pin) const;
    /// endregion </Targets>

    /// region <Sessions>
    /// A surface session following `pin` (an invalid pin: Follow)
    [[nodiscard]] SessionHandle OpenSession(SurfaceSessionSpec spec, Pin pin = {});
    /// Pin (or unpin: Follow) a session. False with `error` "Not Found" (no such session, view or instance) or
    /// "not_now" (a view pin while the host has no views), and `message`
    bool SetPin(const std::string& sessionId, const Pin& pin, std::string& error, std::string& message);
    std::optional<SurfaceSessionInfo> Session(const std::string& sessionId) const;
    /// Every session in opening order, each with its target now
    std::vector<SurfaceSessionInfo> Sessions() const;
    /// The session's target now (no such session: nullopt)
    std::optional<TargetInfo> TargetFor(const std::string& sessionId) const;
    /// endregion </Sessions>

    /// region <Lifetime>
    /// The instance is gone: views showing it lose their active instance, `instance:` pins on it fall back
    void InstanceGone(const std::string& instanceId);
    /// The machine was replaced (a model switch): views and `instance:` pins move to the new id
    void InstanceReplaced(const std::string& oldId, const std::string& newId);
    /// Something rule 4 reads changed (the selection, the instance list): post when the targets moved
    void Refresh();
    /// Bumped on every change it posts
    uint64_t Revision() const;
    /// endregion </Lifetime>

private:
    friend class TargetHandle;
    struct ViewEntry
    {
        ViewInfo info;
    };
    struct SessionEntry
    {
        std::string id;
        SurfaceSessionSpec spec;
        Pin pin;
        std::string notice;
    };
    struct Snapshot
    {
        std::string followed;
        std::vector<ViewInfo> views;
        std::map<std::string, TargetInfo> targets;
    };

    void Unregister(TargetHandle::Kind kind, const std::string& id);
    /// Under the lock: the target of `pin` given rule 4's answer
    TargetInfo ResolveLocked(const Pin& pin, const std::pair<std::string, FollowSource>& fallback) const;
    TargetInfo FollowedLocked(const std::pair<std::string, FollowSource>& fallback) const;
    std::pair<std::string, FollowSource> FallbackNow() const;
    Snapshot SnapshotLocked(const std::pair<std::string, FollowSource>& fallback) const;
    /// Compare with the last posted snapshot; post NC_SURFACE_TARGET when anything moved. `reason` says why
    void Publish(const char* reason);

    Fallback _fallback;
    Exists _exists;
    mutable std::mutex _mutex;
    std::vector<ViewEntry> _views;  ///< focus order, front first
    std::vector<SessionEntry> _sessions;
    uint64_t _nextView = 1;
    uint64_t _nextSession = 1;
    uint64_t _revision = 0;
    Snapshot _last;
    std::mutex _publishMutex;  ///< one Publish at a time, so posts come in the order of the changes
    bool _post = false;
};

}  // namespace actions
