#include "ws-hub.h"

#include <algorithm>

#include "3rdparty/message-center/messagecenter.h"
#include "debug-event-hub.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdcursorcell.h"
#include "common/statenode_json.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/activityfeed.h"
#include "emulator/actions/contextstate.h"
#include "emulator/actions/readouts.h"
#include "emulator/companions/streamdecksupervisor.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

namespace
{
std::string Compact(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    return Json::writeString(builder, value);
}

/// The interval of `hz`, with room for the timer's jitter: a session at the timer's own rate is due at every tick
std::chrono::duration<double> DueAfter(double hz)
{
    return std::chrono::duration<double>(0.75 / std::max(hz, 0.1));
}

/// `max_hz` of a subscribe: a number, at most kMaxHz, at least 1; `current` when absent. False when not a number
bool RateOf(const Json::Value& message, double current, double& hz)
{
    if (!message.isMember("max_hz"))
    {
        hz = current;
        return true;
    }
    if (!message["max_hz"].isNumeric())
        return false;
    hz = std::min(WsHub::kMaxHz, std::max(1.0, message["max_hz"].asDouble()));
    return true;
}

/// What can change the instance a following surface takes, or revoke a companion
const char* const kRefreshTriggers[] = {
    NC_COMPANION_CHANGED, NC_EMULATOR_INSTANCE_CREATED, NC_EMULATOR_INSTANCE_DESTROYED, NC_EMULATOR_SELECTION_CHANGED,
};
/// What changes a state topic's value or an `instances` entry without a TTD frame: woken at once instead of waiting
/// for the next tick
const char* const kWakeTriggers[] = {
    NC_EMULATOR_STATE_CHANGE,
    NC_FEATURE_CHANGED,
    NC_RUN_CONTROL_CHANGED,
};

constexpr const char* kInstances = "instances";
/// The Stream Deck daemon's supervision (companion::streamdeck::Supervisor): one value for the process, no instance
constexpr const char* kStreamDeck = "streamdeck.status";

bool IsProcessTopic(const std::string& name)
{
    return name == kStreamDeck;
}

bool IsEventTopic(const std::string& name)
{
    const std::vector<std::string>& names = WsHub::EventTopicNames();
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool IsStateTopic(const std::string& name)
{
    const std::vector<std::string>& names = WsHub::StateTopicNames();
    return std::find(names.begin(), names.end(), name) != names.end();
}

/// The peer is this host (the debug topics need no token from it, as /api/v1/websocket)
bool IsLoopback(const std::string& host)
{
    return host == "::1" || host.rfind("127.", 0) == 0 || host.rfind("::ffff:127.", 0) == 0;
}

bool Congested(const WsConnection& connection)
{
    return connection.flow && connection.flow->congested.load(std::memory_order_relaxed);
}
}  // namespace

/// region <WsOutbox>

void WsOutbox::Message(const WsConnection& to, std::string text)
{
    if (!to.closing && to.send)
        messages.emplace_back(to.send, std::move(text));
}

void WsOutbox::Message(const WsConnection& to, const Json::Value& message)
{
    Message(to, Compact(message));
}

void WsOutbox::Error(const WsConnection& to, const std::string& t, const std::string& error,
                     const std::string& message, const Json::Value& rid)
{
    Json::Value out;
    out["op"] = "error";
    out["t"] = t;
    if (!rid.isNull())
        out["rid"] = rid;
    out["error"] = error;
    out["message"] = message;
    Message(to, out);
}

void WsOutbox::Close(WsConnection& connection)
{
    if (connection.closing)
        return;
    connection.closing = true;
    if (connection.close)
        closes.push_back(connection.close);
}

void WsOutbox::Flush()
{
    for (const auto& [send, text] : messages)
        send(text);
    messages.clear();
    for (const auto& close : closes)
        close();
    closes.clear();
    for (const auto& task : after)
        task();
    after.clear();
}

/// endregion </WsOutbox>

/// region <Set-up>

WsHub& WsHub::Instance()
{
    static WsHub* hub = new WsHub();
    return *hub;
}

WsHub::WsHub() = default;

WsHub::~WsHub()
{
    Stop();
    {
        std::lock_guard<std::mutex> lock(_timerMutex);
        if (_armed && _timer.disarm)
            _timer.disarm(_timerId);
        _armed = false;
    }
    // Results on their way back from an actions worker capture the hub: let them land
    std::unique_lock<std::mutex> lock(_flightMutex);
    _flightDone.wait_for(lock, std::chrono::seconds(2), [this] { return _inFlight == 0; });
}

void WsHub::SetLoop(Post post, Timer timer)
{
    {
        std::lock_guard<std::mutex> lock(_postMutex);
        _post = std::move(post);
    }
    {
        std::lock_guard<std::mutex> lock(_timerMutex);
        if (_armed && _timer.disarm)
            _timer.disarm(_timerId);
        _armed = false;
        _timerHz = 0;
        _timer = std::move(timer);
    }
    UpdateTimer();
}

void WsHub::SetClock(Clock clock)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _clock = std::move(clock);
}

