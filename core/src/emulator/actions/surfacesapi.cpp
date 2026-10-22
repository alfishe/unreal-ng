#include "emulator/actions/surfacesapi.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>

#include "emulator/actions/activityfeed.h"
#include "emulator/actions/readouts.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"

namespace actions
{
namespace surfaces
{

namespace
{
constexpr const char* kApiOwner = "api";

Reply Fail(int status, const std::string& error, const std::string& message)
{
    Reply reply;
    reply.status = status;
    reply.body["ok"] = false;
    reply.body["error"] = error;
    reply.body["message"] = message;
    return reply;
}

Reply Bad(const std::string& message)
{
    return Fail(400, "Bad Request", message);
}

Reply NotFound(const std::string& message)
{
    return Fail(404, "Not Found", message);
}

Reply Success(StateNode body = StateNode::Object())
{
    Reply reply;
    StateNode out = StateNode::Object();
    out["ok"] = true;
    for (auto& member : body.members)
        out[member.first] = std::move(member.second);
    reply.body = std::move(out);
    return reply;
}

/// A text arg ("" when absent); numbers given as text are accepted elsewhere
std::string Text(const StateNode& args, const char* name)
{
    const StateNode* node = args.find(name);
    if (node == nullptr)
        return {};
    switch (node->kind)
    {
        case StateNode::Kind::String: return node->s;
        case StateNode::Kind::Int: return std::to_string(node->i);
        case StateNode::Kind::Bool: return node->b ? "true" : "false";
        default: return {};
    }
}

bool Has(const StateNode& args, const char* name)
{
    const StateNode* node = args.find(name);
    return node != nullptr && node->kind != StateNode::Kind::Null;
}

/// A bool arg (a Bool, an Int, or "true" / "false" / "1" / "0" / "yes" / "no")
bool Flag(const StateNode& args, const char* name, bool fallback)
{
    const StateNode* node = args.find(name);
    if (node == nullptr)
        return fallback;
    if (node->kind == StateNode::Kind::Bool)
        return node->b;
    if (node->kind == StateNode::Kind::Int)
        return node->i != 0;
    if (node->kind == StateNode::Kind::String)
        return node->s == "true" || node->s == "1" || node->s == "yes" || node->s == "on";
    return fallback;
}

/// An unsigned arg; false when present and not a number
bool Number(const StateNode& args, const char* name, uint64_t& out)
{
    const StateNode* node = args.find(name);
    if (node == nullptr || node->kind == StateNode::Kind::Null)
        return true;
    if (node->kind == StateNode::Kind::Int && node->i >= 0)
    {
        out = static_cast<uint64_t>(node->i);
        return true;
    }
    if (node->kind == StateNode::Kind::Double && node->d >= 0)
    {
        out = static_cast<uint64_t>(node->d);
        return true;
    }
    if (node->kind == StateNode::Kind::String && !node->s.empty())
    {
        char* end = nullptr;
        const unsigned long long value = std::strtoull(node->s.c_str(), &end, 10);
        if (end != nullptr && *end == '\0')
        {
            out = value;
            return true;
        }
    }
    return false;
}

/// An instance id, or an index as GET /api/v1/emulator lists them; "" when none matches
std::string InstanceIdOf(const std::string& named)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    if (named.empty())
        return {};
    if (std::all_of(named.begin(), named.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }) &&
        named.size() < 6)
    {
        const std::vector<std::string> ids = manager->GetEmulatorIds();
        const size_t index = static_cast<size_t>(std::atoi(named.c_str()));
        return index < ids.size() ? ids[index] : std::string();
    }
    return manager->GetEmulator(named) ? named : std::string();
}

/// Views and sessions registered over a plane: they live until removed there
struct Owned
{
    std::mutex mutex;
    std::map<std::string, TargetHandle> views;
    std::map<std::string, TargetHandle> sessions;
};

Owned& OwnedHandles()
{
    static Owned* owned = new Owned();  // never destroyed, as TargetState
    return *owned;
}

StateNode ViewNode(const ViewInfo& view, size_t position, const std::vector<std::string>& ids)
{
    StateNode node = StateNode::Object();
    node["id"] = view.id;
    node["name"] = view.name;
    node["owner"] = view.owner;
    node["position"] = static_cast<int64_t>(position);
    if (view.activeInstance.empty())
        node["active"] = StateNode();
    else
        node["active"] = view.activeInstance;
    const auto at = std::find(ids.begin(), ids.end(), view.activeInstance);
    if (at != ids.end())
        node["index"] = static_cast<int64_t>(at - ids.begin());
    else
        node["index"] = StateNode();
    return node;
}

StateNode SessionNode(const SurfaceSessionInfo& session)
{
    StateNode node = StateNode::Object();
    node["id"] = session.id;
    node["name"] = session.spec.name;
    node["kind"] = session.spec.kind;
    if (!session.spec.serial.empty())
        node["serial"] = session.spec.serial;
    node["pin"] = session.pin.ToText();
    node["target"] = TargetNode(session.target);
    if (!session.notice.empty())
        node["notice"] = session.notice;
    return node;
}

Reply Views()
{
    TargetState& state = TargetState::Instance();
    const std::vector<std::string> ids = EmulatorManager::GetInstance()->GetEmulatorIds();
    StateNode body = StateNode::Object();
    body["followed"] = TargetNode(state.FollowedTarget());
    StateNode& views = body["views"];
    views = StateNode::Array();
    size_t position = 0;
    for (const ViewInfo& view : state.Views())
        views.push(ViewNode(view, position++, ids));
    body["revision"] = static_cast<int64_t>(state.Revision());
    return Success(std::move(body));
}

Reply ViewAdd(const StateNode& args)
{
    std::string active;
    if (!Text(args, "active").empty())
    {
        active = InstanceIdOf(Text(args, "active"));
        if (active.empty())
            return NotFound("no instance '" + Text(args, "active") + "'");
    }
    TargetState& state = TargetState::Instance();
    TargetHandle handle = state.RegisterView(Text(args, "name"), kApiOwner);
    const std::string id = handle.Id();
    if (!active.empty())
        state.SetActive(handle, active);
    if (Flag(args, "activate", false))
        state.Activated(handle);
    {
        Owned& owned = OwnedHandles();
        std::lock_guard<std::mutex> lock(owned.mutex);
        owned.views[id] = std::move(handle);
    }
    const std::vector<std::string> ids = EmulatorManager::GetInstance()->GetEmulatorIds();
    StateNode body = StateNode::Object();
    body["view"] = ViewNode(*state.View(id), 0, ids);
    body["followed"] = TargetNode(state.FollowedTarget());
    return Success(std::move(body));
}

Reply ViewUpdate(const StateNode& args)
{
    const std::string id = Text(args, "view");
    TargetState& state = TargetState::Instance();
    const std::optional<ViewInfo> view = state.View(id);
    if (!view)
        return NotFound("no view '" + id + "'");
    if (Has(args, "active"))
    {
        if (view->owner != kApiOwner)
            return Fail(409, "Conflict",
                        "the view '" + view->name + "' belongs to " + view->owner + ": its active instance is its own");
        const std::string named = Text(args, "active");
        const std::string active = named.empty() ? std::string() : InstanceIdOf(named);
        if (!named.empty() && active.empty())
            return NotFound("no instance '" + named + "'");
        state.SetActiveById(id, active);
    }
    if (Flag(args, "activate", false))
        state.ActivatedById(id);
    const std::vector<std::string> ids = EmulatorManager::GetInstance()->GetEmulatorIds();
    StateNode body = StateNode::Object();
    if (const std::optional<ViewInfo> now = state.View(id))
        body["view"] = ViewNode(*now, 0, ids);
    body["followed"] = TargetNode(state.FollowedTarget());
    return Success(std::move(body));
}

Reply ViewRemove(const StateNode& args)
{
    const std::string id = Text(args, "view");
    const std::optional<ViewInfo> view = TargetState::Instance().View(id);
    if (!view)
        return NotFound("no view '" + id + "'");
    TargetHandle handle;
    {
        Owned& owned = OwnedHandles();
        std::lock_guard<std::mutex> lock(owned.mutex);
        auto it = owned.views.find(id);
        if (it == owned.views.end())
            return Fail(409, "Conflict", "the view '" + view->name + "' belongs to " + view->owner);
        handle = std::move(it->second);
        owned.views.erase(it);
    }
    handle.Reset();
    StateNode body = StateNode::Object();
    body["removed"] = id;
    body["followed"] = TargetNode(TargetState::Instance().FollowedTarget());
    return Success(std::move(body));
}

Reply Target(const StateNode& args)
{
    TargetState& state = TargetState::Instance();
    const std::string session = Text(args, "session");
    StateNode body = StateNode::Object();
    if (session.empty())
    {
        body["target"] = TargetNode(state.FollowedTarget());
        return Success(std::move(body));
    }
    const std::optional<TargetInfo> target = state.TargetFor(session);
    if (!target)
        return NotFound("no surface session '" + session + "'");
    body["session"] = session;
    body["target"] = TargetNode(*target);
    return Success(std::move(body));
}

Reply Sessions()
{
    StateNode body = StateNode::Object();
    StateNode& sessions = body["sessions"];
    sessions = StateNode::Array();
    for (const SurfaceSessionInfo& session : TargetState::Instance().Sessions())
        sessions.push(SessionNode(session));
    return Success(std::move(body));
}

Reply Session(const StateNode& args)
{
    const std::string id = Text(args, "session");
    const std::optional<SurfaceSessionInfo> session = TargetState::Instance().Session(id);
    if (!session)
        return NotFound("no surface session '" + id + "'");
    StateNode body = StateNode::Object();
    body["session"] = SessionNode(*session);
    return Success(std::move(body));
}

Reply SessionOpen(const StateNode& args)
{
    const std::string name = Text(args, "name");
    if (name.empty())
        return Bad("name: the surface's name (\"Stream Deck + XL\", \"my script\")");
    Pin pin;
    if (const StateNode* target = args.find("target"); target != nullptr && target->kind != StateNode::Kind::Null)
    {
        std::string error;
        if (!ParseTarget(*target, pin, error))
            return error.rfind("no ", 0) == 0 ? NotFound(error) : Bad(error);
    }
    SurfaceSessionSpec spec;
    spec.name = name;
    spec.kind = Text(args, "kind").empty() ? kApiOwner : Text(args, "kind");
    spec.serial = Text(args, "serial");
    TargetState& state = TargetState::Instance();
    TargetHandle handle = state.OpenSession(spec);
    const std::string id = handle.Id();
    {
        Owned& owned = OwnedHandles();
        std::lock_guard<std::mutex> lock(owned.mutex);
        owned.sessions[id] = std::move(handle);
    }
    if (pin.kind != Pin::Kind::Follow)
    {
        std::string error;
        std::string message;
        if (!state.SetPin(id, pin, error, message))
        {
            TargetHandle drop;
            {
                Owned& owned = OwnedHandles();
                std::lock_guard<std::mutex> lock(owned.mutex);
                drop = std::move(owned.sessions[id]);
                owned.sessions.erase(id);
            }
            return Fail(error == "Not Found" ? 404 : 409, error, message);
        }
    }
    StateNode body = StateNode::Object();
    body["session"] = SessionNode(*state.Session(id));
    return Success(std::move(body));
}

Reply SessionClose(const StateNode& args)
{
    const std::string id = Text(args, "session");
    if (!TargetState::Instance().Session(id))
        return NotFound("no surface session '" + id + "'");
    TargetHandle handle;
    {
        Owned& owned = OwnedHandles();
        std::lock_guard<std::mutex> lock(owned.mutex);
        auto it = owned.sessions.find(id);
        if (it == owned.sessions.end())
            return Fail(409, "Conflict", "the session '" + id + "' belongs to its surface (it closes it itself)");
        handle = std::move(it->second);
        owned.sessions.erase(it);
    }
    handle.Reset();
    StateNode body = StateNode::Object();
    body["closed"] = id;
    return Success(std::move(body));
}

Reply PinVerb(const StateNode& args)
{
    const std::string id = Text(args, "session");
    if (id.empty())
        return Bad("session: the surface session to pin (sessions lists them)");
    const StateNode* target = args.find("target");
    Pin pin;
    std::string error;
    if (target != nullptr && !ParseTarget(*target, pin, error))
        return error.rfind("no ", 0) == 0 ? NotFound(error) : Bad(error);
    std::string message;
    TargetState& state = TargetState::Instance();
    if (!state.SetPin(id, pin, error, message))
        return Fail(error == "Not Found" ? 404 : 409, error, message);
    StateNode body = StateNode::Object();
    body["session"] = SessionNode(*state.Session(id));
    return Success(std::move(body));
}

Reply ReadoutsVerb(const StateNode& args)
{
    std::string instance;
    const std::string named = Text(args, "instance");
    if (!named.empty())
    {
        instance = InstanceIdOf(named);
        if (instance.empty())
            return NotFound("no instance '" + named + "'");
    }
    else
    {
        std::string error;
        instance = DefaultInstance(error);
        if (instance.empty())
            return error == "no machine" ? NotFound(error) : Bad(error);
    }
    std::vector<std::string> patterns;
    if (const StateNode* names = args.find("names"); names != nullptr)
    {
        if (names->kind == StateNode::Kind::Array)
        {
            for (const StateNode& item : names->items)
                if (item.kind == StateNode::Kind::String)
                    patterns.push_back(item.s);
        }
        else if (names->kind == StateNode::Kind::String)
        {
            std::string word;
            for (char c : names->s + ",")
            {
                if (c == ',' || c == ' ')
                {
                    if (!word.empty())
                        patterns.push_back(word);
                    word.clear();
                }
                else
                    word += c;
            }
        }
    }
    const ReadoutSet set = ReadReadouts(instance, patterns);
    StateNode body = StateNode::Object();
    body["instance"] = instance;
    body["values"] = set.values;
    StateNode& unknown = body["unknown"];
    unknown = StateNode::Array();
    for (const std::string& name : set.unknown)
        unknown.push(name);
    return Success(std::move(body));
}

Reply ActivityVerb(const StateNode& args)
{
    uint64_t since = 0;
    uint64_t limit = 64;
    if (!Number(args, "since", since))
        return Bad("since: a sequence number (the `last` of the previous answer)");
    if (!Number(args, "limit", limit))
        return Bad("limit: a number");
    std::string instance;
    const std::string named = Text(args, "instance");
    if (!named.empty())
    {
        instance = InstanceIdOf(named);
        if (instance.empty())
            return NotFound("no instance '" + named + "'");
    }
    const ActivityFeed::Page page =
        ActivityFeed::Instance().Since(since, instance, static_cast<size_t>(std::clamp<uint64_t>(limit, 1, 256)));
    StateNode body = StateNode::Object();
    StateNode& entries = body["entries"];
    entries = StateNode::Array();
    for (const ActivityEntry& entry : page.entries)
        entries.push(ActivityEntryNode(entry));
    body["last"] = static_cast<int64_t>(page.last);
    body["dropped"] = static_cast<int64_t>(page.dropped);
    return Success(std::move(body));
}
}  // namespace

