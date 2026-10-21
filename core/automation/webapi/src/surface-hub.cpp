#include "surface-hub.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "common/statenode_json.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/contextstate.h"
#include "emulator/actions/readouts.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/automationgate.h"
#include "ws-hub.h"

namespace
{
constexpr const char* kTopic = "surface";

bool AllDigits(const std::string& text)
{
    auto digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    return !text.empty() && std::all_of(text.begin(), text.end(), digit);
}

/// An instance by id or index ("" when none)
std::string InstanceNamed(const std::string& named)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->GetEmulator(named);
    if (!emulator && AllDigits(named) && named.size() < 6)
        emulator = manager->GetEmulatorByIndex(std::stoi(named));
    return emulator ? emulator->GetId() : std::string();
}

bool Finite(const Json::Value& value, double& out)
{
    if (!value.isNumeric())
        return false;
    out = value.asDouble();
    return std::isfinite(out);
}

/// The protocol's value by the action's kind (protocol.md §3.1): `{"p": true/false}`, `{"d": n, "rate": r, …params}`,
/// `{"l": x}` or a string; the REST spellings (`pressed`, `delta`, `level`, `text`, `params`) and bare values (a
/// bool, a number) as well. Any other member of the object is a parameter (`unit`, `min`, `max` of ttd.seek_by)
bool SurfaceValue(const Json::Value& raw, actions::ValueKind kind, actions::ActionValue& value, std::string& error)
{
    using actions::ValueKind;
    value = actions::ActionValue{};
    value.kind = kind;
    const bool object = raw.isObject();
    auto member = [&](const char* shortName, const char* longName) -> const Json::Value& {
        if (!object)
            return Json::Value::nullSingleton();
        if (raw.isMember(shortName))
            return raw[shortName];
        return raw[longName];
    };
    switch (kind)
    {
        case ValueKind::Press:
        {
            const Json::Value& p = object ? member("p", "pressed") : raw;
            if (p.isBool())
                value.pressed = p.asBool();
            else if (p.isNumeric())
                value.pressed = p.asDouble() != 0;
            else if (!p.isNull())
            {
                error = "value: a press takes true / false or {\"p\": true}";
                return false;
            }
            break;
        }
        case ValueKind::Delta:
            if (!Finite(object ? member("d", "delta") : raw, value.delta))
            {
                error = "value: a delta takes a number or {\"d\": n, \"rate\": r}";
                return false;
            }
            if (object && raw.isMember("rate") && !Finite(raw["rate"], value.rate))
            {
                error = "value: rate, a number (steps per second)";
                return false;
            }
            break;
        case ValueKind::Level:
            if (!Finite(object ? member("l", "level") : raw, value.level))
            {
                error = "value: a level takes a number 0..1 or {\"l\": x}";
                return false;
            }
            value.level = std::max(0.0, std::min(1.0, value.level));
            value.pressed = value.level > 0;
            break;
        case ValueKind::Text:
        {
            const Json::Value& text = object ? member("text", "text") : raw;
            if (!text.isString())
            {
                error = "value: a text action takes a string";
                return false;
            }
            value.text = text.asString();
            break;
        }
    }
    if (object)
    {
        static const char* const kReserved[] = {"p", "pressed", "d", "delta", "rate", "l", "level", "text", "params"};
        StateNode params = StateNode::Object();
        if (raw.isMember("params"))
        {
            if (!raw["params"].isObject())
            {
                error = "value: params, an object";
                return false;
            }
            params = JsonToStateNode(raw["params"]);
        }
        for (const std::string& name : raw.getMemberNames())
            if (std::none_of(std::begin(kReserved), std::end(kReserved), [&](const char* r) { return name == r; }))
                params[name] = JsonToStateNode(raw[name]);
        value.params = std::move(params);
    }
    return true;
}

Json::Value Nullable(const std::string& text)
{
    return text.empty() ? Json::Value(Json::nullValue) : Json::Value(text);
}