void WsHub::SetRegistry(actions::ActionRegistry* registry)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _registry = registry;
}

void WsHub::SetSnapshot(Snapshot snapshot)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _snapshot = std::move(snapshot);
}

void WsHub::SetSlowConsumerLimit(std::chrono::milliseconds limit)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _slowAfter = limit;
}

void WsHub::SetHighWaterMark(uint64_t bytes)
{
    _highWater.store(std::max<uint64_t>(bytes, 1), std::memory_order_relaxed);
}

uint64_t WsHub::HighWaterMark() const
{
    return _highWater.load(std::memory_order_relaxed);
}

void WsHub::Start()
{
    std::lock_guard<std::mutex> lock(_observersMutex);
    if (!_observers.empty())
        return;
    MessageCenter& center = MessageCenter::DefaultMessageCenter();
    _observers.emplace_back(NC_SURFACE_CONTEXT,
                            center.AddObserver(NC_SURFACE_CONTEXT, ObserverCallbackFunc([this](int, Message* message) {
                                auto* payload = message != nullptr
                                                    ? dynamic_cast<SurfaceContextPayload*>(message->obj)
                                                    : nullptr;
                                if (payload == nullptr)
                                    return;
                                PostTask([this, id = payload->emulatorId.toString(), context = payload->context,
                                          by = payload->by] { OnContext(id, context, by); });
                            })));
    for (const char* topic : kRefreshTriggers)
        _observers.emplace_back(topic, center.AddObserver(topic, ObserverCallbackFunc([this](int, Message*) {
                                    PostTask([this] { Refresh(); });
                                })));
    for (const char* topic : kWakeTriggers)
        _observers.emplace_back(topic, center.AddObserver(topic, ObserverCallbackFunc([this](int, Message*) {
                                    if (_subscribed.load(std::memory_order_relaxed))
                                        PostTask([this] { Tick(); });
                                    WakeInstances();
                                })));
    _observers.emplace_back(
        NC_ACTION_CATALOGUE,
        center.AddObserver(NC_ACTION_CATALOGUE, ObserverCallbackFunc([this](int, Message* message) {
                               auto* payload = message != nullptr
                                                   ? dynamic_cast<ActionCatalogueChangedPayload*>(message->obj)
                                                   : nullptr;
                               const uint64_t revision = payload != nullptr ? payload->revision
                                                                            : Registry().Revision();
                               PostTask([this, revision] { OnCatalogue(revision); });
                           })));
    // The debug.* topics: DebugEventHub numbers the events, the hub queues them for its loop
    _debugSink = DebugEventHub::Instance().AddSink(
        [this](const std::string& topic, const Json::Value& event) { OnDebugEvent(topic, event); });
    // `activity` of the surfaces: called on the actions worker after each job; only a copy and a post
    _activityListener = actions::ActivityFeed::Instance().AddListener([this](const actions::ActivityEntry& entry) {
        if (_activitySubscribed.load(std::memory_order_relaxed))
            PostTask([this, entry] { OnActivity(entry); });
    });
}

void WsHub::Stop()
{
    std::vector<std::pair<std::string, uint64_t>> observers;
    uint64_t sink = 0;
    uint64_t listener = 0;
    {
        std::lock_guard<std::mutex> lock(_observersMutex);
        observers.swap(_observers);
        std::swap(sink, _debugSink);
        std::swap(listener, _activityListener);
    }
    if (sink != 0)
        DebugEventHub::Instance().RemoveSink(sink);
    if (listener != 0)
        actions::ActivityFeed::Instance().RemoveListener(listener);
    MessageCenter& center = MessageCenter::DefaultMessageCenter();
    for (const auto& [topic, id] : observers)
        center.RemoveObserverById(topic, id);
}

/// endregion </Set-up>

/// region <Connections>

void WsHub::Open(const void* client, Peer peer, std::function<void(const std::string&)> send,
                 std::function<void()> close, std::shared_ptr<WsFlow> flow, std::function<void()> probe)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto connection = std::make_unique<WsConnection>();
    connection->id = _nextId++;
    connection->send = std::move(send);
    connection->close = std::move(close);
    connection->flow = std::move(flow);
    connection->probe = std::move(probe);
    connection->token = std::move(peer.token);
    connection->source = std::move(peer.source);
    connection->host = std::move(peer.host);
    _connections[client] = std::move(connection);
}

