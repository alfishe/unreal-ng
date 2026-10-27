#pragma once

// Companion events (docs/inprogress/2026-10-16-companion-client/protocol.md §4.1): a companion connected to
// /api/v1/companions/events is sent its `me` once, then each instance's entry (state, control, actions) whenever it
// changes, instead of polling `me`. Drogon-free (the WebSocket controller hands each connection's messages, a send and
// a close function in), so core-tests drive it directly

#include <json/json.h>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "emulator/execution/companionregistry.h"

class CompanionEventHub
{
public:
    /// Sends one text message to a client (thread-safe; a closed client ignores it)
    using Send = std::function<void(const std::string& text)>;
    /// Closes the client's connection
    using Close = std::function<void()>;
    /// The `me` body of a companion: {companion, emulators: [{id, state, control, actions, ...}]}
    using Snapshot = std::function<Json::Value(const companion::CompanionInfo& companion)>;

    static CompanionEventHub& Instance();

    CompanionEventHub() = default;
    ~CompanionEventHub();
    CompanionEventHub(const CompanionEventHub&) = delete;
    CompanionEventHub& operator=(const CompanionEventHub&) = delete;

    /// How `me` is built (the WebAPI's own, tests their fake)
    void SetSnapshot(Snapshot snapshot);

    /// Follow the core's notifications (idempotent): every one that can change an instance's entry refreshes
    void Start();
    void Stop();

    /// A new connection. `token`: the handshake's bearer token; empty when the client sends {"op":"auth"} instead
    void Open(const void* client, const std::string& token, Send send, Close close);
    /// One message from a client: {"op":"auth","token":"unc_..."} or {"op":"snapshot"}
    void HandleMessage(const void* client, const std::string& text);
    /// The connection closed
    void Remove(const void* client);

    /// Rebuilds every companion's view and sends each instance entry that changed; a revoked companion is told and
    /// closed. The notifications call it; the WebAPI also once a second, for what changes with time alone (a re-take
    /// cooldown running out, the first recorded frame of time travel)
    void Refresh();

    /// Connections, authenticated or not (tests)
    size_t Connections();

private:
    struct Client
    {
        Send send;
        Close close;
        bool authenticated = false;
        companion::CompanionInfo companion;
        uint64_t seq = 0;
        std::map<std::string, std::string> sent;  // instance id -> its entry as last sent (compact JSON)
    };

    void Authenticate(const void* client, const std::string& token);
    /// Sends the whole view ("hello" after authentication, "snapshot" on request) and remembers it
    void SendSnapshot(const void* client, const char* op);

    std::mutex _mutex;
    std::mutex _refreshMutex;  // one refresh at a time: notifications and the timer may overlap
    std::map<const void*, Client> _clients;
    Snapshot _snapshot;
    std::vector<std::pair<std::string, uint64_t>> _observers;  // topic, observer id
};
