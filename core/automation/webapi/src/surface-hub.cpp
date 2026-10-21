#include "surface-hub.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "common/statenode_json.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/contextstate.h"
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
}  // namespace

/// region <Ops>

void SurfaceHub::Join(WsConnection& connection, const Json::Value& message, WsOutbox& out)
{
    if (connection.surface)
    {
        out.Error(connection, kTopic, "bad_request", "this connection joined already: one surface per connection");
        return;
    }
    if (!_hub.Authenticated(connection, kTopic, out))
        return;
    const Json::Value& v = message["v"];
    const Json::Value& name = message["name"];
    const Json::Value& model = message["model"];
    if (!v.isIntegral() || v.asInt64() < 1 || !name.isString() || name.asString().empty() || !model.isString() ||
        model.asString().empty() || (message.isMember("serial") && !message["serial"].isString()))
    {
        out.Error(connection, kTopic, "bad_request",
                  "join: v (the highest protocol version the client speaks, 1), name and model (strings), serial? "
                  "(a string), target? (\"follow\" or {\"instance\": id})");
        return;
    }

    auto session = std::make_unique<SurfaceSession>();
    const Json::Value& target = message["target"];
    if (target.isObject() && target.isMember("view"))
    {
        out.Error(connection, kTopic, "not_now",
                  "views come with the Stream Deck targeting (TargetState, SD4): join with \"follow\" or "
                  "{\"instance\": id}");
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
            out.Error(connection, kTopic, "Not Found", "no instance '" + named + "'");
            return;
        }
    }
    else if (!target.isNull() && !(target.isString() && target.asString() == "follow"))
    {
        out.Error(connection, kTopic, "bad_request", "target: \"follow\" or {\"instance\": id or index}");
        return;
    }
    else
        session->instance = WsHub::FollowedInstance();

    session->session = "s" + std::to_string(_nextSession++);
    session->v = static_cast<int>(std::min<int64_t>(v.asInt64(), kVersion));
    session->name = name.asString();
    session->model = model.asString();
    session->serial = message.get("serial", "").asString();
    if (!session->instance.empty())
    {
        std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(session->instance);
        session->context = actions::ContextState::Instance().Get(session->instance, emulator.get()).context;
    }

    Json::Value welcome;
    welcome["op"] = "welcome";
    welcome["t"] = kTopic;
    welcome["v"] = session->v;
    welcome["session"] = session->session;
    welcome["instance"] = Nullable(session->instance);
    welcome["context"] = Nullable(session->context);
    welcome["scopes"] = Json::Value(Json::arrayValue);
    for (const std::string& scope : automation_gate::CompanionScopeNames(connection.companion->scopes))
        welcome["scopes"].append(scope);
    welcome["target"] = TargetOf(*session);
    welcome["readouts"] = Json::Value(Json::arrayValue);
    for (const std::string& readout : WsHub::ReadoutNames())
        welcome["readouts"].append(readout);
    connection.surface = std::move(session);
    out.Message(connection, welcome);
}

void SurfaceHub::Action(WsConnection& connection, const Json::Value& message, WsOutbox& out)
{
    const Json::Value rid = message.get("rid", Json::Value());
    if (!connection.surface)
    {
        out.Error(connection, kTopic, "bad_request", "join first: {\"op\":\"join\",\"v\":1,\"name\":…,\"model\":…}",
                  rid);
        return;
    }
    const std::string id = message.get("id", "").asString();
    if (id.empty())
    {
        out.Error(connection, kTopic, "bad_request", "action: id, an action id (GET /api/v1/controllers/actions)", rid);
        return;
    }
    actions::ActionRegistry& registry = _hub.Registry();
    const std::optional<actions::ActionInfo> info = registry.Find(id);
    const std::string instance = connection.surface->instance;
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
        out.after.push_back([this, connectionId, rid, result = *refused] { DeliverResult(connectionId, rid, result); });
        return;
    }

    actions::ActionOrigin origin;
    origin.origin = execution::Origin::Companion;
    origin.identity = connection.companion->id;
    origin.client = connection.companion->name;
    origin.peer = connection.host;
    const uint64_t connectionId = connection.id;
    WsHub* hub = &_hub;
    out.after.push_back([this, hub, &registry, id, value = std::move(value), instance, origin = std::move(origin),
                         connectionId, rid]() mutable {
        hub->BeginDelivery();
        registry.Submit(id, std::move(value), instance, std::move(origin),
                        [this, hub, connectionId, rid](actions::ActionResult result) {
                            // On the actions worker: the message is built and sent on the hub's loop
                            hub->PostTask([this, hub, connectionId, rid, result = std::move(result)] {
                                DeliverResult(connectionId, rid, result);
                                hub->EndDelivery();
                            });
                        });
    });
}