/// A message of one surface: its op, the topic and the session
Json::Value SessionMessage(const char* op, const SurfaceSession& session)
{
    Json::Value message;
    message["op"] = op;
    message["t"] = kTopic;
    message["session"] = session.session;
    return message;
}

bool IsPattern(const std::string& name)
{
    return name == "*" || (name.size() >= 2 && name.compare(name.size() - 2, 2, ".*") == 0);
}
}  // namespace

/// region <Ops>

SurfaceSession* SurfaceHub::SessionOf(WsConnection& connection, const Json::Value& message, WsOutbox& out,
                                      const Json::Value& rid)
{
    if (connection.surfaces.empty())
    {
        out.Error(connection, kTopic, "bad_request", "join first: {\"op\":\"join\",\"v\":1,\"name\":…,\"model\":…}",
                  rid);
        return nullptr;
    }
    const Json::Value& named = message["session"];
    if (named.isString())
    {
        auto it = connection.surfaces.find(named.asString());
        if (it != connection.surfaces.end())
            return it->second.get();
        out.Error(connection, kTopic, "Not Found", "no surface session '" + named.asString() + "' on this connection",
                  rid);
        return nullptr;
    }
    if (!named.isNull())
    {
        out.Error(connection, kTopic, "bad_request", "session: the id its welcome named (a string)", rid);
        return nullptr;
    }
    if (connection.surfaces.size() == 1)
        return connection.surfaces.begin()->second.get();
    out.Error(connection, kTopic, "bad_request",
              "this connection joined " + std::to_string(connection.surfaces.size()) +
                  " surfaces: name one by \"session\"",
              rid);
    return nullptr;
}

void SurfaceHub::Join(WsConnection& connection, const Json::Value& message, WsOutbox& out)
{
    const Json::Value rid = message.get("rid", Json::Value());
    const std::string serial = message.get("serial", "").isString() ? message.get("serial", "").asString() : "";
    for (const auto& [id, joined] : connection.surfaces)
        if (joined->serial == serial)
        {
            out.Error(connection, kTopic, "bad_request",
                      serial.empty() ? std::string("this connection joined already: a further surface needs a serial "
                                                   "of its own")
                                     : "serial '" + serial + "' joined already on this connection (" + id + ")",
                      rid);
            return;
        }
    if (!_hub.Authenticated(connection, kTopic, out))
        return;
    const Json::Value& v = message["v"];
    const Json::Value& name = message["name"];
    const Json::Value& model = message["model"];
    const Json::Value& features = message["features"];
    if (!v.isIntegral() || v.asInt64() < 1 || !name.isString() || name.asString().empty() || !model.isString() ||
        model.asString().empty() || (message.isMember("serial") && !message["serial"].isString()) ||
        (!features.isNull() && !features.isArray()))
    {
        out.Error(connection, kTopic, "bad_request",
                  "join: v (the highest protocol version the client speaks, 1), name and model (strings), serial? "
                  "(a string), target? (\"follow\" or {\"instance\": id}), features? ([\"activity\", "
                  "\"catalogue\"])",
                  rid);
        return;
    }

    auto session = std::make_unique<SurfaceSession>();
    const Json::Value& target = message["target"];
    if (target.isObject() && target.isMember("view"))
    {
        out.Error(connection, kTopic, "not_now",
                  "views come with the Stream Deck targeting (TargetState, SD4): join with \"follow\" or "
                  "{\"instance\": id}",
                  rid);
        return;
    }
    if (target.isObject() && (target["instance"].isString() || target["instance"].isIntegral()))
    {
        const Json::Value& pin = target["instance"];
        const std::string named = pin.isString() ? pin.asString() : std::to_string(pin.asInt64());
        session->instance = InstanceNamed(named);
        session->pinned = true;
        if (session->instance.empty())
        {
            out.Error(connection, kTopic, "Not Found", "no instance '" + named + "'", rid);
            return;
        }
    }
    else if (!target.isNull() && !(target.isString() && target.asString() == "follow"))
    {
        out.Error(connection, kTopic, "bad_request", "target: \"follow\" or {\"instance\": id or index}", rid);
        return;
    }
    else
        session->instance = WsHub::FollowedInstance();

    session->session = "s" + std::to_string(_nextSession++);
    session->v = static_cast<int>(std::min<int64_t>(v.asInt64(), kVersion));
    session->name = name.asString();
    session->model = model.asString();
    session->serial = serial;
    Json::Value accepted(Json::arrayValue);
    for (const Json::Value& feature : features)
    {
        const std::string text = feature.isString() ? feature.asString() : std::string();
        if (text == "activity")
            session->activity = true;
        else if (text == "catalogue")
            session->catalogue = true;
        else if (text != "target")  // `target` is always pushed
            continue;
        accepted.append(text);
    }
    if (!session->instance.empty())
    {
        std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(session->instance);
        session->context = actions::ContextState::Instance().Get(session->instance, emulator.get()).context;
    }

    Json::Value welcome = SessionMessage("welcome", *session);
    if (!rid.isNull())
        welcome["rid"] = rid;
    welcome["v"] = session->v;
    welcome["serial"] = Nullable(session->serial);
    welcome["instance"] = Nullable(session->instance);
    welcome["context"] = Nullable(session->context);
    welcome["scopes"] = Json::Value(Json::arrayValue);
    for (const std::string& scope : automation_gate::CompanionScopeNames(connection.companion->scopes))
        welcome["scopes"].append(scope);
    welcome["target"] = TargetOf(*session);
    welcome["readouts"] = Json::Value(Json::arrayValue);
    for (const std::string& readout : ReadoutNamesOf(session->instance))
        welcome["readouts"].append(readout);
    welcome["features"] = accepted;
    welcome["catalogue_rev"] = static_cast<Json::UInt64>(_hub.Registry().Revision());
    const std::string id = session->session;
    connection.surfaces[id] = std::move(session);
    out.Message(connection, welcome);
}

