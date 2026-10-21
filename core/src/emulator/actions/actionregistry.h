#pragma once

/// @file actionregistry.h
/// @brief The host actions every binding source and every automation plane calls by id (control surfaces design
/// docs/inprogress/2026-10-18-control-surfaces/architecture.md §3, actions.md §1): a pad or the Surface Dial through
/// the Qt client, the WebAPI (POST /api/v1/controllers/actions/{id}) and the WebSocket call of it, MCP `actions`,
/// the CLI `action`, Lua and Python `action()`.
///
/// An action has an id, a label, a value kind, a rule (the companion scope it needs, as a function of the value and
/// the machine; whether it needs a pause; when it cannot run now) and an executor. The core registers its actions
/// (run control, time travel, machine) when the registry is first used; the Qt client adds its GUI actions with a
/// RegistrationHandle it keeps until it closes.
///
/// **One actions worker per instance** runs the executors of that instance in submission order, so an executor may
/// block (a seek waits for the machine, a step runs it) while nothing else waits: never the emulation thread, never a
/// GUI or network thread. The worker starts with the first action for its instance and ends when the instance is
/// removed (EmulatorManager) or the registry shuts down; queued jobs then answer "Not Found".
///
/// **Gates** (architecture §4) are applied inside the registry, for every origin, at the moment the job runs:
/// - WebAPI, MCP, CLI: the automation gate. A changing action without a session opens the implicit run-control session
///   for that client (EmulatorContext::EnterImplicitRunControl), exactly as the REST routes do; the result names it.
/// - Lua, Python: the script gate (execution::script::Check), as the script's own control calls.
/// - Companion: companion::CheckAction (scope, a holding session, a needed pause).
/// - GUI, host device: the user's own hands, no session; a changing action is refused "Run-control held" while
///   another surface holds the claim.
/// A release that only ends what a press started (a held action's release, a level of 0) passes every gate.
///
/// **Coalescing**: a Delta merges into the last queued job only, when the id, the origin's identity and session and
/// the params are equal; the merged job keeps the higher rate and every caller's `done` gets its result.

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emulator/execution/executionarbiter.h"
#include "emulator/state/statenode.h"

class Emulator;

namespace actions
{

class ActivityFeed;

enum class ValueKind : uint8_t
{
    Press,  ///< a button: down / up (held actions see both)
    Delta,  ///< signed steps with a rate (an encoder, the Dial)
    Level,  ///< 0..1, 0 = released (an analog trigger's scrub)
    Text    ///< a string (ctx.set)
};

const char* ValueKindName(ValueKind kind);
/// "press", "delta", "level", "text"; false for anything else
bool ValueKindFromName(std::string_view name, ValueKind& kind);

struct ActionValue
{
    ValueKind kind = ValueKind::Press;
    bool pressed = true;   ///< Press: down / up
    double delta = 0;      ///< Delta: signed steps, fractional allowed
    double rate = 0;       ///< Delta: steps per second
    double level = 0;      ///< Level: 0..1
    std::string text;      ///< Text
    StateNode params = StateNode::Object();  ///< per-action parameters (run.to_scanline: line, run.n_scanlines: count)
};

/// A value from text, for the CLI and the scripts: Press "press" / "down" / "1" / "" (a press), "release" / "up" /
/// "0" (a release); Delta and Level a number; Text the text. False with `error` when it does not parse
bool ValueFromText(ValueKind kind, const std::string& text, ActionValue& value, std::string& error);

struct ActionOrigin
{
    execution::Origin origin = execution::Origin::Gui;
    /// Who: the automation gate's identity (one implicit session per identity: "webapi|host|client", "cli|peer"),
    /// the companion's id, the host device's id. Coalescing never crosses identities
    std::string identity;
    std::string client;   ///< a name the GUI shows ("python-urllib/3.12", "MCP agent", the companion's name)
    std::string peer;     ///< host:port of a network client
    /// The run-control session the caller carries (X-Unreal-Session, the CLI connection's session); "" none
    std::string session;
    /// The idle lease of an implicit session the gate opens (WebAPI: 15 s; CLI: 0, it ends with the connection)
    std::chrono::milliseconds lease{0};
};

struct ActionResult
{
    bool ok = false;
    /// "" on success; else as the routes answer: "Not Found", "Bad Request", "Conflict", "Not Implemented",
    /// "Run-control held", "not_now", "companion_forbidden", "held_by_session", "held_by_other", "revoked_by_user",
    /// "Timeout"
    std::string error;
    std::string message;  ///< one sentence a user can act on
    StateNode body = StateNode::Object();
    /// The automation gate opened (or went on in) this session for the caller: the WebAPI says so in its reply, the
    /// CLI keeps it for its connection
    std::string implicitSession;
    bool implicitOpened = false;