void SurfaceHub::Subscribe(WsConnection& connection, const Json::Value& message, WsOutbox& out)
{
    if (!connection.surface)
    {
        out.Error(connection, kTopic, "bad_request", "join first: readouts belong to the surface topic");
        return;
    }
    SurfaceSession& session = *connection.surface;
    const Json::Value& list = message["readouts"];
    double hz = session.hz;
    if (!list.isArray() || (message.isMember("max_hz") && !message["max_hz"].isNumeric()))
    {
        out.Error(connection, kTopic, "bad_request", "subscribe: readouts, an array of names; max_hz, a number");
        return;
    }
    if (message.isMember("max_hz"))
        hz = std::min(WsHub::kMaxHz, std::max(1.0, message["max_hz"].asDouble()));
    for (const Json::Value& item : list)
    {
        const std::string pattern = item.isString() ? item.asString() : std::string();
        bool matched = false;
        for (const std::string& name : WsHub::ReadoutNames())
            if (WsHub::Matches(pattern, name))
            {
                session.readouts.insert(name);
                matched = true;
            }
        if (!matched)
            out.Error(connection, kTopic, "Not Found", "no readout '" + pattern + "' (ttd.position, run.state)");
    }
    session.hz = hz;
    session.full = true;
}

void SurfaceHub::Unsubscribe(WsConnection& connection, const Json::Value& message, WsOutbox& out)
{
    if (!connection.surface)
    {
        out.Error(connection, kTopic, "bad_request", "join first: readouts belong to the surface topic");
        return;
    }
    SurfaceSession& session = *connection.surface;
    const Json::Value& list = message["readouts"];
    if (!list.isArray())
    {
        out.Error(connection, kTopic, "bad_request", "unsubscribe: readouts, an array of names");
        return;
    }
    for (const Json::Value& item : list)
    {
        const std::string pattern = item.isString() ? item.asString() : std::string();
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

void SurfaceHub::Bye(WsConnection& connection, WsOutbox& out)
{
    connection.surface.reset();
    out.Close(connection);
}

/// endregion </Ops>

/// region <Pushes>

void SurfaceHub::Context(WsConnection& connection, const std::string& instance, const std::string& context,
                         const std::string& by, WsOutbox& out)
{
    SurfaceSession& session = *connection.surface;
    if (session.instance != instance || session.context == context)
        return;
    session.context = context;
    Json::Value ctx;
    ctx["op"] = "ctx";
    ctx["t"] = kTopic;
    ctx["instance"] = instance;
    ctx["context"] = context;
    ctx["by"] = by;
    out.Message(connection, ctx);
}

void SurfaceHub::Rebind(WsConnection& connection, const std::string& followed, WsOutbox& out)
{
    SurfaceSession& session = *connection.surface;
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
    Json::Value target = TargetOf(session);
    target["op"] = "target";
    target["t"] = kTopic;
    out.Message(connection, target);
    session.context.clear();
    if (instance.empty())
        return;
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(instance);
    const actions::ContextInfo info = actions::ContextState::Instance().Get(instance, emulator.get());
    Context(connection, instance, info.context, info.set ? info.by : actions::ContextState::kByDefault, out);
}

bool SurfaceHub::Due(const SurfaceSession& session, std::chrono::steady_clock::time_point now)
{
    if (session.readouts.empty())
        return false;
    return session.full || now - session.sentAt >= std::chrono::duration<double>(0.75 / session.hz);
}

void SurfaceHub::Deliver(WsConnection& connection, const WsReadings& readings,
                         std::chrono::steady_clock::time_point now, WsOutbox& out)
{
    SurfaceSession& session = *connection.surface;
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
    out.Message(connection, "{\"op\":\"readout\",\"t\":\"surface\",\"instance\":\"" + session.instance +
                                "\",\"values\":{" + text + "}}");
}

void SurfaceHub::DeliverResult(uint64_t connectionId, const Json::Value& rid, const actions::ActionResult& result)
{
    WsOutbox out;
    {
        std::lock_guard<std::mutex> lock(_hub._mutex);
        WsConnection* connection = _hub.FindById(connectionId);
        if (connection == nullptr || connection->closing)
            return;
        if (!rid.isNull())
        {
            Json::Value message;
            message["op"] = "result";
            message["t"] = kTopic;
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
            out.Error(*connection, kTopic, result.error, result.message);
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
