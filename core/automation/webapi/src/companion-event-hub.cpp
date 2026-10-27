#include "companion-event-hub.h"

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/platform.h"

#include <set>
#include <sstream>

namespace
{
std::string CompactJson(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

Json::Value Error(const std::string& error, const std::string& message)
{
    Json::Value out;
    out["op"] = "error";
    out["error"] = error;
    out["message"] = message;
    return out;
}

/// What can change an instance's entry in `me`: its state, who holds it, the companions, the instance list
const char* const kTriggers[] = {
    NC_EMULATOR_STATE_CHANGE,    NC_RUN_CONTROL_CHANGED,         NC_COMPANION_CHANGED,
    NC_EMULATOR_INSTANCE_CREATED, NC_EMULATOR_INSTANCE_DESTROYED, NC_EMULATOR_SELECTION_CHANGED,
    NC_FEATURE_CHANGED,
};
}  // namespace

CompanionEventHub& CompanionEventHub::Instance()
{
    static CompanionEventHub hub;
    return hub;
}

CompanionEventHub::~CompanionEventHub()
{
    Stop();
}

void CompanionEventHub::SetSnapshot(Snapshot snapshot)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _snapshot = std::move(snapshot);
}

void CompanionEventHub::Start()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_observers.empty())
        return;
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    for (const char* topic : kTriggers)
        _observers.emplace_back(topic, messageCenter.AddObserver(topic, [this](int, Message*) { Refresh(); }));
}

void CompanionEventHub::Stop()
{
    std::vector<std::pair<std::string, uint64_t>> observers;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        observers.swap(_observers);
    }
    if (observers.empty())
        return;
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    for (const auto& [topic, id] : observers)
        messageCenter.RemoveObserverById(topic, id);
}

void CompanionEventHub::Open(const void* client, const std::string& token, Send send, Close close)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Client& entry = _clients[client];
        entry.send = std::move(send);
        entry.close = std::move(close);
    }
    if (!token.empty())
        Authenticate(client, token);
}

void CompanionEventHub::HandleMessage(const void* client, const std::string& text)
{
    Send send;
    bool authenticated = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _clients.find(client);
        if (it == _clients.end())
            return;
        send = it->second.send;
        authenticated = it->second.authenticated;
    }

    Json::Value request;
    Json::CharReaderBuilder reader;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(reader, stream, &request, &errors) || !request.isObject())
    {
        if (send)
            send(CompactJson(Error("bad_request", "not a JSON object: " + errors)));
        return;
    }

    const std::string op = request.get("op", "").asString();
    if (op == "auth")
    {
        if (authenticated)
        {
            if (send)
                send(CompactJson(Error("bad_request", "this connection is authenticated already")));
            return;
        }
        Authenticate(client, request.get("token", "").asString());
        return;
    }
    if (!authenticated)
    {
        if (send)
            send(CompactJson(Error("companion_required", "authenticate first: {\"op\":\"auth\",\"token\":\"unc_...\"} "
                                                         "or Authorization: Bearer <token> on the handshake")));
        return;
    }
    if (op == "snapshot")
    {
        SendSnapshot(client, "snapshot");
        return;
    }
    if (send)
        send(CompactJson(Error("bad_request", "unknown op '" + op + "' (auth, snapshot)")));
}

void CompanionEventHub::Remove(const void* client)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _clients.erase(client);
}

size_t CompanionEventHub::Connections()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _clients.size();
}

void CompanionEventHub::Authenticate(const void* client, const std::string& token)
{
    const std::optional<companion::CompanionInfo> companion =
        token.empty() ? std::nullopt : companion::CompanionRegistry::Instance().Authenticate(token);
    Send send;
    Close close;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _clients.find(client);
        if (it == _clients.end())
            return;
        if (companion)
        {
            it->second.authenticated = true;
            it->second.companion = *companion;
        }
        else
        {
            send = it->second.send;
            close = it->second.close;
        }
    }
    if (companion)
    {
        SendSnapshot(client, "hello");
        return;
    }
    // An unknown token ends the connection: the client pairs again (as a 401 tells it on HTTP)
    if (send)
        send(CompactJson(Error("companion_unknown", "this token is not known or was revoked: pair again")));
    if (close)
        close();
}