    static ActionResult Success(StateNode body = StateNode::Object(), std::string message = {});
    static ActionResult Failure(std::string error, std::string message, StateNode body = StateNode::Object());
    /// The HTTP status of the result: 200; 400, 403, 404, 409, 501, 504
    int HttpStatus() const;
};

struct ActionInfo
{
    std::string id;
    std::string label;
    std::string group;  ///< "Run", "Time travel", "Debugger", "Capture and machine"
    ValueKind kind = ValueKind::Press;
    bool held = false;  ///< a Press whose release matters too (turbo while held, reset after a 1 s hold)
    bool gui = false;   ///< registered by the Qt client; absent headless
    /// The parameters it reads from ActionValue::params, one line for the catalogue ("key, hold?, frames?"; a `?`
    /// marks an optional one); "" when it takes none
    std::string params;
    /// One sentence for a palette's tooltip and the MCP tool ("" at Register: the core's text for the id, catalogue.h)
    std::string description;
    /// It has an on / off state (ActionRules::state): the readout `toggle.<id>` (or `toggle.<stem>.<param>`) shows it.
    /// Set by Register from the rules
    bool toggle = false;
};

/// What an executor gets: the instance (never null), the value, who asks
struct ActionCall
{
    const ActionInfo& info;
    const std::shared_ptr<Emulator>& emulator;
    const std::string& instanceId;
    const ActionValue& value;
    const ActionOrigin& origin;
    /// Who asks the execution arbiter: the session the gate opened or found for the caller, else the origin alone
    execution::Requester requester;
};

using Executor = std::function<ActionResult(const ActionCall& call)>;

struct ActionRules
{
    /// The companion scope the call needs (automation_gate::CompanionScope bits); 0 = it changes nothing, nobody is
    /// gated. Null: 0. A function of the value and the machine: run.pause_toggle pausing needs none, resuming ScopeRun
    std::function<uint32_t(Emulator& emulator, const ActionValue& value)> scope;
    /// A paused machine is needed (steps, instruction steps): "needs a pause" for every origin otherwise
    bool needsPause = false;
    /// "" when it can run now, else why not ("no time travel history", "not available yet"). Reads published state
    /// only: never waits for the machine. Null: always available
    std::function<std::string(Emulator& emulator, const ActionValue& value)> unavailable;
    /// The error code of an `unavailable` answer ("not_now"; "Not Implemented" for the stubs)
    std::string unavailableError = "not_now";
    /// A toggle's state now (on / off; nullopt: not known, such as no machine part for it). Reads published state
    /// only, never waits for the machine (the readouts call it at up to 50 Hz). Null: not a toggle. `value.params`
    /// carries `toggleParam` when the action has one (feature.toggle: name)
    std::function<std::optional<bool>(Emulator& emulator, const ActionValue& value)> state;
    /// The readout name of a toggle: `toggle.<toggleStem>` ("" = the id), or with a param `toggle.<toggleStem>.<value
    /// of toggleParam>` (feature.toggle: stem "feature", param "name": `toggle.feature.turbomode`)
    std::string toggleStem;
    std::string toggleParam;

    static std::function<uint32_t(Emulator&, const ActionValue&)> Fixed(uint32_t scope);
};

class ActionRegistry;

/// Keeps a registration: the action is gone when the handle dies (the Qt client's GUI actions). Move-only
class RegistrationHandle
{
public:
    RegistrationHandle() = default;
    RegistrationHandle(RegistrationHandle&& other) noexcept;
    RegistrationHandle& operator=(RegistrationHandle&& other) noexcept;
    RegistrationHandle(const RegistrationHandle&) = delete;
    RegistrationHandle& operator=(const RegistrationHandle&) = delete;
    ~RegistrationHandle();

    void Reset();
    explicit operator bool() const { return _registry != nullptr; }

private:
    friend class ActionRegistry;
    RegistrationHandle(ActionRegistry* registry, std::string id, uint64_t serial)
        : _registry(registry), _id(std::move(id)), _serial(serial)
    {
    }

    ActionRegistry* _registry = nullptr;
    std::string _id;
    uint64_t _serial = 0;
};

/// One row of List(instance, origin): the action and whether it can run now for that caller
struct ActionStatus
{
    ActionInfo info;
    uint32_t scope = 0;  ///< the scope it needs with a default value on this machine now
    bool needsPause = false;
    bool available = false;
    std::string error;   ///< "" when available, else the code (as Submit would answer)
    std::string reason;  ///< "" when available, else why not
};

class ActionRegistry
{
public:
    using Done = std::function<void(ActionResult result)>;
    using Resolver = std::function<std::shared_ptr<Emulator>(const std::string& instanceId)>;

