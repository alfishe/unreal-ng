#include "ws-hub.h"

#include <algorithm>
#include <optional>

#include "3rdparty/message-center/messagecenter.h"
#include "debug-event-hub.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdcursorcell.h"
#include "common/statenode_json.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/activityfeed.h"
#include "emulator/actions/contextstate.h"
#include "emulator/actions/readouts.h"
#include "emulator/actions/targetstate.h"
#include "emulator/actions/steppingactions.h"
#include "emulator/companions/streamdecksupervisor.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/companionactions.h"
#include "emulator/monitoring/clientstats.h"
#include "emulator/monitoring/monitoringapi.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/readoutschanged.h"

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

/// Besides kStaleEvent / kStaleFrame: the Meter values alone (read on time while subscribed)
constexpr uint8_t kWantMeter = 4;

/// `deadline` moved to `at` when that is earlier
void Earliest(std::optional<std::chrono::steady_clock::time_point>& deadline, std::chrono::steady_clock::time_point at)
{
    if (!deadline || at < *deadline)
        deadline = at;
}

/// What a subscription reads at `now` (kStaleEvent: every value; kStaleFrame: the Frames and Meter ones; kWantMeter:
/// the Meter ones; 0: nothing): its marks, its meters, once its interval since the last read passed (a full send
/// at once). Waiting for the interval: `deadline` moved to its end; read with meters: to the next meter read
uint8_t Want(bool full, uint8_t stale, bool meters, std::chrono::steady_clock::time_point readAt, double hz,
             std::chrono::steady_clock::time_point now, std::optional<std::chrono::steady_clock::time_point>& deadline)
{
    const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(DueAfter(hz));
    const uint8_t want = full ? WsHub::kStaleEvent : static_cast<uint8_t>(stale | (meters ? kWantMeter : 0));
    if (want == 0)
        return 0;
    if (full || now - readAt >= interval)
    {
        if (meters)
            Earliest(deadline, now + interval);
        return want;
    }
    Earliest(deadline, readAt + interval);
    return 0;
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
    NC_COMPANION_CHANGED,          NC_EMULATOR_INSTANCE_CREATED, NC_EMULATOR_INSTANCE_DESTROYED,
    NC_EMULATOR_SELECTION_CHANGED, NC_SURFACE_TARGET,  // a view got focus or changed its active instance
};
/// What changes a state topic's value or an `instances` entry: the `instances` view is refreshed at once
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
    return name == kStreamDeck || monitoring::IsProcessName(name);
}

/// What can change a value without an emulated frame: every value of the instances is read again (a coalesced tick).
/// Not here: the per-access ones (memory pages, the FDC's registers), which only change while frames run
const char* const kChangeTriggers[] = {
    NC_EMULATOR_STATE_CHANGE,  NC_FEATURE_CHANGED,      NC_RUN_CONTROL_CHANGED,   NC_RUN_PACE,
    NC_SPEED_CHANGED,          NC_CPU_FREQ_CHANGED,     NC_SYSTEM_RESET,          NC_BREAKPOINTS_CHANGED,
    NC_BREAKPOINT_CHANGED,     NC_EXECUTION_BREAKPOINT, NC_SCRIPT_CALLBACKS_CHANGED, NC_LABEL_CHANGED,
    NC_MEDIA_INSERTED,         NC_MEDIA_EJECTED,        NC_MEDIA_DIRTY,           NC_MEDIA_CLEAN,
    NC_MEDIA_SAVED,            NC_MEDIA_EXPORTED,       NC_FDD_DISK_INSERTED,     NC_FDD_DISK_EJECTED,
    NC_FDD_DISK_PENDING_WRITE, NC_FDD_DISK_WRITTEN,     NC_FDD_MOTOR_STARTED,     NC_FDD_MOTOR_STOPPED,
    NC_FILE_LOADED,            NC_RZX_PLAYBACK,         NC_RECORDING_STATE,       NC_CONTROLLER_CONNECTOR_CHANGED,
    NC_VIDEO_MODE_CHANGED,     NC_VIDEO_VIEWPORT_CHANGED, NC_SURFACE_CONTEXT,     NC_SURFACE_TARGET,
    NC_ACTION_CATALOGUE,       NC_EMULATOR_INSTANCE_CREATED, NC_EMULATOR_INSTANCE_DESTROYED,
    NC_EMULATOR_SELECTION_CHANGED, NC_COMPANION_CHANGED,     NC_CONTROLLER_DEVICES_CHANGED,
};

