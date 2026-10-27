#include "emulator/actions/actionregistry.h"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <exception>
#include <future>
#include <stdexcept>
#include <thread>
#include <utility>

#include "common/logger.h"
#include "common/uuid.h"
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/actions/activityfeed.h"
#include "emulator/actions/builtinactions.h"
#include "emulator/actions/catalogue.h"
#include "emulator/actions/hostactions.h"
#include "emulator/actions/pictureactions.h"
#include "emulator/actions/steppingactions.h"
#include "emulator/actions/hostactions_debugtools.h"
#include "emulator/coordinator/machineop.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/companionregistry.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/execution/scriptsession.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

namespace actions
{

/// region <Values and results>

const char* ValueKindName(ValueKind kind)
{
    switch (kind)
    {
        case ValueKind::Press: return "press";
        case ValueKind::Delta: return "delta";
        case ValueKind::Level: return "level";
        case ValueKind::Text: return "text";
    }
    return "press";
}

bool ValueKindFromName(std::string_view name, ValueKind& kind)
{
    for (ValueKind candidate : {ValueKind::Press, ValueKind::Delta, ValueKind::Level, ValueKind::Text})
        if (name == ValueKindName(candidate))
        {
            kind = candidate;
            return true;
        }
    return false;
}

bool ValueFromText(ValueKind kind, const std::string& text, ActionValue& value, std::string& error)
{
    value = ActionValue{};
    value.kind = kind;
    switch (kind)
    {
        case ValueKind::Press:
            if (text.empty() || text == "press" || text == "down" || text == "1" || text == "true")
                value.pressed = true;
            else if (text == "release" || text == "up" || text == "0" || text == "false")
                value.pressed = false;
            else
            {
                error = "a press takes press or release, not '" + text + "'";
                return false;
            }
            return true;
        case ValueKind::Delta:
        case ValueKind::Level:
        {
            char* end = nullptr;
            const double number = text.empty() ? 0.0 : std::strtod(text.c_str(), &end);
            if (text.empty() || end == nullptr || *end != '\0')
            {
                error = std::string("a ") + ValueKindName(kind) + " takes a number, not '" + text + "'";
                return false;
            }
            if (kind == ValueKind::Delta)
                value.delta = number;
            else
            {
                value.level = std::clamp(number, 0.0, 1.0);
                value.pressed = value.level > 0;
            }
            return true;
        }
        case ValueKind::Text:
            value.text = text;
            return true;
    }
    return true;
}

ActionResult ActionResult::Success(StateNode body, std::string message)
{
    ActionResult result;
    result.ok = true;
    result.body = std::move(body);
    result.message = std::move(message);
    return result;
}

ActionResult ActionResult::Failure(std::string error, std::string message, StateNode body)
{
    ActionResult result;
    result.ok = false;
    result.error = std::move(error);
    result.message = std::move(message);
    result.body = std::move(body);
    return result;
}

int ActionResult::HttpStatus() const
{
    if (ok)
        return 200;
    if (error == "Bad Request")
        return 400;
    if (error == "companion_forbidden")
        return 403;
    if (error == "Not Found")
        return 404;
    if (error == "Not Implemented")
        return 501;
    if (error == "Timeout")
        return 504;
    if (error == "Internal")
        return 500;
    return 409;  // Conflict, Run-control held, not_now, held_by_session, held_by_other, revoked_by_user
}

std::function<uint32_t(Emulator&, const ActionValue&)> ActionRules::Fixed(uint32_t scope)
{
    return [scope](Emulator&, const ActionValue&) { return scope; };
}

bool IsRelease(const ActionInfo& info, const ActionValue& value)
{
    if (info.kind == ValueKind::Press)
        return !value.pressed;
    if (info.kind == ValueKind::Level)
        return value.level <= 0;
    return false;
}

/// endregion </Values and results>

/// region <Gates>

namespace
{
bool SameNode(const StateNode& a, const StateNode& b)
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind)
    {
        case StateNode::Kind::Null: return true;
        case StateNode::Kind::Bool: return a.b == b.b;
        case StateNode::Kind::Int: return a.i == b.i;
        case StateNode::Kind::Double: return a.d == b.d;
        case StateNode::Kind::String: return a.s == b.s;
        case StateNode::Kind::Object:
            if (a.members.size() != b.members.size())
                return false;
            for (size_t i = 0; i < a.members.size(); i++)
                if (a.members[i].first != b.members[i].first || !SameNode(a.members[i].second, b.members[i].second))
                    return false;
            return true;
        case StateNode::Kind::Array:
            if (a.items.size() != b.items.size())
                return false;
            for (size_t i = 0; i < a.items.size(); i++)
                if (!SameNode(a.items[i], b.items[i]))
                    return false;
            return true;
    }
    return false;
}