    /// The process's registry, with the core actions (builtinactions.h, hostactions.h) registered at its first use; it
    /// lives until the process ends (never destroyed, so the static EmulatorManager can drop instances in its own
    /// destructor)
    static ActionRegistry& Instance();
    /// An empty registry over `resolver` (default: EmulatorManager). Tests make their own
    explicit ActionRegistry(Resolver resolver = {});
    ~ActionRegistry();
    ActionRegistry(const ActionRegistry&) = delete;
    ActionRegistry& operator=(const ActionRegistry&) = delete;

    /// region <Catalogue>
    /// Register (or replace) an action; it stays while the handle lives
    [[nodiscard]] RegistrationHandle Register(ActionInfo info, Executor run, ActionRules rules = {});
    /// Every action in registration order
    std::vector<ActionInfo> List() const;
    std::optional<ActionInfo> Find(std::string_view id) const;
    /// Bumped by every Register and every unregistration (the protocol's `catalogue_rev`)
    uint64_t Revision() const;
    /// A toggle's state on `instanceId` (`params`: its toggleParam); nullopt for an unknown action, a non-toggle, no
    /// machine or no state
    std::optional<bool> ToggleState(std::string_view id, const std::string& instanceId,
                                    const StateNode& params = StateNode::Object()) const;
    /// The toggles as their readouts name them: {readout stem ("toggle.mach.turbo_toggle", "toggle.feature"), action
    /// id, toggleParam ("" when none)}
    struct ToggleReadout
    {
        std::string stem;
        std::string id;
        std::string param;
    };
    std::vector<ToggleReadout> ToggleReadouts() const;
    /// Every action with its availability on `instanceId` for `origin` (a dry run of the gates: no session is
    /// opened). Reads published state only. Without the instance every row is unavailable "no machine"
    std::vector<ActionStatus> Statuses(const std::string& instanceId, const ActionOrigin& origin) const;
    /// "" when `id` can run now on `instanceId` for `origin` with `value`, else the reason
    std::string Unavailable(std::string_view id, const std::string& instanceId, const ActionOrigin& origin,
                            const ActionValue& value) const;
    /// endregion </Catalogue>

    /// region <Running>
    /// Queue `id` on the instance's actions worker; `done` is called on that worker with the result - or at once on
    /// the calling thread for an unknown action, an unknown instance, a wrong value kind and a release that does
    /// nothing (a Press action that is not held)
    void Submit(std::string_view id, ActionValue value, const std::string& instanceId, ActionOrigin origin, Done done);
    /// Submit and wait up to `timeout` for the result (the CLI and the scripts): "Timeout" when it did not come
    /// (the job still runs). Never call it from the instance's actions worker
    ActionResult Run(std::string_view id, ActionValue value, const std::string& instanceId, ActionOrigin origin,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
    /// The instance is gone: its queued jobs answer "Not Found", its worker ends after the running job (waited for
    /// up to 2 s, then left to finish on its own)
    void DropInstance(const std::string& instanceId);
    /// Every worker, as DropInstance (process shutdown; the destructor does it too)
    void Shutdown();
    /// Workers alive (tests)
    size_t WorkerCount() const;
    /// Where the workers record what they ran (ActivityFeed::Instance() for the process's registry; null: nowhere)
    void SetActivityFeed(ActivityFeed* feed);
    /// endregion </Running>

    class Worker;
    struct Entry;

private:
    friend class RegistrationHandle;
    void Unregister(const std::string& id, uint64_t serial);
    std::shared_ptr<const Entry> Lookup(std::string_view id) const;
    std::shared_ptr<Emulator> Resolve(const std::string& instanceId) const;

    Resolver _resolver;
    mutable std::mutex _mutex;
    std::map<std::string, std::shared_ptr<const Entry>, std::less<>> _entries;
    std::vector<std::string> _order;
    uint64_t _nextSerial = 1;
    std::map<std::string, std::shared_ptr<Worker>> _workers;
    bool _shutDown = false;
    std::vector<RegistrationHandle> _builtin;  ///< the core actions of Instance()
    uint64_t _revision = 0;
    ActivityFeed* _feed = nullptr;
    bool _post = false;  ///< Instance(): a catalogue change posts NC_ACTION_CATALOGUE
};

/// The gates of architecture §4 for one call, without running it. `open`: open the implicit session the automation
/// gate would (a real call); false: only predict (List). On success `requester` is who asks the arbiter and `result`
/// carries the implicit session (if any); on refusal `result` is the refusal
bool PassGates(const ActionInfo& info, const ActionRules& rules, Emulator& emulator, const ActionValue& value,
               const ActionOrigin& origin, bool open, execution::Requester& requester, ActionResult& result);

/// Does this value only end what a press started (a held action's release, a level of 0)? It passes every gate
bool IsRelease(const ActionInfo& info, const ActionValue& value);

}  // namespace actions