void WsHub::SetToken(const void* client, const std::string& token)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (WsConnection* connection = Find(client))
    {
        connection->token = token;
        connection->companion.reset();
    }
}

bool WsHub::HandleMessage(const void* client, const Json::Value& message)
{
    const std::string op = message.get("op", "").asString();
    if (op != "subscribe" && op != "unsubscribe" && op != "snapshot" && op != "join" && op != "action" && op != "bye")
        return false;
    WsOutbox out;
    bool subscriptions = false;
    bool instances = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        WsConnection* connection = Find(client);
        if (connection == nullptr || connection->closing)
            return true;
        if (op == "snapshot")
        {
            // As {"op":"snapshot"} on the events socket: the whole view again
            if (connection->topics.count(kInstances) == 0)
                out.Error(*connection, kInstances, "bad_request", "snapshot: subscribe to the instances topic first");
            else
            {
                connection->instancesFull = true;
                connection->instancesOp = "snapshot";
                instances = true;
            }
        }
        else if (op == "join")
            _surface.Join(*connection, message, out);
        else if (op == "action")
            _surface.Action(*connection, message, out);
        else if (op == "bye")
            _surface.Bye(*connection, message, out);
        else
        {
            subscriptions = true;
            const bool topics = message.isMember("topics");
            const bool readouts = message.isMember("readouts");
            if (!topics && !readouts)
                out.Error(*connection, "ws", "bad_request",
                          op + " takes topics[] (run.state, ttd.position, surfaces.context, instances, debug.events, "
                               "debug.asm_sync) or readouts[] (the surface topic, after join)");
            if (topics)
                op == "subscribe" ? SubscribeTopics(*connection, message, out)
                                  : UnsubscribeTopics(*connection, message, out);
            instances = op == "subscribe" && connection->instancesFull;
            if (readouts)
                op == "subscribe" ? _surface.Subscribe(*connection, message, out)
                                  : _surface.Unsubscribe(*connection, message, out);
        }
    }
    out.Flush();
    // The view at once (before the tick below, which then finds it fresh)
    if (instances)
        PostTask([this] { RefreshInstances(); });
    if (op == "join" || op == "bye")
        UpdateTimer();  // who wants `activity`, the readouts of a surface that ended
    if (subscriptions)
    {
        UpdateTimer();
        // The current values at once, not at the next tick
        if (op == "subscribe")
            PostTask([this] { Tick(); });
    }
    return true;
}

void WsHub::Remove(const void* client)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _connections.erase(client);
    }
    UpdateTimer();
}

WsConnection* WsHub::Find(const void* client)
{
    auto it = _connections.find(client);
    return it != _connections.end() ? it->second.get() : nullptr;
}

WsConnection* WsHub::FindById(uint64_t id)
{
    for (auto& [client, connection] : _connections)
        if (connection->id == id)
            return connection.get();
    return nullptr;
}

bool WsHub::Authenticated(WsConnection& connection, const std::string& t, WsOutbox& out)
{
    if (connection.companion)
        return true;  // Refresh re-checks it when the companions change
    if (connection.token.empty())
    {
        out.Error(connection, t, "companion_required",
                  "authenticate first: {\"op\":\"auth\",\"token\":\"unc_...\"} or Authorization: Bearer <token> on "
                  "the handshake");
        return false;
    }
    if (connection.source == "mcp")
    {
        out.Error(connection, t, "companion_not_for_agents",
                  "An MCP agent does not act as a companion: take control (control_execution take_control) instead");
        return false;
    }
    const std::optional<companion::CompanionInfo> companion =
        companion::CompanionRegistry::Instance().Authenticate(connection.token);
    if (!companion)
    {
        // As the events socket: an unknown token ends the connection, the client pairs again
        out.Error(connection, t, "companion_unknown", "this token is not known or was revoked: pair again");
        out.Close(connection);
        return false;
    }
    connection.companion = *companion;
    return true;
}

/// endregion </Connections>

/// region <Topics>