void SurfaceHub::Action(WsConnection& connection, const Json::Value& message, WsOutbox& out)
{
    const Json::Value rid = message.get("rid", Json::Value());
    SurfaceSession* session = SessionOf(connection, message, out, rid);
    if (session == nullptr)
        return;
    const std::string id = message.get("id", "").asString();
    if (id.empty())
    {
        out.Error(connection, kTopic, "bad_request", "action: id, an action id (GET /api/v1/controllers/actions)", rid);
        return;
    }
    actions::ActionRegistry& registry = _hub.Registry();
    const std::optional<actions::ActionInfo> info = registry.Find(id);
    const std::string instance = session->instance;
    const std::string sessionId = session->session;
    actions::ActionValue value;
    std::string error;
    std::optional<actions::ActionResult> refused;
    if (!info)
        refused = actions::ActionResult::Failure("Not Found", "unknown action '" + id + "'");
    else if (!SurfaceValue(message["value"], info->kind, value, error))
    {
        out.Error(connection, kTopic, "bad_request", error, rid);
        return;
    }
    else if (instance.empty())
        refused = actions::ActionResult::Failure("Not Found", "no machine: the surface is bound to none");
    if (refused)
    {
        // Answered as the worker would, through the loop like any result
        const uint64_t connectionId = connection.id;
        out.after.push_back([this, connectionId, sessionId, rid, result = *refused] {
            DeliverResult(connectionId, sessionId, rid, result);
        });
        return;
    }

    actions::ActionOrigin origin;
    origin.origin = execution::Origin::Companion;
    origin.identity = connection.companion->id;
    origin.client = connection.companion->name;
    origin.peer = connection.host;
    origin.surface = sessionId;
    const uint64_t connectionId = connection.id;
    WsHub* hub = &_hub;
    out.after.push_back([this, hub, &registry, id, value = std::move(value), instance, origin = std::move(origin),
                         connectionId, sessionId, rid]() mutable {
        hub->BeginDelivery();
        registry.Submit(id, std::move(value), instance, std::move(origin),
                        [this, hub, connectionId, sessionId, rid](actions::ActionResult result) {
                            // On the actions worker: the message is built and sent on the hub's loop
                            hub->PostTask([this, hub, connectionId, sessionId, rid, result = std::move(result)] {
                                DeliverResult(connectionId, sessionId, rid, result);
                                hub->EndDelivery();
                            });
                        });
    });
}