std::string TopicList()
{
    std::string list;
    for (const std::string& name : WsHub::TopicNames())
        list += (list.empty() ? "" : ", ") + name;
    return list;
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
    SetFrameReadersWanted(false);  // nobody left to read a frame's state
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
        _timerDelay = 0;
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
                                    WakeInstances();
                                })));
    // Every value may have changed: many changes in a row (a symbol file's labels, a disk being written) are one tick
    for (const char* topic : kChangeTriggers)
        _observers.emplace_back(topic, center.AddObserver(topic, ObserverCallbackFunc([this](int, Message*) {
                                    OnChange("");
                                })));
    // Published state no other notification tells (readoutschanged.h): its instance, or every one
    _observers.emplace_back(NC_READOUTS_CHANGED,
                            center.AddObserver(NC_READOUTS_CHANGED, ObserverCallbackFunc([this](int, Message* message) {
                                auto* payload = message != nullptr
                                                    ? dynamic_cast<ReadoutsChangedPayload*>(message->obj)
                                                    : nullptr;
                                if (payload != nullptr && payload->area == kFrameArea)
                                {
                                    OnFrame(payload->emulatorId);  // a frame's state written: its Frames values
                                    return;
                                }
                                OnChange(payload != nullptr ? payload->emulatorId : std::string());
                                // Time travel's state decides rewind and the speed lock of an `instances` entry
                                if (payload != nullptr && payload->area.compare(0, 3, "ttd") == 0)
                                    WakeInstances();
                            })));
    // Emulated time moved (a frame, a TTD seek's frame, a step): the instance's Frames values
    _observers.emplace_back(NC_VIDEO_FRAME_REFRESH,
                            center.AddObserver(NC_VIDEO_FRAME_REFRESH, ObserverCallbackFunc([this](int, Message* message) {
                                if (!_subscribed.load(std::memory_order_relaxed))
                                    return;
                                auto* payload = message != nullptr
                                                    ? dynamic_cast<EmulatorFramePayload*>(message->obj)
                                                    : nullptr;
                                OnFrame(payload != nullptr ? payload->_emulatorId.toString() : std::string());
                            })));
    _observers.emplace_back(NC_EXECUTION_CPU_STEP,
                            center.AddObserver(NC_EXECUTION_CPU_STEP, ObserverCallbackFunc([this](int, Message* message) {
                                if (!_subscribed.load(std::memory_order_relaxed))
                                    return;
                                auto* payload = message != nullptr ? dynamic_cast<CpuStepPayload*>(message->obj)
                                                                   : nullptr;
                                OnFrame(payload != nullptr ? payload->emulatorId.toString() : std::string());
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
    // An action ran: what it changed is read again (its instance; "" when it named none)
    _activityListener = actions::ActivityFeed::Instance().AddListener([this](const actions::ActivityEntry& entry) {
        OnChange(entry.instance);
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
    if (op != "subscribe" && op != "unsubscribe" && op != "snapshot" && op != "join" && op != "action" && op != "pin" &&
        op != "bye")
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
        else if (op == "pin")
            _surface.Pin(*connection, message, out);
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
                               "debug.asm_sync, audio.mixer, host.stats, emulator.stats, ...) or readouts[] (the "
                               "surface topic, after join)");
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
                matched = true;
                // A prefix from a peer without a token ("debug.*" from this host, as before the monitoring topics
                // came) takes the event topics it may have, not an error for the state topics it may not
                if (pattern != name && !IsEventTopic(name) && connection.token.empty() && !connection.companion)
                    continue;
                names.insert(name);
            }
        if (!matched)
            out.Error(connection, pattern.empty() ? std::string("ws") : pattern, "Not Found",
                      "no topic '" + pattern + "' (" + TopicList() + ")");
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
    static const std::vector<std::string> names = [] {
        std::vector<std::string> all = StateTopicNames();
        all.insert(all.end(), {kInstances, "debug.events", "debug.asm_sync"});
        return all;
    }();
    return names;
}

const std::vector<std::string>& WsHub::StateTopicNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> all = {"run.state", "ttd.position", "surfaces.context", kStreamDeck, "audio.mixer"};
        // Monitoring (monitoring::ProcessNames / InstanceNames: host.stats, emulator.stats, debug.breakpoints, ...)
        all.insert(all.end(), monitoring::ProcessNames().begin(), monitoring::ProcessNames().end());
        all.insert(all.end(), monitoring::InstanceNames().begin(), monitoring::InstanceNames().end());
        // The machine area: the readouts of the same name (machinereadouts.h), read by Read below
        all.insert(all.end(), {"media.recent", "media.rzx", "cap.record", "machine.switches", "input.connectors"});
        return all;
    }();
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
        // One producer for the topic, the surface readout and GET /api/v1/surfaces/readouts: `speed` is the host
        // setting (1..16), the composed clock (host x the machine's own turbo) is `emulator.stats` cpu.multiplier
        return StateNodeToJson(actions::ReadReadout(emulator, name));
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
        value["unit"] = actions::ScrubUnitOf(instanceId);  // the scrub unit (ttd.scrub_unit, D3.2)
        value["unit_count"] = static_cast<Json::Int64>(actions::ScrubCountOf(instanceId));
        return value;
    }
    if (name == "audio.mixer")
    {
        // The mixer's levels and mute / solo of every row and channel (the readout of the same name): latest value,
        // sent when it changed, at the subscriber's max_hz
        return StateNodeToJson(actions::ReadReadout(emulator, name));
    }
    if (monitoring::IsInstanceName(name))
        return StateNodeToJson(monitoring::ReadInstance(name, emulator));
    if (monitoring::IsProcessName(name))
        return StateNodeToJson(monitoring::ReadProcess(name));  // a surface readout of the process's value
    if (name == "surfaces.context")
    {
        const actions::ContextInfo info = actions::ContextState::Instance().Get(instanceId, &emulator);
        value["context"] = info.context;
        value["set"] = info.set;
        value["by"] = info.set ? Json::Value(info.by) : Json::Value(Json::nullValue);
        value["default"] = info.fallback;
        return value;
    }
    // The other state topics are readouts of the same name (published state only)
    return StateNodeToJson(actions::ReadReadout(emulator, name));
}