void WsHub::SubscribeTopics(WsConnection& connection, const Json::Value& message, WsOutbox& out)
{
    const Json::Value& list = message["topics"];
    double hz = connection.topicHz;
    if (!list.isArray() || !RateOf(message, connection.topicHz, hz))
    {
        out.Error(connection, "ws", "bad_request", "subscribe: topics, an array of names; max_hz, a number (<= 50)");
        return;
    }
    std::set<std::string> names;
    for (const Json::Value& item : list)
    {
        const std::string pattern = item.isString() ? item.asString() : std::string();
        if (pattern == "surface")
        {
            out.Error(connection, "surface", "bad_request",
                      "join opens the surface topic: {\"op\":\"join\",\"v\":1,\"name\":…,\"model\":…}");
            continue;
        }
        bool matched = false;
        for (const std::string& name : TopicNames())
            if (Matches(pattern, name))
            {
                names.insert(name);
                matched = true;
            }
        if (!matched)
            out.Error(connection, pattern.empty() ? std::string("ws") : pattern, "Not Found",
                      "no topic '" + pattern +
                          "' (run.state, ttd.position, surfaces.context, streamdeck.status, instances, debug.events, "
                          "debug.asm_sync)");
    }

    // The state topics and `instances` need the companion; the debug topics a companion or a loopback peer
    const bool needsCompanion =
        std::any_of(names.begin(), names.end(), [](const std::string& name) { return !IsEventTopic(name); });
    bool companion = false;
    bool told = false;
    if (needsCompanion || !connection.token.empty())
    {
        companion = Authenticated(connection, "ws", out);
        told = !companion;
        if (connection.closing)
            return;
    }
    const bool local = IsLoopback(connection.host);
    size_t added = 0;
    for (const std::string& name : names)
    {
        if (IsEventTopic(name) ? (companion || local) : companion)
        {
            connection.topics.insert(name);
            added++;
            if (name == kInstances)
            {
                // The whole view first, as `hello` on the events socket
                connection.instancesFull = true;
                connection.instancesOp = "hello";
            }
        }
        else if (!told)
        {
            out.Error(connection, "ws", "companion_required",
                      "the debug topics take a companion token from another host: {\"op\":\"auth\",\"token\":…}");
            told = true;
        }
    }
    if (told && added == 0)
        return;  // refused: the error is the answer
    connection.topicHz = hz;
    connection.topicFull = true;
    Json::Value ack;
    ack["op"] = "subscribed";
    ack["t"] = "ws";
    ack["topics"] = Json::Value(Json::arrayValue);
    for (const std::string& name : connection.topics)
        ack["topics"].append(name);
    ack["max_hz"] = hz;
    out.Message(connection, ack);
}

void WsHub::UnsubscribeTopics(WsConnection& connection, const Json::Value& message, WsOutbox& out)
{
    const Json::Value& list = message["topics"];
    if (!list.isArray())
    {
        out.Error(connection, "ws", "bad_request", "unsubscribe: topics, an array of names");
        return;
    }
    for (const Json::Value& item : list)
    {
        const std::string pattern = item.isString() ? item.asString() : std::string();
        for (auto it = connection.topics.begin(); it != connection.topics.end();)
            it = Matches(pattern, *it) ? connection.topics.erase(it) : std::next(it);
    }
    for (auto it = connection.topicSent.begin(); it != connection.topicSent.end();)
    {
        const std::string topic = it->first.substr(0, it->first.find('|'));
        it = connection.topics.count(topic) == 0 ? connection.topicSent.erase(it) : std::next(it);
    }
    if (connection.topics.count(kInstances) == 0)
    {
        connection.instancesFull = false;
        connection.instancesSent.clear();
    }
    auto& events = connection.events;
    events.erase(std::remove_if(events.begin(), events.end(),
                                [&](const auto& event) { return connection.topics.count(event.first) == 0; }),
                 events.end());
    if (std::none_of(connection.topics.begin(), connection.topics.end(), IsEventTopic))
        connection.eventsDropped = 0;
    Json::Value ack;
    ack["op"] = "unsubscribed";
    ack["t"] = "ws";
    ack["topics"] = Json::Value(Json::arrayValue);
    for (const std::string& name : connection.topics)
        ack["topics"].append(name);
    out.Message(connection, ack);
}

const std::vector<std::string>& WsHub::TopicNames()
{
    static const std::vector<std::string> names = {"run.state",    "ttd.position",   "surfaces.context",
                                                   kStreamDeck,    kInstances,       "debug.events",
                                                   "debug.asm_sync"};
    return names;
}

const std::vector<std::string>& WsHub::StateTopicNames()
{
    static const std::vector<std::string> names = {"run.state", "ttd.position", "surfaces.context", kStreamDeck};
    return names;
}

const std::vector<std::string>& WsHub::EventTopicNames()
{
    static const std::vector<std::string> names = {"debug.events", "debug.asm_sync"};
    return names;
}

const std::vector<std::string>& WsHub::ReadoutNames()
{
    static const std::vector<std::string> names = {"ttd.position", "run.state"};
    return names;
}

bool WsHub::Matches(const std::string& pattern, const std::string& name)
{
    if (pattern == name || pattern == "*")
        return true;
    if (pattern.size() >= 2 && pattern.compare(pattern.size() - 2, 2, ".*") == 0)
        return name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
    return false;
}