std::vector<std::string> SurfaceHub::ReadoutNamesOf(const std::string& instance)
{
    if (!instance.empty())
        if (std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(instance))
            return actions::ReadoutNames(*emulator);
    std::vector<std::string> names = actions::BaseReadoutNames();
    for (const actions::ActionRegistry::ToggleReadout& toggle : actions::ActionRegistry::Instance().ToggleReadouts())
        if (toggle.param.empty())
            names.push_back(toggle.stem);
    return names;
}

std::vector<std::string> SurfaceHub::Expand(SurfaceSession& session)
{
    const std::vector<std::string> names = ReadoutNamesOf(session.instance);
    std::set<std::string> readouts;
    std::vector<std::string> unknown;
    for (const std::string& pattern : session.patterns)
    {
        bool matched = false;
        for (const std::string& name : names)
            if (actions::ReadoutMatches(pattern, name))
            {
                readouts.insert(name);
                matched = true;
            }
        // A toggle by name the list does not spell out (`toggle.set.fast_tape`): read as it is ({on: null} when the
        // instance has no such toggle)
        if (!matched && !IsPattern(pattern) && pattern.compare(0, 7, "toggle.") == 0)
        {
            readouts.insert(pattern);
            matched = true;
        }
        if (!matched)
            unknown.push_back(pattern);
    }
    for (auto it = session.sent.begin(); it != session.sent.end();)
        it = readouts.count(it->first) == 0 ? session.sent.erase(it) : std::next(it);
    session.readouts = std::move(readouts);
    return unknown;
}

void SurfaceHub::Subscribe(WsConnection& connection, const Json::Value& message, WsOutbox& out)
{
    SurfaceSession* found = SessionOf(connection, message, out);
    if (found == nullptr)
        return;
    SurfaceSession& session = *found;
    const Json::Value& list = message["readouts"];
    double hz = session.hz;
    if (!list.isArray() || (message.isMember("max_hz") && !message["max_hz"].isNumeric()))
    {
        out.Error(connection, kTopic, "bad_request", "subscribe: readouts, an array of names; max_hz, a number");
        return;
    }
    if (message.isMember("max_hz"))
        hz = std::min(WsHub::kMaxHz, std::max(1.0, message["max_hz"].asDouble()));
    std::vector<std::string> added;
    for (const Json::Value& item : list)
    {
        const std::string pattern = item.isString() ? item.asString() : std::string();
        if (std::find(session.patterns.begin(), session.patterns.end(), pattern) == session.patterns.end())
        {
            session.patterns.push_back(pattern);
            added.push_back(pattern);
        }
    }
    for (const std::string& pattern : Expand(session))
        if (std::find(added.begin(), added.end(), pattern) != added.end())
        {
            Json::Value error = SessionMessage("error", session);
            error["error"] = "Not Found";
            error["message"] = "no readout '" + pattern +
                               "' (ttd.position, run.state, machine, media.tape, media.disk, audio.mute, debug.mode, "
                               "toggle.*)";
            out.Message(connection, error);
            session.patterns.erase(std::find(session.patterns.begin(), session.patterns.end(), pattern));
        }
    session.hz = hz;
    session.full = true;
}