void CompanionEventHub::SendSnapshot(const void* client, const char* op)
{
    std::lock_guard<std::mutex> refresh(_refreshMutex);
    companion::CompanionInfo companion;
    Snapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _clients.find(client);
        if (it == _clients.end() || !it->second.authenticated)
            return;
        companion = it->second.companion;
        snapshot = _snapshot;
    }
    Json::Value me = snapshot ? snapshot(companion) : Json::Value(Json::objectValue);

    Send send;
    std::string text;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _clients.find(client);
        if (it == _clients.end())
            return;
        Client& entry = it->second;
        entry.sent.clear();
        for (const Json::Value& emulator : me["emulators"])
            entry.sent[emulator["id"].asString()] = CompactJson(emulator);
        me["op"] = op;
        me["seq"] = static_cast<Json::UInt64>(++entry.seq);
        send = entry.send;
        text = CompactJson(me);
    }
    if (send)
        send(text);
}

void CompanionEventHub::Refresh()
{
    std::lock_guard<std::mutex> refresh(_refreshMutex);
    std::vector<std::pair<const void*, companion::CompanionInfo>> targets;
    Snapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& [client, entry] : _clients)
            if (entry.authenticated)
                targets.emplace_back(client, entry.companion);
        snapshot = _snapshot;
    }
    if (targets.empty())
        return;

    // Built outside the lock: `me` reads every instance
    std::vector<std::optional<Json::Value>> views;
    views.reserve(targets.size());
    for (const auto& [client, companion] : targets)
    {
        if (!companion::CompanionRegistry::Instance().Find(companion.id))
            views.emplace_back(std::nullopt);  // revoked
        else
            views.emplace_back(snapshot ? snapshot(companion) : Json::Value(Json::objectValue));
    }

    std::vector<std::pair<Send, std::string>> messages;
    std::vector<Close> closes;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (size_t i = 0; i < targets.size(); ++i)
        {
            auto it = _clients.find(targets[i].first);
            if (it == _clients.end())
                continue;  // closed meanwhile
            Client& entry = it->second;
            if (!views[i])
            {
                Json::Value revoked;
                revoked["op"] = "revoked";
                revoked["message"] = "the user revoked this companion: pair again to use it";
                messages.emplace_back(entry.send, CompactJson(revoked));
                closes.push_back(entry.close);
                entry.authenticated = false;
                continue;
            }
            std::set<std::string> present;
            for (const Json::Value& emulator : (*views[i])["emulators"])
            {
                const std::string id = emulator["id"].asString();
                present.insert(id);
                std::string text = CompactJson(emulator);
                auto& last = entry.sent[id];
                if (last == text)
                    continue;
                last = std::move(text);
                Json::Value event;
                event["op"] = "emulator";
                event["seq"] = static_cast<Json::UInt64>(++entry.seq);
                event["emulator"] = emulator;
                messages.emplace_back(entry.send, CompactJson(event));
            }
            for (auto sent = entry.sent.begin(); sent != entry.sent.end();)
            {
                if (present.count(sent->first))
                {
                    ++sent;
                    continue;
                }
                Json::Value event;
                event["op"] = "emulator_gone";
                event["seq"] = static_cast<Json::UInt64>(++entry.seq);
                event["id"] = sent->first;
                messages.emplace_back(entry.send, CompactJson(event));
                sent = entry.sent.erase(sent);
            }
        }
    }
    for (const auto& [send, text] : messages)
        if (send)
            send(text);
    for (const Close& close : closes)
        if (close)
            close();
}