/// The automation gate's surface label of an automation origin ("" for the others)
const char* AutomationLabel(execution::Origin origin)
{
    switch (origin)
    {
        case execution::Origin::WebApi: return "webapi";
        case execution::Origin::Mcp: return "mcp";
        case execution::Origin::Cli: return "cli";
        default: return "";
    }
}

bool IsScript(execution::Origin origin)
{
    return origin == execution::Origin::Lua || origin == execution::Origin::Python;
}

coordinator::OpSource OpSourceOf(execution::Origin origin)
{
    using coordinator::OpSource;
    switch (origin)
    {
        case execution::Origin::Gui: return OpSource::Qt;
        case execution::Origin::HostDevice: return OpSource::HostDevice;
        case execution::Origin::WebApi: return OpSource::WebApi;
        case execution::Origin::Mcp: return OpSource::Mcp;
        case execution::Origin::Cli: return OpSource::Cli;
        case execution::Origin::Lua: return OpSource::Lua;
        case execution::Origin::Python: return OpSource::Python;
        case execution::Origin::Gdb: return OpSource::Gdb;
        case execution::Origin::DeZog: return OpSource::DeZog;
        case execution::Origin::Companion: return OpSource::Companion;
        default: return OpSource::Core;
    }
}

std::string HolderName(const EmulatorContext::RunControlState& state)
{
    return state.client.empty() ? state.surfaceLabel : state.client;
}

/// The session `origin` carries, when it holds control on `context`
bool CarriesHeldSession(EmulatorContext* context, const ActionOrigin& origin)
{
    return !origin.session.empty() && context->HasRunControl(unreal::UUID(origin.session));
}
}  // namespace