void SurfaceHub::Unsubscribe(WsConnection& connection, const Json::Value& message, WsOutbox& out)
{
    SurfaceSession* found = SessionOf(connection, message, out);
    if (found == nullptr)
        return;
    SurfaceSession& session = *found;
    const Json::Value& list = message["readouts"];
    if (!list.isArray())
    {
        out.Error(connection, kTopic, "bad_request", "unsubscribe: readouts, an array of names");
        return;
    }
    for (const Json::Value& item : list)
    {
        const std::string pattern = item.isString() ? item.asString() : std::string();
        // A pattern removes the patterns it covers ("*" every one); a name the patterns that are that name
        session.patterns.erase(std::remove_if(session.patterns.begin(), session.patterns.end(),
                                              [&](const std::string& p) { return WsHub::Matches(pattern, p); }),
                               session.patterns.end());
        for (auto it = session.readouts.begin(); it != session.readouts.end();)
        {
            if (WsHub::Matches(pattern, *it))
            {
                session.sent.erase(*it);
                it = session.readouts.erase(it);
            }
            else
                ++it;
        }
    }
}

void SurfaceHub::Bye(WsConnection& connection, const Json::Value& message, WsOutbox& out)
{
    if (message["session"].isString())
    {
        // One surface ends (a deck went), the others and the socket stay
        if (SurfaceSession* session = SessionOf(connection, message, out))
            connection.surfaces.erase(session->session);
        return;
    }
    connection.surfaces.clear();
    out.Close(connection);
}

/// endregion </Ops>

/// region <Pushes>

void SurfaceHub::Context(WsConnection& connection, SurfaceSession& session, const std::string& instance,
                         const std::string& context, const std::string& by, WsOutbox& out)
{
    if (session.instance != instance || session.context == context)
        return;
    session.context = context;
    Json::Value ctx = SessionMessage("ctx", session);
    ctx["instance"] = instance;
    ctx["context"] = context;
    ctx["by"] = by;
    out.Message(connection, ctx);
}

void SurfaceHub::Rebind(WsConnection& connection, SurfaceSession& session, const std::string& followed,
                        WsOutbox& out)
{
    std::string instance = followed;
    if (session.pinned)
        instance = !session.instance.empty() && EmulatorManager::GetInstance()->GetEmulator(session.instance)
                       ? session.instance
                       : std::string();
    if (instance != session.instance)
        Bind(session, instance, connection, out);
}

void SurfaceHub::Bind(SurfaceSession& session, const std::string& instance, WsConnection& connection, WsOutbox& out)
{
    session.instance = instance;
    session.sent.clear();
    session.full = true;
    (void)Expand(session);  // the new instance's toggles (its features)
    Json::Value target = TargetOf(session);
    target["op"] = "target";
    target["t"] = kTopic;
    target["session"] = session.session;
    out.Message(connection, target);
    session.context.clear();
    if (instance.empty())
        return;
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(instance);
    const actions::ContextInfo info = actions::ContextState::Instance().Get(instance, emulator.get());
    Context(connection, session, instance, info.context, info.set ? info.by : actions::ContextState::kByDefault, out);
}

bool SurfaceHub::Due(const SurfaceSession& session, std::chrono::steady_clock::time_point now)
{
    if (session.readouts.empty())
        return false;
    return session.full || now - session.sentAt >= std::chrono::duration<double>(0.75 / session.hz);
}

void SurfaceHub::Deliver(WsConnection& connection, SurfaceSession& session, const WsReadings& readings,
                         std::chrono::steady_clock::time_point now, WsOutbox& out)
{
    if (session.instance.empty())
    {
        session.full = false;
        return;
    }
    auto values = readings.find(session.instance);
    if (values == readings.end())
        return;  // gone meanwhile: the rebind sends a full readout
    std::string text;
    for (const std::string& name : session.readouts)
    {
        auto value = values->second.find(name);
        if (value == values->second.end())
            continue;
        std::string& last = session.sent[name];
        if (!session.full && last == value->second)
            continue;
        last = value->second;
        text += (text.empty() ? "\"" : ",\"") + name + "\":" + value->second;
    }
    session.full = false;
    if (text.empty())
        return;
    session.sentAt = now;
    out.Message(connection, "{\"op\":\"readout\",\"t\":\"surface\",\"session\":\"" + session.session +
                                "\",\"instance\":\"" + session.instance + "\",\"values\":{" + text + "}}");
}

