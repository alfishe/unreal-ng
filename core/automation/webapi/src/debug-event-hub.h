#pragma once

// The WebAPI's debugger events (docs/inprogress/2026-09-28-debugger-model/protocol.md §5): a client of
// /api/v1/websocket subscribes to topics of one emulator or of all of them and is sent an event for every
// pause, resume, finished step and breakpoint set change, instead of polling. Drogon-free (the WebSocket
// controller hands each connection's messages and a send function in), so core-tests can drive it directly

#include <json/json.h>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

class Message;

class DebugEventHub
{
public:
    /// Sends one text message to a client (thread-safe; a closed client ignores it)
    using Send = std::function<void(const std::string& text)>;

    static DebugEventHub& Instance();

    DebugEventHub() = default;
    ~DebugEventHub();
    DebugEventHub(const DebugEventHub&) = delete;
    DebugEventHub& operator=(const DebugEventHub&) = delete;

    /// Follow the core's notifications (idempotent): NC_EMULATOR_STATE_CHANGE, NC_EXECUTION_CPU_STEP,
    /// NC_BREAKPOINTS_CHANGED on the default MessageCenter
    void Start();
    void Stop();

    /// One message from a client: {"op":"subscribe","id":"s1","emulator":"<id>","topics":["debug"]} or
    /// {"op":"unsubscribe","id":"s1"}. Replies ("subscribed" with the emulator's current seq, "unsubscribed",
    /// "error") and later the events go through `send`. `client` identifies the connection
    void HandleMessage(const void* client, const std::string& text, const Send& send);
    /// The connection closed: drop its subscriptions
    void RemoveClient(const void* client);

    /// The topics this build publishes; a subscription naming others is told so in its reply
    static const std::set<std::string>& PublishedTopics();

private:
    struct Subscription
    {
        std::string id;
        std::string emulator;  // empty: every emulator
        std::set<std::string> topics;
    };
    struct Client
    {
        Send send;
        std::vector<Subscription> subscriptions;
    };

    void OnStateChange(Message* message);
    void OnCpuStep(Message* message);
    void OnBreakpointsChanged(Message* message);

    /// Numbers the event (per emulator), then sends it to every matching subscription
    void Publish(const std::string& emulatorId, const std::string& topic, Json::Value event);
    static void Reply(const Send& send, const Json::Value& reply);

    std::mutex _mutex;
    std::map<const void*, Client> _clients;
    std::map<std::string, uint64_t> _seq;  // the last event number per emulator
    std::vector<std::pair<std::string, uint64_t>> _observers;  // topic, observer id
};