Json::Value WsHub::Read(const std::string& name, const std::string& instanceId, Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    if (context == nullptr || context->pCore == nullptr)
        return Json::Value();
    Json::Value value(Json::objectValue);
    if (name == "run.state")
    {
        value["state"] = !emulator.IsRunning() ? "stopped" : emulator.IsPaused() ? "paused" : "running";
        value["speed"] = static_cast<int>(emulator.GetSpeedMultiplier());
        value["turbo"] = emulator.IsTurboMode();
        return value;
    }
    if (name == "ttd.position")
    {
        // The published cursor (a seqlock read): never a session operation, never waits for the machine
        const ttd::ITimeTravelHooks* hooks = context->pTimeTravelHooks;
        const ttd::TTDCursor cursor = hooks != nullptr ? hooks->PublishedCursor() : ttd::TTDCursor{};
        value["frame"] = static_cast<Json::UInt64>(cursor.frame);
        value["tinframe"] = static_cast<Json::UInt>(cursor.tinframe);
        value["first"] = static_cast<Json::UInt64>(cursor.first);
        value["end"] = static_cast<Json::UInt64>(cursor.end);
        value["end_tinframe"] = static_cast<Json::UInt>(cursor.endTinframe);
        value["state"] = ttd::TTDSessionStateToString(cursor.state);
        value["history"] = cursor.history;
        value["paused"] = hooks != nullptr ? cursor.paused : emulator.IsPaused();
        value["fps"] = ttd::TTDFrameRate(context);
        return value;
    }
    if (name == "surfaces.context")
    {
        const actions::ContextInfo info = actions::ContextState::Instance().Get(instanceId, &emulator);
        value["context"] = info.context;
        value["set"] = info.set;
        value["by"] = info.set ? Json::Value(info.by) : Json::Value(Json::nullValue);
        value["default"] = info.fallback;
        return value;
    }
    return Json::Value();
}

std::string WsHub::FollowedInstance()
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    const std::string selected = manager->GetSelectedEmulatorId();
    if (!selected.empty() && manager->GetEmulator(selected))
        return selected;
    const std::vector<std::string> ids = manager->GetEmulatorIds();
    return ids.size() == 1 ? ids.front() : std::string();
}

/// endregion </Topics>

/// region <Driven by the loop>

void WsHub::Tick()
{
    WsOutbox out;
    bool refreshInstances = false;
    bool drained = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto now = Now();
        std::vector<WsConnection*> topicDue;
        SurfaceDue surfaceDue;
        std::set<std::string> topicNames;
        for (auto& [client, connection] : _connections)
        {
            if (connection->closing)
                continue;
            // Backpressure: above the high-water mark only control messages go; the values wait (the latest is sent
            // once it drains), the events queue (the oldest dropped); too long above it: a slow consumer
            if (Congested(*connection))
            {
                if (!connection->congestedAt)
                    connection->congestedAt = now;
                if (now - *connection->congestedAt >= _slowAfter)
                {
                    out.Error(*connection, "ws", "slow_consumer",
                              "this connection read too slowly: " +
                                  std::to_string(connection->flow->queued.load(std::memory_order_relaxed)) +
                                  " bytes queued, above the high-water mark for " +
                                  std::to_string(_slowAfter.count()) + " ms");
                    out.Close(*connection);
                    continue;
                }
                if (connection->probe)
                    out.after.push_back(connection->probe);
                continue;
            }
            if (connection->congestedAt)
            {
                // Drained: `instances` sends what changed meanwhile now, not at the next second
                connection->congestedAt.reset();
                drained = true;
            }
            FlushEvents(*connection, out);
            const bool states = std::any_of(connection->topics.begin(), connection->topics.end(), IsStateTopic);
            if (states && (connection->topicFull || now - connection->topicSentAt >= DueAfter(connection->topicHz)))
            {
                topicDue.push_back(connection.get());
                for (const std::string& name : connection->topics)
                    if (IsStateTopic(name))
                        topicNames.insert(name);
            }
            for (auto& [id, session] : connection->surfaces)
                if (SurfaceHub::Due(*session, now))
                    surfaceDue.emplace_back(connection.get(), session.get());
        }
        // `instances`: what changes with time alone (a re-take cooldown, the first recorded frame) once a second
        refreshInstances = _instancesSubscribed.load(std::memory_order_relaxed) &&
                           (drained || now - _instancesAt >= DueAfter(1));
        if (!topicDue.empty() || !surfaceDue.empty())
            DeliverValues(topicDue, topicNames, surfaceDue, now, out);
    }
    out.Flush();
    if (refreshInstances)
        RefreshInstances();
}