bool PassGates(const ActionInfo& info, const ActionRules& rules, Emulator& emulator, const ActionValue& value,
               const ActionOrigin& origin, bool open, execution::Requester& requester, ActionResult& result)
{
    using execution::Origin;
    EmulatorContext* context = emulator.GetContext();
    if (context == nullptr)
    {
        result = ActionResult::Failure("Not Found", "the instance is gone");
        return false;
    }
    const uint32_t scope = rules.scope ? rules.scope(emulator, value) : 0;
    const bool changes = scope != 0 && !IsRelease(info, value);
    const EmulatorContext::RunControlState control = context->GetRunControlState();

    // Automation planes: the session the client carries, else the implicit one (automationgate.h)
    if (const char* label = AutomationLabel(origin.origin); *label != '\0')
    {
        if (!origin.session.empty())
        {
            const unreal::UUID session(origin.session);
            if (context->IsRunControlRevoked(session))
            {
                StateNode body = StateNode::Object();
                body["retry"] = false;
                result = ActionResult::Failure("revoked_by_user",
                                               "The user took control back (at the GUI or from a companion); this "
                                               "session has ended. Do not retry, and do not take control again "
                                               "without asking the user.",
                                               std::move(body));
                return false;
            }
            if (context->HasRunControl(session))
            {
                if (open)
                    context->TouchRunControl(session);
                requester = {execution::OriginFromName(control.surfaceLabel), origin.session};
                return true;
            }
        }
        requester = {origin.origin, {}};
        if (!changes)
            return true;
        if (!open)
        {
            // A prediction: free, or (most likely) this client's own implicit session
            if (!control.claimed || (control.implicit && control.surfaceLabel == label && control.client == origin.client))
                return true;
            result = ActionResult::Failure("held_by_other", HolderName(control) + " controls the emulator");
            return false;
        }
        const EmulatorContext::ImplicitControl gate =
            context->EnterImplicitRunControl(label, origin.identity, origin.client, origin.lease, origin.peer);
        if (!gate.allowed)
        {
            StateNode body = StateNode::Object();
            body["retry"] = false;
            body["advice"] = automation_gate::kAdvice;
            result = ActionResult::Failure(gate.error, gate.message, std::move(body));
            return false;
        }
        requester = {execution::OriginFromName(label), gate.owner.toString()};
        result.implicitSession = gate.owner.toString();
        result.implicitOpened = gate.opened;
        return true;
    }

    // In-process scripts: the script gate, as their own control calls (scriptsession.h)
    if (IsScript(origin.origin))
    {
        if (changes)
        {
            if (!open)
            {
                if (execution::RunControl::HeldByOther(context, execution::script::RequesterOf(emulator, origin.origin)))
                {
                    result = ActionResult::Failure("held_by_other", HolderName(control) + " controls the emulator");
                    return false;
                }
            }
            else
            {
                try
                {
                    execution::script::Check(emulator, origin.origin, true);
                }
                catch (const execution::script::ControlRevoked& revoked)
                {
                    result = ActionResult::Failure("revoked_by_user", revoked.what());
                    return false;
                }
                catch (const std::exception& refused)
                {
                    result = ActionResult::Failure("held_by_other", refused.what());
                    return false;
                }
            }
        }
        requester = execution::script::RequesterOf(emulator, origin.origin);
        return true;
    }

    // A companion: its scopes, a holding session, a needed pause (companionregistry.h)
    if (origin.origin == Origin::Companion)
    {
        requester = {Origin::Companion, {}};
        if (!changes)
            return true;
        const std::optional<companion::CompanionInfo> companion =
            companion::CompanionRegistry::Instance().Find(origin.identity);
        if (!companion)
        {
            result = ActionResult::Failure("companion_unknown", "Unknown or revoked companion: pair again");
            return false;
        }
        const companion::ActionCheck check =
            companion::CheckAction(*companion, scope, control.claimed, HolderName(control), "POST /control/take-back",
                                   rules.needsPause, emulator.IsPaused());
        if (check.allowed)
            return true;
        StateNode body = StateNode::Object();
        if (check.error == "companion_forbidden")
            body["required"] = automation_gate::CompanionScopeName(check.required);
        result = ActionResult::Failure(check.error, check.message, std::move(body));
        return false;
    }

    // The GUI and host devices: the user's own hands, no session; refused while another surface holds control
    requester = {origin.origin == Origin::HostDevice ? Origin::HostDevice : origin.origin,
                 origin.origin == Origin::HostDevice ? origin.identity : std::string()};
    if (changes && control.claimed && !CarriesHeldSession(context, origin))
    {
        result = ActionResult::Failure("Run-control held", "Run-control held by " + control.surfaceLabel +
                                                               ". Use that surface, or take control back.");
        return false;
    }
    if (CarriesHeldSession(context, origin))
        requester = {execution::OriginFromName(control.surfaceLabel), origin.session};
    return true;
}

/// endregion </Gates>

/// region <Entries and workers>

struct ActionRegistry::Entry
{
    ActionInfo info;
    Executor run;
    ActionRules rules;
    uint64_t serial = 0;
};

namespace
{
/// Why `entry` cannot run now on `emulator`, before the gates: "" when it can
ActionResult Availability(const ActionRegistry::Entry& entry, Emulator& emulator, const ActionValue& value)
{
    if (IsRelease(entry.info, value))
        return ActionResult::Success();
    const EmulatorStateEnum state = emulator.GetState();
    if (state == StateRetired || emulator.GetContext() == nullptr)
        return ActionResult::Failure("Not Found", "the instance is gone");
    if (entry.rules.unavailable)
        if (std::string why = entry.rules.unavailable(emulator, value); !why.empty())
            return ActionResult::Failure(entry.rules.unavailableError, std::move(why));
    if (entry.rules.needsPause && !emulator.IsPaused())
        return ActionResult::Failure("not_now", "needs a pause");
    return ActionResult::Success();
}
}  // namespace

class ActionRegistry::Worker
{
public:
    struct Job
    {
        std::shared_ptr<const Entry> entry;
        ActionValue value;
        ActionOrigin origin;
        std::vector<Done> done;
    };