std::string WsHub::FollowedInstance()
{
    // The one rule of every surface (targeting.md §3): the active instance of the front view, else the selected
    // instance, else the sole one - the instance `instances.followed` and GET /api/v1/surfaces/target name
    return actions::TargetState::Instance().Followed();
}

/// endregion </Topics>

/// region <Driven by the loop>

void WsHub::Tick()
{
    WsOutbox out;
    bool refreshInstances = false;
    bool drained = false;
    std::optional<std::chrono::steady_clock::time_point> deadline;
    std::chrono::steady_clock::time_point now;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        now = Now();
        ApplyStale();
        TopicDue topicDue;
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
                    monitoring::ClientStats::Instance().Dropped(monitoring::ClientPlane::Ws);
                    continue;
                }
                if (connection->probe)
                    out.after.push_back(connection->probe);
                // Measured again soon: it drains (its values then) or stays (a slow consumer)
                Earliest(deadline, now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                             DueAfter(std::max(connection->topicHz, kDefaultHz))));
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
            if (states)
            {
                const bool meters = std::any_of(connection->topics.begin(), connection->topics.end(),
                                                [](const std::string& name) {
                                                    return IsStateTopic(name) && Cadence(name) == ReadoutCadence::Meter;
                                                });
                const uint8_t want = Want(connection->topicFull, connection->topicStale, meters,
                                          connection->topicSentAt, connection->topicHz, now, deadline);
                if (want != 0)
                {
                    topicDue.emplace_back(connection.get(), want);
                    for (const std::string& name : connection->topics)
                        if (IsStateTopic(name) && WsHub::Reads(want, name))
                            topicNames.insert(name);
                }
            }
            for (auto& [id, session] : connection->surfaces)
            {
                if (session->readouts.empty())
                {
                    session->stale = 0;
                    continue;
                }
                const bool meters = std::any_of(session->readouts.begin(), session->readouts.end(),
                                                [](const std::string& name) {
                                                    return Cadence(name) == ReadoutCadence::Meter;
                                                });
                const uint8_t want =
                    Want(session->full, session->stale, meters, session->sentAt, session->hz, now, deadline);
                if (want != 0)
                    surfaceDue.push_back({connection.get(), session.get(), want});
            }
        }
        // `instances`: what changes with time alone (an idle lease running out, a re-take cooldown ending) at that
        // moment (RefreshInstances keeps it); everything else has a notification
        if (_instancesSubscribed.load(std::memory_order_relaxed))
        {
            refreshInstances = drained || (_instancesDue && now >= *_instancesDue);
            if (!refreshInstances && _instancesDue)
                Earliest(deadline, *_instancesDue);
        }
        if (!topicDue.empty() || !surfaceDue.empty())
            DeliverValues(topicDue, topicNames, surfaceDue, now, out);
    }
    out.Flush();
    ArmFor(deadline, now);
    if (refreshInstances)
        RefreshInstances();  // arms for its next timed change itself
}