const std::vector<std::string>& Verbs()
{
    static const std::vector<std::string> verbs = {"views",    "view_add",      "view_update",  "view_remove",
                                                   "target",   "sessions",      "session",      "session_open",
                                                   "session_close", "pin",      "readouts",     "activity"};
    return verbs;
}

Reply Call(std::string_view verb, const StateNode& raw)
{
    const StateNode args = raw.kind == StateNode::Kind::Object ? raw : StateNode::Object();
    if (verb == "views")
        return Views();
    if (verb == "view_add")
        return ViewAdd(args);
    if (verb == "view_update")
        return ViewUpdate(args);
    if (verb == "view_remove")
        return ViewRemove(args);
    if (verb == "target")
        return Target(args);
    if (verb == "sessions")
        return Sessions();
    if (verb == "session")
        return Session(args);
    if (verb == "session_open")
        return SessionOpen(args);
    if (verb == "session_close")
        return SessionClose(args);
    if (verb == "pin")
        return PinVerb(args);
    if (verb == "readouts")
        return ReadoutsVerb(args);
    if (verb == "activity")
        return ActivityVerb(args);
    std::string list;
    for (const std::string& name : Verbs())
        list += (list.empty() ? "" : ", ") + name;
    return Bad("unknown surface verb '" + std::string(verb) + "': " + list);
}