    Worker(std::string instanceId, Resolver resolver, ActivityFeed* feed)
        : _instanceId(std::move(instanceId)), _resolver(std::move(resolver)), _feed(feed)
    {
    }

    /// The thread keeps its worker alive: a worker left to finish a long action on its own is never freed under it
    static std::shared_ptr<Worker> Start(std::string instanceId, Resolver resolver, ActivityFeed* feed)
    {
        auto worker = std::make_shared<Worker>(std::move(instanceId), std::move(resolver), feed);
        worker->_thread = std::thread([self = worker] { self->Loop(); });
        return worker;
    }

    ~Worker()
    {
        // The last reference may be the thread's own, released as it ends
        if (_thread.joinable())
        {
            if (_thread.get_id() == std::this_thread::get_id())
                _thread.detach();
            else
                _thread.join();
        }
    }

    /// Queue a job; a stopping worker answers it "Not Found" at once
    void Enqueue(Job job)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_stopping)
        {
            lock.unlock();
            Answer(job, ActionResult::Failure("Not Found", "the instance is gone"));
            return;
        }
        if (job.value.kind == ValueKind::Delta && !_queue.empty())
        {
            Job& last = _queue.back();
            if (last.entry == job.entry && last.value.kind == ValueKind::Delta &&
                last.origin.identity == job.origin.identity && last.origin.session == job.origin.session &&
                last.origin.origin == job.origin.origin && SameNode(last.value.params, job.value.params))
            {
                last.value.delta += job.value.delta;
                last.value.rate = std::max(last.value.rate, job.value.rate);
                for (Done& done : job.done)
                    last.done.push_back(std::move(done));
                _merged++;
                return;
            }
        }
        _queue.push_back(std::move(job));
        _wake.notify_one();
    }

    /// Queued jobs answer "Not Found"; the thread ends after the running job, joined when it does so in 2 s
    void Stop()
    {
        std::deque<Job> left;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stopping = true;
            left.swap(_queue);
        }
        _wake.notify_one();
        for (Job& job : left)
            Answer(job, ActionResult::Failure("Not Found", "the instance is gone"));
        if (!_thread.joinable())
            return;
        if (_thread.get_id() == std::this_thread::get_id())
        {
            _thread.detach();  // an executor removed its own instance: the loop ends after it returns
            return;
        }
        std::unique_lock<std::mutex> lock(_mutex);
        const bool finished = _finishedCv.wait_for(lock, std::chrono::seconds(2), [this] { return _finished; });
        lock.unlock();
        if (finished)
            _thread.join();
        else
        {
            LOGWARNING("ActionRegistry: the actions worker of '%s' still runs an action after 2 s; it ends on its own",
                       _instanceId.c_str());
            _thread.detach();
        }
    }

private:
    static void Answer(Job& job, const ActionResult& result)
    {
        for (Done& done : job.done)
        {
            if (!done)
                continue;
            try
            {
                done(result);
            }
            catch (const std::exception& e)
            {
                LOGWARNING("ActionRegistry: a result callback threw: %s", e.what());
            }
        }
    }

    ActionResult Execute(const Job& job)
    {
        std::shared_ptr<Emulator> emulator = _resolver(_instanceId);
        if (!emulator)
            return ActionResult::Failure("Not Found", "the instance is gone");
        const Entry& entry = *job.entry;
        ActionResult result = Availability(entry, *emulator, job.value);
        if (!result.ok)
            return result;
        execution::Requester requester;
        ActionResult gate;
        if (!PassGates(entry.info, entry.rules, *emulator, job.value, job.origin, true, requester, gate))
            return gate;
        // The operation log names who asked: the source is thread-local
        coordinator::SourceScope source(OpSourceOf(job.origin.origin));
        const ActionCall call{entry.info, emulator, _instanceId, job.value, job.origin, requester};
        try
        {
            result = entry.run ? entry.run(call) : ActionResult::Failure("Not Implemented", "not available yet");
        }
        catch (const std::exception& e)
        {
            result = ActionResult::Failure("Internal", entry.info.id + " failed: " + e.what());
        }
        if (!gate.implicitSession.empty())
        {
            result.implicitSession = gate.implicitSession;
            result.implicitOpened = gate.implicitOpened;
        }
        return result;
    }

    void Loop()
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _wake.wait(lock, [this] { return _stopping || !_queue.empty(); });
                if (_queue.empty())
                    break;  // stopping
                job = std::move(_queue.front());
                _queue.pop_front();
            }
            const ActionResult result = Execute(job);
            // Display only (the activity feed): what ran, with the merged value, before the callers learn it
            if (_feed != nullptr)
                _feed->Record(_instanceId, job.entry->info, job.value, job.origin, result);
            Answer(job, result);
        }
        std::lock_guard<std::mutex> lock(_mutex);
        _finished = true;
        _finishedCv.notify_all();
    }

    const std::string _instanceId;
    const Resolver _resolver;
    ActivityFeed* const _feed;
    std::mutex _mutex;
    std::condition_variable _wake;
    std::condition_variable _finishedCv;
    std::deque<Job> _queue;
    bool _stopping = false;
    bool _finished = false;
    uint64_t _merged = 0;
    std::thread _thread;
};