void WsHub::OnChange(const std::string& instanceId)
{
    if (!_subscribed.load(std::memory_order_relaxed))
        return;
    {
        std::lock_guard<std::mutex> lock(_staleMutex);
        _stale[instanceId] |= kStaleEvent;
    }
    WakeTick();
}

void WsHub::OnFrame(const std::string& instanceId)
{
    if (!_subscribed.load(std::memory_order_relaxed))
        return;
    {
        std::lock_guard<std::mutex> lock(_staleMutex);
        // Nobody reads a value of this instance that moves with its frames: nothing to do (the common case of a
        // deck of toggles and names, of a running machine nobody watches)
        if (!_frameWatchAll && (instanceId.empty() ? _frameWatch.empty() : _frameWatch.count(instanceId) == 0))
            return;
        _stale[instanceId] |= kStaleFrame;
    }
    WakeTick();
}

void WsHub::ApplyStale()
{
    std::map<std::string, uint8_t> stale;
    {
        std::lock_guard<std::mutex> lock(_staleMutex);
        stale.swap(_stale);
    }
    if (stale.empty())
        return;
    uint8_t any = 0;
    for (const auto& [id, bits] : stale)
    {
        any |= bits;
        // An event: what the monitoring values keep for their polled readers is not what is true now
        if ((bits & kStaleEvent) != 0)
            monitoring::ForgetCached(id);
    }
    const auto every = stale.find("");
    const uint8_t everyone = every != stale.end() ? every->second : 0;
    for (auto& [client, connection] : _connections)
    {
        // A state topic carries every instance
        if (!connection->topics.empty())
            connection->topicStale |= any;
        for (auto& [id, session] : connection->surfaces)
        {
            uint8_t bits = everyone;
            if (!session->instance.empty())
                if (auto it = stale.find(session->instance); it != stale.end())
                    bits |= it->second;
            session->stale |= bits;
        }
    }
}