void WsHub::DeliverValues(const std::vector<WsConnection*>& topicDue, const std::set<std::string>& topicNames,
                          const SurfaceDue& surfaceDue, std::chrono::steady_clock::time_point now, WsOutbox& out)
{
    // Read each value once: published state only
    EmulatorManager* manager = EmulatorManager::GetInstance();
    WsReadings readings;
    std::map<std::string, std::shared_ptr<Emulator>> emulators;
    auto read = [&](const std::string& id, const std::string& name) {
        auto& values = readings[id];
        if (values.count(name) != 0)
            return;
        auto it = emulators.find(id);
        if (it == emulators.end())
            it = emulators.emplace(id, manager->GetEmulator(id)).first;
        if (!it->second)
            return;
        // The state topics' own reads (the surface's ttd.position / run.state are the same bytes); the rest of the
        // readout vocabulary from readouts.h
        const std::vector<std::string>& own = StateTopicNames();
        values[name] = std::find(own.begin(), own.end(), name) != own.end()
                           ? Compact(Read(name, id, *it->second))
                           : Compact(StateNodeToJson(actions::ReadReadout(*it->second, name)));
    };
    std::vector<std::string> all;
    // the process's own topics: read once, no instance
    std::map<std::string, std::string> processValues;
    if (!topicDue.empty())
    {
        all = manager->GetEmulatorIds();
        for (const std::string& id : all)
            for (const std::string& name : topicNames)
                if (!IsProcessTopic(name))
                    read(id, name);
        for (const std::string& name : topicNames)
            if (IsProcessTopic(name))
                processValues[name] = Compact(StateNodeToJson(companion::streamdeck::Supervisor::Instance().Summary()));
    }
    for (const auto& [connection, session] : surfaceDue)
        if (!session->instance.empty())
            for (const std::string& name : session->readouts)
                read(session->instance, name);

    // A topic's message of one instance is the same bytes for every subscriber: built once
    std::map<std::string, std::string> built;
    for (WsConnection* connection : topicDue)
    {
        bool sent = false;
        std::set<std::string> present;
        for (const std::string& id : all)
        {
            auto values = readings.find(id);
            if (values == readings.end())
                continue;
            for (const std::string& topic : connection->topics)
            {
                if (!IsStateTopic(topic))
                    continue;
                auto value = values->second.find(topic);
                if (value == values->second.end())
                    continue;
                const std::string key = topic + "|" + id;
                present.insert(key);
                std::string& last = connection->topicSent[key];
                if (!connection->topicFull && last == value->second)
                    continue;
                last = value->second;
                auto text = built.find(key);
                if (text == built.end())
                    text = built
                               .emplace(key, "{\"t\":\"" + topic + "\",\"op\":\"state\",\"instance\":\"" + id +
                                                 "\",\"value\":" + value->second + "}")
                               .first;
                out.Message(*connection, text->second);
                sent = true;
            }
        }
        for (const std::string& topic : connection->topics)
        {
            auto value = processValues.find(topic);
            if (value == processValues.end())
                continue;
            present.insert(topic);
            std::string& last = connection->topicSent[topic];
            if (!connection->topicFull && last == value->second)
                continue;
            last = value->second;
            out.Message(*connection, "{\"t\":\"" + topic + "\",\"op\":\"state\",\"value\":" + value->second + "}");
            sent = true;
        }
        for (auto it = connection->topicSent.begin(); it != connection->topicSent.end();)
        {
            if (present.count(it->first) != 0)
            {
                ++it;
                continue;
            }
            const size_t bar = it->first.find('|');
            Json::Value gone;
            gone["t"] = it->first.substr(0, bar);
            gone["op"] = "gone";
            gone["instance"] = it->first.substr(bar + 1);
            out.Message(*connection, gone);
            sent = true;
            it = connection->topicSent.erase(it);
        }
        connection->topicFull = false;
        if (sent)
            connection->topicSentAt = now;
    }
    for (const auto& [connection, session] : surfaceDue)
        _surface.Deliver(*connection, *session, readings, now, out);
}

void WsHub::Refresh()
{
    WsOutbox out;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const std::string followed = FollowedInstance();
        for (auto& [client, connection] : _connections)
        {
            if (connection->closing)
                continue;
            if (connection->companion && !companion::CompanionRegistry::Instance().Find(connection->companion->id))
            {
                Json::Value revoked;
                revoked["op"] = "revoked";
                revoked["t"] = connection->surfaces.empty() ? "ws" : "surface";
                revoked["message"] = "the user revoked this companion: pair again to use it";
                out.Message(*connection, revoked);
                out.Close(*connection);
                continue;
            }
            for (auto& [id, session] : connection->surfaces)
                _surface.Rebind(*connection, *session, followed, out);
        }
    }
    out.Flush();
    if (_instancesSubscribed.load(std::memory_order_relaxed))
        RefreshInstances();
    if (_subscribed.load(std::memory_order_relaxed))
        Tick();
}