/// endregion </Entries and workers>

/// region <Registration handle>

RegistrationHandle::RegistrationHandle(RegistrationHandle&& other) noexcept
    : _registry(other._registry), _id(std::move(other._id)), _serial(other._serial)
{
    other._registry = nullptr;
}

RegistrationHandle& RegistrationHandle::operator=(RegistrationHandle&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        _registry = other._registry;
        _id = std::move(other._id);
        _serial = other._serial;
        other._registry = nullptr;
    }
    return *this;
}

RegistrationHandle::~RegistrationHandle()
{
    Reset();
}

void RegistrationHandle::Reset()
{
    if (_registry != nullptr)
        _registry->Unregister(_id, _serial);
    _registry = nullptr;
}

/// endregion </Registration handle>

/// region <Registry>

ActionRegistry& ActionRegistry::Instance()
{
    // Never destroyed: EmulatorManager's destructor (a static too, possibly destroyed later) drops instances here at
    // exit. Its workers are stopped by ShutdownAllEmulators / RemoveEmulator; an idle one dies with the process
    static ActionRegistry* registry = [] {
        auto* created = new ActionRegistry();
        created->_builtin = RegisterBuiltinActions(*created);
        for (RegistrationHandle& handle : RegisterHostActions(*created))
            created->_builtin.push_back(std::move(handle));
        for (RegistrationHandle& handle : RegisterPictureActions(*created))  // the window and the viewport (C9)
            created->_builtin.push_back(std::move(handle));
        for (RegistrationHandle& handle : RegisterSteppingActions(*created))  // run pace, runs to a place, TTD units
            created->_builtin.push_back(std::move(handle));
        for (RegistrationHandle& handle : RegisterDebugToolsActions(*created))  // breakpoints, profilers, state
            created->_builtin.push_back(std::move(handle));
        created->_feed = &ActivityFeed::Instance();
        created->_post = true;  // from now on: the Qt client's GUI actions come and go
        return created;
    }();
    return *registry;
}

ActionRegistry::ActionRegistry(Resolver resolver) : _resolver(std::move(resolver))
{
    if (!_resolver)
        _resolver = [](const std::string& instanceId) -> std::shared_ptr<Emulator> {
            return EmulatorManager::GetInstance()->GetEmulator(instanceId);
        };
}

ActionRegistry::~ActionRegistry()
{
    Shutdown();
    // The core actions' handles point here: drop them before the maps go
    std::vector<RegistrationHandle> builtin;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        builtin.swap(_builtin);
    }
    builtin.clear();
}

namespace
{
void PostCatalogue(uint64_t revision)
{
    MessageCenter::DefaultMessageCenter().Post(NC_ACTION_CATALOGUE, new ActionCatalogueChangedPayload(revision));
}
}  // namespace