StateNode TargetNode(const TargetInfo& target)
{
    StateNode node = StateNode::Object();
    std::shared_ptr<Emulator> emulator =
        target.instance.empty() ? nullptr : EmulatorManager::GetInstance()->GetEmulator(target.instance);
    if (!emulator)
    {
        node["instance"] = StateNode();
        node["index"] = StateNode();
        node["model"] = StateNode();
        node["ram_kb"] = StateNode();
    }
    else
    {
        node["instance"] = target.instance;
        const std::vector<std::string> ids = EmulatorManager::GetInstance()->GetEmulatorIds();
        const auto at = std::find(ids.begin(), ids.end(), target.instance);
        if (at != ids.end())
            node["index"] = static_cast<int64_t>(at - ids.begin());
        else
            node["index"] = StateNode();
        const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);
        if (identity.Valid)
        {
            node["model"] = identity.Model;
            node["ram_kb"] = static_cast<int64_t>(identity.RamKb);
        }
        else
        {
            node["model"] = StateNode();
            node["ram_kb"] = StateNode();
        }
    }
    if (target.view.empty())
        node["view"] = StateNode();
    else
    {
        StateNode view = StateNode::Object();
        view["id"] = target.view;
        view["name"] = target.viewName;
        node["view"] = std::move(view);
    }
    node["mode"] = target.pin.ModeName();
    node["pin"] = target.pin.ToText();
    node["source"] = FollowSourceName(target.source);
    return node;
}