void WsHub::OnContext(const std::string& instanceId, const std::string& context, const std::string& by)
{
    WsOutbox out;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& [client, connection] : _connections)
            if (!connection->closing)
                for (auto& [id, session] : connection->surfaces)
                    _surface.Context(*connection, *session, instanceId, context, by, out);
    }
    out.Flush();
    if (_subscribed.load(std::memory_order_relaxed))
        Tick();
}

void WsHub::RefreshInstances()
{
    std::lock_guard<std::mutex> refresh(_instancesMutex);
    std::vector<std::pair<uint64_t, companion::CompanionInfo>> targets;
    Snapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _instancesAt = Now();
        for (const auto& [client, connection] : _connections)
            if (!connection->closing && connection->companion && connection->topics.count(kInstances) != 0 &&
                !Congested(*connection))
                targets.emplace_back(connection->id, *connection->companion);
        snapshot = _snapshot;
    }
    if (targets.empty())
        return;

    // Built outside the hub's lock (`me` reads every instance), once per companion
    std::map<std::string, Json::Value> views;
    for (const auto& [id, companion] : targets)
        if (views.count(companion.id) == 0 && companion::CompanionRegistry::Instance().Find(companion.id))
            views.emplace(companion.id, snapshot ? snapshot(companion) : Json::Value(Json::objectValue));

    WsOutbox out;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& [id, companion] : targets)
        {
            WsConnection* connection = FindById(id);
            auto view = views.find(companion.id);
            if (connection == nullptr || connection->closing || connection->topics.count(kInstances) == 0 ||
                view == views.end())
                continue;  // closed or unsubscribed meanwhile; revoked: Refresh tells it
            const Json::Value& me = view->second;
            if (connection->instancesFull)
            {
                connection->instancesFull = false;
                connection->instancesSent.clear();
                for (const Json::Value& emulator : me["emulators"])
                    connection->instancesSent[emulator["id"].asString()] = Compact(emulator);
                Json::Value message = me;
                message["t"] = kInstances;
                message["op"] = connection->instancesOp.empty() ? "hello" : connection->instancesOp;
                message["seq"] = static_cast<Json::UInt64>(++connection->instancesSeq);
                out.Message(*connection, message);
                continue;
            }
            std::set<std::string> present;
            for (const Json::Value& emulator : me["emulators"])
            {
                const std::string instance = emulator["id"].asString();
                present.insert(instance);
                std::string text = Compact(emulator);
                std::string& last = connection->instancesSent[instance];
                if (last == text)
                    continue;
                last = std::move(text);
                Json::Value event;
                event["t"] = kInstances;
                event["op"] = "emulator";
                event["seq"] = static_cast<Json::UInt64>(++connection->instancesSeq);
                event["emulator"] = emulator;
                out.Message(*connection, event);
            }
            for (auto sent = connection->instancesSent.begin(); sent != connection->instancesSent.end();)
            {
                if (present.count(sent->first) != 0)
                {
                    ++sent;
                    continue;
                }
                Json::Value event;
                event["t"] = kInstances;
                event["op"] = "emulator_gone";
                event["seq"] = static_cast<Json::UInt64>(++connection->instancesSeq);
                event["id"] = sent->first;
                out.Message(*connection, event);
                sent = connection->instancesSent.erase(sent);
            }
        }
    }
    out.Flush();
}

void WsHub::WakeInstances()
{
    if (!_instancesSubscribed.load(std::memory_order_relaxed) || _instancesPending.exchange(true))
        return;
    PostTask([this] {
        _instancesPending.store(false);
        RefreshInstances();
    });
}

void WsHub::OnDebugEvent(const std::string& topic, const Json::Value& event)
{
    if (!_eventsSubscribed.load(std::memory_order_relaxed))
        return;
    const std::string name = topic == "debug" ? std::string("debug.events") : "debug." + topic;
    bool post = false;
    {
        std::lock_guard<std::mutex> lock(_eventsMutex);
        if (_pendingEvents.size() >= kPendingEvents)
        {
            _pendingDropped[_pendingEvents.front().first]++;
            _pendingEvents.pop_front();
        }
        _pendingEvents.emplace_back(name, event);
        post = !_eventsPosted;
        _eventsPosted = true;
    }
    if (post)
        PostTask([this] { DrainEvents(); });
}