RegistrationHandle ActionRegistry::Register(ActionInfo info, Executor run, ActionRules rules)
{
    auto entry = std::make_shared<Entry>();
    entry->info = std::move(info);
    entry->run = std::move(run);
    entry->rules = std::move(rules);
    // The catalogue fields the core knows for its ids (catalogue.h): a description, a toggle's state
    if (entry->info.description.empty())
        entry->info.description = DefaultDescription(entry->info.id);
    if (!entry->rules.state)
        if (const DefaultToggle* toggle = DefaultToggleOf(entry->info.id))
        {
            entry->rules.state = toggle->state;
            if (entry->rules.toggleStem.empty())
                entry->rules.toggleStem = toggle->stem;
            if (entry->rules.toggleParam.empty())
                entry->rules.toggleParam = toggle->param;
        }
    entry->info.toggle = static_cast<bool>(entry->rules.state);
    uint64_t revision = 0;
    bool post = false;
    RegistrationHandle handle;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        entry->serial = _nextSerial++;
        const std::string id = entry->info.id;
        if (_entries.find(id) == _entries.end())
            _order.push_back(id);
        _entries[id] = entry;
        revision = ++_revision;
        post = _post;
        handle = RegistrationHandle(this, id, entry->serial);
    }
    if (post)
        PostCatalogue(revision);
    return handle;
}

void ActionRegistry::Unregister(const std::string& id, uint64_t serial)
{
    uint64_t revision = 0;
    bool post = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _entries.find(id);
        // A newer registration of the same id replaced this one: it stays
        if (it == _entries.end() || it->second->serial != serial)
            return;
        _entries.erase(it);
        _order.erase(std::remove(_order.begin(), _order.end(), id), _order.end());
        revision = ++_revision;
        post = _post;
    }
    if (post)
        PostCatalogue(revision);
}

uint64_t ActionRegistry::Revision() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _revision;
}

std::optional<bool> ActionRegistry::ToggleState(std::string_view id, const std::string& instanceId,
                                                const StateNode& params) const
{
    std::shared_ptr<const Entry> entry = Lookup(id);
    if (!entry || !entry->rules.state)
        return std::nullopt;
    std::shared_ptr<Emulator> emulator = Resolve(instanceId);
    if (!emulator || emulator->GetContext() == nullptr || emulator->GetState() == StateRetired)
        return std::nullopt;
    ActionValue value;
    value.kind = entry->info.kind;
    value.params = params;
    try
    {
        return entry->rules.state(*emulator, value);
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

std::vector<ActionRegistry::ToggleReadout> ActionRegistry::ToggleReadouts() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<ToggleReadout> out;
    for (const std::string& id : _order)
    {
        const Entry& entry = *_entries.find(id)->second;
        if (!entry.rules.state)
            continue;
        const std::string stem = entry.rules.toggleStem.empty() ? id : entry.rules.toggleStem;
        out.push_back({"toggle." + stem, id, entry.rules.toggleParam});
    }
    return out;
}

void ActionRegistry::SetActivityFeed(ActivityFeed* feed)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _feed = feed;
}

std::shared_ptr<const ActionRegistry::Entry> ActionRegistry::Lookup(std::string_view id) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _entries.find(id);
    return it != _entries.end() ? it->second : nullptr;
}

std::shared_ptr<Emulator> ActionRegistry::Resolve(const std::string& instanceId) const
{
    return instanceId.empty() ? nullptr : _resolver(instanceId);
}

std::vector<ActionInfo> ActionRegistry::List() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<ActionInfo> out;
    out.reserve(_order.size());
    for (const std::string& id : _order)
        out.push_back(_entries.find(id)->second->info);
    return out;
}

std::optional<ActionInfo> ActionRegistry::Find(std::string_view id) const
{
    std::shared_ptr<const Entry> entry = Lookup(id);
    if (!entry)
        return std::nullopt;
    return entry->info;
}

std::vector<ActionStatus> ActionRegistry::Statuses(const std::string& instanceId, const ActionOrigin& origin) const
{
    std::vector<std::shared_ptr<const Entry>> entries;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const std::string& id : _order)
            entries.push_back(_entries.find(id)->second);
    }
    std::shared_ptr<Emulator> emulator = Resolve(instanceId);
    std::vector<ActionStatus> out;
    out.reserve(entries.size());
    for (const std::shared_ptr<const Entry>& entry : entries)
    {
        ActionStatus status;
        status.info = entry->info;
        status.needsPause = entry->rules.needsPause;
        ActionValue value;
        value.kind = entry->info.kind;
        if (value.kind == ValueKind::Delta)
            value.delta = 1;
        if (value.kind == ValueKind::Level)
            value.level = 1;
        if (!emulator)
        {
            status.error = "Not Found";
            status.reason = "no machine";
            out.push_back(std::move(status));
            continue;
        }
        status.scope = entry->rules.scope ? entry->rules.scope(*emulator, value) : 0;
        ActionResult result = Availability(*entry, *emulator, value);
        execution::Requester requester;
        if (result.ok)
            PassGates(entry->info, entry->rules, *emulator, value, origin, false, requester, result);
        status.available = result.ok;
        status.error = result.ok ? std::string() : result.error;
        status.reason = result.ok ? std::string() : result.message;
        out.push_back(std::move(status));
    }
    return out;
}