std::string DefaultInstance(std::string& error)
{
    const TargetInfo target = TargetState::Instance().FollowedTarget();
    if (!target.instance.empty() && EmulatorManager::GetInstance()->GetEmulator(target.instance))
        return target.instance;
    const std::vector<std::string> ids = EmulatorManager::GetInstance()->GetEmulatorIds();
    error = ids.empty() ? "no machine" : "several instances: name one (instance)";
    return {};
}

bool ParseTarget(const StateNode& node, Pin& pin, std::string& error)
{
    Pin parsed;
    if (node.kind == StateNode::Kind::String)
    {
        if (!Pin::FromText(node.s, parsed, error))
            return false;
    }
    else if (node.kind == StateNode::Kind::Object)
    {
        if (const StateNode* view = node.find("view"); view != nullptr && view->kind == StateNode::Kind::String)
            parsed = Pin::OnView(view->s);
        else if (const StateNode* instance = node.find("instance"); instance != nullptr)
        {
            if (instance->kind == StateNode::Kind::String)
                parsed = Pin::OnInstance(instance->s);
            else if (instance->kind == StateNode::Kind::Int)
                parsed = Pin::OnInstance(std::to_string(instance->i));
            else
            {
                error = "target: {\"instance\": <id|index>}";
                return false;
            }
        }
        else
        {
            error = "target: \"follow\", {\"view\": <id>} or {\"instance\": <id|index>}";
            return false;
        }
    }
    else if (node.kind == StateNode::Kind::Null)
        parsed = Pin::Follow();
    else
    {
        error = "target: \"follow\", \"view:<id>\", \"instance:<id>\" or an object";
        return false;
    }
    if (parsed.kind == Pin::Kind::Instance)
    {
        const std::string id = InstanceIdOf(parsed.id);
        if (id.empty())
        {
            error = "no instance '" + parsed.id + "'";
            return false;
        }
        parsed.id = id;
    }
    pin = std::move(parsed);
    return true;
}

}  // namespace surfaces
}  // namespace actions