void WsHub::ArmFor(std::optional<std::chrono::steady_clock::time_point> deadline,
                   std::chrono::steady_clock::time_point now)
{
    std::lock_guard<std::mutex> lock(_timerMutex);
    if (deadline && _armed && _timerAt <= *deadline)
        return;  // an earlier tick comes first: it arms for this one
    if (_armed && _timer.disarm)
        _timer.disarm(_timerId);
    _armed = false;
    _timerDelay = 0;
    if (!deadline || !_timer.arm)
        return;
    const double seconds = std::max(0.0, std::chrono::duration<double>(*deadline - now).count());
    const uint64_t generation = ++_timerGeneration;
    _timerAt = *deadline;
    _timerDelay = seconds;
    _timerId = _timer.arm(seconds, [this, generation] {
        {
            std::lock_guard<std::mutex> fired(_timerMutex);
            if (_timerGeneration == generation)
            {
                _armed = false;
                _timerDelay = 0;
            }
        }
        Tick();
    });
    _armed = true;
}

void WsHub::DeliverValues(const TopicDue& topicDue, const std::set<std::string>& topicNames,
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
        _valuesRead.fetch_add(1, std::memory_order_relaxed);
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
            {
                _valuesRead.fetch_add(1, std::memory_order_relaxed);
                processValues[name] =
                    Compact(StateNodeToJson(name == kStreamDeck ? companion::streamdeck::Supervisor::Instance().Summary()
                                                                : monitoring::ReadProcess(name)));
            }
    }
    for (const SurfaceWant& due : surfaceDue)
        if (!due.session->instance.empty())
            for (const std::string& name : due.session->readouts)
                if (WsHub::Reads(due.want, name))
                    read(due.session->instance, name);

    // A topic's message of one instance is the same bytes for every subscriber: built once
    std::map<std::string, std::string> built;
    for (const auto& [connection, want] : topicDue)
    {
        std::set<std::string> present;
        bool sent = false;
        for (const std::string& id : all)
        {
            auto values = readings.find(id);
            if (values == readings.end())
                continue;
            for (const std::string& topic : connection->topics)
            {
                if (!IsStateTopic(topic) || !WsHub::Reads(want, topic))
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
            if (value == processValues.end() || !WsHub::Reads(want, topic))
                continue;
            present.insert(topic);
            std::string& last = connection->topicSent[topic];
            if (!connection->topicFull && last == value->second)
                continue;
            last = value->second;
            out.Message(*connection, "{\"t\":\"" + topic + "\",\"op\":\"state\",\"value\":" + value->second + "}");
            sent = true;
        }
        // An instance that went: only a read of every value tells (its going is an event)
        if ((want & kStaleEvent) != 0)
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
        connection->topicStale = 0;
        if (sent)
            connection->topicSentAt = now;  // the rate bounds what is sent; a read that sent nothing holds nothing back
    }
    for (const SurfaceWant& due : surfaceDue)
        _surface.Deliver(*due.connection, *due.session, readings, due.want, now, out);
}

void WsHub::Refresh()
{
    WsOutbox out;
    {
        std::lock_guard<std::mutex> lock(_mutex);
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
                (void)_surface.Rebind(*connection, *session, out);
        }
    }
    out.Flush();
    UpdateTimer();  // a rebind moved a surface to another instance: whose frames matter
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
    const std::optional<std::chrono::milliseconds> left = companion::NextTimedChange();
    std::optional<std::chrono::steady_clock::time_point> due;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto now = Now();
        if (left)
            due = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(*left);
        _instancesDue = due;
        bool congested = false;
        for (const auto& [client, connection] : _connections)
        {
            if (connection->closing || !connection->companion || connection->topics.count(kInstances) == 0)
                continue;
            if (Congested(*connection))
                congested = true;
            else
                targets.emplace_back(connection->id, *connection->companion);
        }
        // A subscriber over the mark is measured again by the tick (it drains: the view then; it stays: a slow
        // consumer). Nothing else may tick for it: `instances` alone has no beat
        if (congested)
            Earliest(due, now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(DueAfter(kDefaultHz)));
        snapshot = _snapshot;
    }
    if (targets.empty())
    {
        if (due)
            ArmFor(due, Now());
        return;
    }

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
    if (due)
        ArmFor(due, Now());  // an earlier wait stays armed (ArmFor keeps the earliest)
}