std::string ActionRegistry::Unavailable(std::string_view id, const std::string& instanceId, const ActionOrigin& origin,
                                        const ActionValue& value) const
{
    std::shared_ptr<const Entry> entry = Lookup(id);
    if (!entry)
        return "unknown action";
    std::shared_ptr<Emulator> emulator = Resolve(instanceId);
    if (!emulator)
        return "no machine";
    ActionResult result = Availability(*entry, *emulator, value);
    execution::Requester requester;
    if (result.ok)
        PassGates(entry->info, entry->rules, *emulator, value, origin, false, requester, result);
    return result.ok ? std::string() : result.message;
}

void ActionRegistry::Submit(std::string_view id, ActionValue value, const std::string& instanceId, ActionOrigin origin,
                            Done done)
{
    auto answer = [&done](ActionResult result) {
        if (done)
            done(std::move(result));
    };
    std::shared_ptr<const Entry> entry = Lookup(id);
    if (!entry)
    {
        answer(ActionResult::Failure("Not Found", "unknown action '" + std::string(id) + "'"));
        return;
    }
    if (value.kind != entry->info.kind)
    {
        answer(ActionResult::Failure("Bad Request", entry->info.id + " takes a " + ValueKindName(entry->info.kind) +
                                                        " value, not a " + ValueKindName(value.kind)));
        return;
    }
    if (entry->info.kind == ValueKind::Press && !value.pressed && !entry->info.held)
    {
        StateNode body = StateNode::Object();
        body["ignored"] = "release";
        answer(ActionResult::Success(std::move(body)));  // only a held action sees its release
        return;
    }
    if (!Resolve(instanceId))
    {
        answer(ActionResult::Failure("Not Found", instanceId.empty() ? "no machine" : "no instance '" + instanceId + "'"));
        return;
    }
    std::shared_ptr<Worker> worker;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_shutDown)
        {
            answer(ActionResult::Failure("Not Found", "the emulator is shutting down"));
            return;
        }
        std::shared_ptr<Worker>& slot = _workers[instanceId];
        if (!slot)
            slot = Worker::Start(instanceId, _resolver, _feed);
        worker = slot;
    }
    Worker::Job job{entry, std::move(value), std::move(origin), {}};
    job.done.push_back(std::move(done));
    worker->Enqueue(std::move(job));
}

ActionResult ActionRegistry::Run(std::string_view id, ActionValue value, const std::string& instanceId,
                                 ActionOrigin origin, std::chrono::milliseconds timeout)
{
    auto promise = std::make_shared<std::promise<ActionResult>>();
    std::future<ActionResult> future = promise->get_future();
    Submit(id, std::move(value), instanceId, std::move(origin),
           [promise](ActionResult result) { promise->set_value(std::move(result)); });
    if (future.wait_for(timeout) != std::future_status::ready)
        return ActionResult::Failure("Timeout", "the action did not finish in " + std::to_string(timeout.count()) +
                                                    " ms; it goes on");
    return future.get();
}

void ActionRegistry::DropInstance(const std::string& instanceId)
{
    std::shared_ptr<Worker> worker;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _workers.find(instanceId);
        if (it == _workers.end())
            return;
        worker = std::move(it->second);
        _workers.erase(it);
    }
    worker->Stop();
}

void ActionRegistry::Shutdown()
{
    std::map<std::string, std::shared_ptr<Worker>> workers;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _shutDown = true;
        workers.swap(_workers);
    }
    for (auto& [id, worker] : workers)
        worker->Stop();
}

size_t ActionRegistry::WorkerCount() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _workers.size();
}

/// endregion </Registry>

}  // namespace actions