void WsHub::DrainEvents()
{
    std::deque<std::pair<std::string, Json::Value>> events;
    std::map<std::string, uint64_t> dropped;
    {
        std::lock_guard<std::mutex> lock(_eventsMutex);
        events.swap(_pendingEvents);
        dropped.swap(_pendingDropped);
        _eventsPosted = false;
    }
    WsOutbox out;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& [topic, count] : dropped)
            for (auto& [client, connection] : _connections)
                if (connection->topics.count(topic) != 0)
                    connection->eventsDropped += count;
        for (const auto& [topic, event] : events)
        {
            // Serialised once, on the loop: every subscriber gets the same bytes
            Json::Value message = event;
            message.removeMember("topic");
            message["t"] = topic;
            message["instance"] = event["emulator"];
            const std::string text = Compact(message);
            for (auto& [client, connection] : _connections)
            {
                if (connection->closing || connection->topics.count(topic) == 0)
                    continue;
                if (connection->events.size() >= kEventQueue)
                {
                    connection->events.pop_front();
                    connection->eventsDropped++;
                }
                connection->events.emplace_back(topic, text);
            }
        }
        for (auto& [client, connection] : _connections)
            if (!connection->closing && !Congested(*connection))
                FlushEvents(*connection, out);
    }
    out.Flush();
}

void WsHub::OnActivity(const actions::ActivityEntry& entry)
{
    // Serialised once, on the loop; each surface filters (not its own, its bound instance, 20 a second)
    const Json::Value node = StateNodeToJson(actions::ActivityEntryNode(entry));
    WsOutbox out;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto now = Now();
        for (auto& [client, connection] : _connections)
        {
            if (connection->closing)
                continue;
            const bool congested = Congested(*connection);
            for (auto& [id, session] : connection->surfaces)
                _surface.Activity(*connection, *session, node, entry.instance, entry.surfaceSession, now, congested,
                                  out);
        }
    }
    out.Flush();
}

void WsHub::OnCatalogue(uint64_t revision)
{
    WsOutbox out;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& [client, connection] : _connections)
            if (!connection->closing)
                for (auto& [id, session] : connection->surfaces)
                    _surface.Catalogue(*connection, *session, revision, out);
    }
    out.Flush();
}

void WsHub::FlushEvents(WsConnection& connection, WsOutbox& out)
{
    while (!connection.events.empty())
    {
        std::string text = std::move(connection.events.front().second);
        connection.events.pop_front();
        if (connection.eventsDropped > 0)
        {
            // The first event after a loss says how many were lost: {"dropped":n,...}
            text.insert(1, "\"dropped\":" + std::to_string(connection.eventsDropped) + ",");
            connection.eventsDropped = 0;
        }
        out.Message(connection, std::move(text));
    }
}

/// endregion </Driven by the loop>

/// region <Internals>

size_t WsHub::Connections() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _connections.size();
}

double WsHub::TimerHz() const
{
    std::lock_guard<std::mutex> lock(_timerMutex);
    return _armed ? _timerHz : 0;
}

void WsHub::UpdateTimer()
{
    double hz = 0;
    bool instances = false;
    bool events = false;
    bool activity = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& [client, connection] : _connections)
        {
            if (connection->closing)
                continue;
            instances = instances || connection->topics.count(kInstances) != 0;
            events = events || std::any_of(connection->topics.begin(), connection->topics.end(), IsEventTopic);
            if (!connection->topics.empty())
                hz = std::max(hz, connection->topicHz);
            for (const auto& [id, session] : connection->surfaces)
            {
                if (!session->readouts.empty())
                    hz = std::max(hz, session->hz);
                activity = activity || session->activity;
            }
        }
    }
    _subscribed.store(hz > 0, std::memory_order_relaxed);
    _instancesSubscribed.store(instances, std::memory_order_relaxed);
    _eventsSubscribed.store(events, std::memory_order_relaxed);
    _activitySubscribed.store(activity, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(_timerMutex);
    if (hz == _timerHz && _armed == (hz > 0))
        return;
    if (_armed && _timer.disarm)
        _timer.disarm(_timerId);
    _armed = false;
    _timerHz = hz;
    if (hz > 0 && _timer.arm)
    {
        _timerId = _timer.arm(1.0 / hz, [this] { Tick(); });
        _armed = true;
    }
}

void WsHub::PostTask(Task task)
{
    Post post;
    {
        std::lock_guard<std::mutex> lock(_postMutex);
        post = _post;
    }
    if (post)
        post(std::move(task));
    else
        task();
}

std::chrono::steady_clock::time_point WsHub::Now() const
{
    return _clock ? _clock() : std::chrono::steady_clock::now();
}

actions::ActionRegistry& WsHub::Registry() const
{
    return _registry != nullptr ? *_registry : actions::ActionRegistry::Instance();
}

void WsHub::BeginDelivery()
{
    std::lock_guard<std::mutex> lock(_flightMutex);
    _inFlight++;
}

void WsHub::EndDelivery()
{
    std::lock_guard<std::mutex> lock(_flightMutex);
    if (--_inFlight == 0)
        _flightDone.notify_all();
}

/// endregion </Internals>