void WsHub::WakeTick()
{
    if (!_subscribed.load(std::memory_order_relaxed) || _tickPending.exchange(true))
        return;
    PostTask([this] {
        _tickPending.store(false);
        Tick();
    });
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

double WsHub::TimerDelay() const
{
    std::lock_guard<std::mutex> lock(_timerMutex);
    return _armed ? _timerDelay : 0;
}

bool WsHub::Reads(uint8_t want, const std::string& name)
{
    if ((want & kStaleEvent) != 0)
        return true;
    switch (Cadence(name))
    {
        case ReadoutCadence::Meter:
            return want != 0;
        case ReadoutCadence::Frames:
            return (want & kStaleFrame) != 0;
        case ReadoutCadence::Events:
            break;
    }
    return false;
}

WsHub::ReadoutCadence WsHub::Cadence(const std::string& name)
{
    // Changed by a notification (kChangeTriggers, NC_READOUTS_CHANGED) or an action alone: every producer of these
    // posts one, paused or not (readoutschanged.h)
    static const std::set<std::string> events = {
        "run.state",         "machine",          "audio.mute",       "debug.mode",      "debug.breakpoints",
        "debug.labels",      "debug.analyzers",  "surfaces.context", "media.recent",    "machine.previous",
        "machine.switches",  "input.connectors", "video.window",     "video.crt_params", "video.viewport",
        "video.temporal",    "actions.unavailable", "analysis.progress", "analysis.ready", "controllers.devices",
        "debug.markup",
    };
    // With wall time: read on time while subscribed
    static const std::set<std::string> meters = {"host.stats", "host.clients", kStreamDeck};
    if (events.count(name) != 0 || name.compare(0, 7, "toggle.") == 0)
        return ReadoutCadence::Events;
    if (meters.count(name) != 0)
        return ReadoutCadence::Meter;
    // The rest moves with emulated time too (the TTD cursor and status, the tape's block, a disk's motor and dirty
    // flag, the mixer's levels, the stats, the buffers, the watches, the RZX frame), and any name not listed: a new
    // value is never left stale
    return ReadoutCadence::Frames;
}

void WsHub::UpdateTimer()
{
    bool subscribed = false;
    bool instances = false;
    bool events = false;
    bool activity = false;
    bool frameAll = false;
    std::set<std::string> frames;
    auto moves = [](const std::string& name) { return Cadence(name) != ReadoutCadence::Events; };
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& [client, connection] : _connections)
        {
            if (connection->closing)
                continue;
            instances = instances || connection->topics.count(kInstances) != 0;
            events = events || std::any_of(connection->topics.begin(), connection->topics.end(), IsEventTopic);
            // The state topics (the event topics and `instances` have their own producers)
            if (std::any_of(connection->topics.begin(), connection->topics.end(), IsStateTopic))
                subscribed = true;
            frameAll = frameAll || std::any_of(connection->topics.begin(), connection->topics.end(),
                                               [&](const std::string& name) { return IsStateTopic(name) && moves(name); });
            for (const auto& [id, session] : connection->surfaces)
            {
                if (!session->readouts.empty())
                    subscribed = true;
                if (!session->instance.empty() &&
                    std::any_of(session->readouts.begin(), session->readouts.end(), moves))
                    frames.insert(session->instance);
                activity = activity || session->activity;
            }
        }
    }
    // The frame's publishers post only while somebody reads what they publish
    SetFrameReadersWanted(frameAll || !frames.empty());
    {
        std::lock_guard<std::mutex> lock(_staleMutex);
        _frameWatch = std::move(frames);
        _frameWatchAll = frameAll;
    }
    _subscribed.store(subscribed, std::memory_order_relaxed);
    _instancesSubscribed.store(instances, std::memory_order_relaxed);
    _eventsSubscribed.store(events, std::memory_order_relaxed);
    _activitySubscribed.store(activity, std::memory_order_relaxed);
    // Nobody left to wait for: no timer (a subscription's own tick arms it again)
    if (!subscribed && !instances)
        ArmFor(std::nullopt, Now());
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