void SurfaceHub::Activity(WsConnection& connection, SurfaceSession& session, const Json::Value& entry,
                          const std::string& instance, const std::string& surfaceSession,
                          std::chrono::steady_clock::time_point now, bool congested, WsOutbox& out)
{
    // Not its own actions, its bound instance only
    if (!session.activity || session.instance.empty() || session.instance != instance ||
        surfaceSession == session.session)
        return;
    if (now - session.activityWindow >= std::chrono::seconds(1))
    {
        session.activityWindow = now;
        session.activityCount = 0;
    }
    if (congested || session.activityCount >= kActivityPerSecond)
    {
        session.activityDropped++;
        return;
    }
    session.activityCount++;
    Json::Value message = entry;
    message["op"] = "activity";
    message["t"] = kTopic;
    message["session"] = session.session;
    if (session.activityDropped > 0)
    {
        message["dropped"] = static_cast<Json::UInt64>(session.activityDropped);
        session.activityDropped = 0;
    }
    out.Message(connection, message);
}

void SurfaceHub::Catalogue(WsConnection& connection, SurfaceSession& session, uint64_t revision, WsOutbox& out)
{
    if (!session.catalogue)
        return;
    Json::Value message = SessionMessage("catalogue", session);
    message["rev"] = static_cast<Json::UInt64>(revision);
    out.Message(connection, message);
}

void SurfaceHub::DeliverResult(uint64_t connectionId, const std::string& sessionId, const Json::Value& rid,
                               const actions::ActionResult& result)
{
    WsOutbox out;
    {
        std::lock_guard<std::mutex> lock(_hub._mutex);
        WsConnection* connection = _hub.FindById(connectionId);
        if (connection == nullptr || connection->closing)
            return;
        auto session = connection->surfaces.find(sessionId);
        if (session == connection->surfaces.end())
            return;  // that surface ended meanwhile (bye)
        if (!rid.isNull())
        {
            Json::Value message = SessionMessage("result", *session->second);
            message["rid"] = rid;
            message["ok"] = result.ok;
            if (!result.ok)
                message["error"] = result.error;
            if (!result.message.empty())
                message["message"] = result.message;
            message["body"] = StateNodeToJson(result.body);
            out.Message(*connection, message);
        }
        else if (!result.ok)
        {
            Json::Value message = SessionMessage("error", *session->second);
            message["error"] = result.error;
            message["message"] = result.message;
            out.Message(*connection, message);
        }
    }
    out.Flush();
}

Json::Value SurfaceHub::TargetOf(const SurfaceSession& session)
{
    Json::Value target;
    target["instance"] = Nullable(session.instance);
    target["index"] = Json::Value(Json::nullValue);
    target["model"] = Json::Value(Json::nullValue);
    target["ram_kb"] = Json::Value(Json::nullValue);
    target["view"] = Json::Value(Json::nullValue);  // views: SD4 (TargetState)
    target["mode"] = session.pinned ? "instance" : "follow";
    if (session.instance.empty())
        return target;
    EmulatorManager* manager = EmulatorManager::GetInstance();
    const std::vector<std::string> ids = manager->GetEmulatorIds();
    const auto at = std::find(ids.begin(), ids.end(), session.instance);
    if (at != ids.end())
        target["index"] = static_cast<Json::Int>(at - ids.begin());
    if (std::shared_ptr<Emulator> emulator = manager->GetEmulator(session.instance))
    {
        const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);
        if (identity.Valid)
        {
            target["model"] = identity.Model;
            target["ram_kb"] = static_cast<Json::UInt>(identity.RamKb);
        }
    }
    return target;
}

/// endregion </Pushes>
