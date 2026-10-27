#include "debug-event-hub.h"

#include "3rdparty/message-center/messagecenter.h"
#include "common/statenode_json.h"
#include "debugger/callbacks/callbackcontrol.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

#include <sstream>

namespace
{
constexpr const char* kTopicDebug = "debug";
constexpr const char* kTopicAsmSync = "asm_sync";   // the asm-synchronizer's watch (NC_ASM_SYNC)

std::string CompactJson(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

/// A stop's breakpoint fields (PauseEvent, protocol §3.16)
void AddBreakpointFields(Json::Value& event, uint16_t breakpointId, uint16_t address, BreakpointHitKind kind)
{
    event["breakpoint_id"] = breakpointId;
    event["address"] = address;
    event["access"] = BreakpointHitKindName(kind);
}
}  // namespace

DebugEventHub& DebugEventHub::Instance()
{
    static DebugEventHub hub;
    return hub;
}

DebugEventHub::~DebugEventHub()
{
    Stop();
}

const std::set<std::string>& DebugEventHub::PublishedTopics()
{
    static const std::set<std::string> topics{kTopicDebug, kTopicAsmSync};
    return topics;
}

void DebugEventHub::Start()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_observers.empty())
        return;
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    _observers.emplace_back(NC_EMULATOR_STATE_CHANGE,
                            messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE,
                                                      [this](int, Message* message) { OnStateChange(message); }));
    _observers.emplace_back(NC_EXECUTION_CPU_STEP,
                            messageCenter.AddObserver(NC_EXECUTION_CPU_STEP,
                                                      [this](int, Message* message) { OnCpuStep(message); }));
    _observers.emplace_back(NC_BREAKPOINTS_CHANGED,
                            messageCenter.AddObserver(NC_BREAKPOINTS_CHANGED,
                                                      [this](int, Message* message) { OnBreakpointsChanged(message); }));
    _observers.emplace_back(NC_SCRIPT_CALLBACKS_CHANGED,
                            messageCenter.AddObserver(NC_SCRIPT_CALLBACKS_CHANGED, [this](int, Message* message) {
                                OnScriptCallbacksChanged(message);
                            }));
    _observers.emplace_back(NC_ASM_SYNC,
                            messageCenter.AddObserver(NC_ASM_SYNC, [this](int, Message* message) { OnAsmSync(message); }));
}

void DebugEventHub::Stop()
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

void DebugEventHub::Reply(const Send& send, const Json::Value& reply)
{
    if (send)
        send(CompactJson(reply));
}

void DebugEventHub::HandleMessage(const void* client, const std::string& text, const Send& send)
{
    Json::Value request;
    Json::CharReaderBuilder reader;
    std::string errors;
    std::istringstream stream(text);
    if (!Json::parseFromStream(reader, stream, &request, &errors) || !request.isObject())
    {
        Json::Value error;
        error["op"] = "error";
        error["message"] = "not a JSON object: " + errors;
        Reply(send, error);
        return;
    }

    const std::string op = request.get("op", "").asString();
    const std::string id = request.get("id", "").asString();
    if (op == "subscribe")
    {
        Subscription subscription;
        subscription.id = id;
        subscription.emulator = request.get("emulator", "").asString();
        // No "topics": every published topic. (Read through a const reference: operator[] on a
        // non-const value would add a null "topics" member)
        Json::Value unsupported(Json::arrayValue);
        const Json::Value& constRequest = request;
        if (!constRequest.isMember("topics"))
            subscription.topics = PublishedTopics();
        for (const Json::Value& topic : constRequest["topics"])
        {
            const std::string name = topic.asString();
            if (PublishedTopics().count(name))
                subscription.topics.insert(name);
            else
                unsupported.append(name);
        }

        Json::Value reply;
        reply["op"] = "subscribed";
        reply["id"] = id;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            Client& entry = _clients[client];
            entry.send = send;
            // The same id again replaces the earlier subscription
            auto& subscriptions = entry.subscriptions;
            for (auto it = subscriptions.begin(); it != subscriptions.end();)
                it = it->id == id ? subscriptions.erase(it) : it + 1;
            subscriptions.push_back(subscription);
            if (!subscription.emulator.empty())
            {
                auto seq = _seq.find(subscription.emulator);
                reply["seq"] = static_cast<Json::UInt64>(seq == _seq.end() ? 0 : seq->second);
            }
        }
        if (!unsupported.empty())
            reply["unsupported"] = unsupported;
        Reply(send, reply);
        return;
    }
    if (op == "unsubscribe")
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto entry = _clients.find(client);
            if (entry != _clients.end())
            {
                auto& subscriptions = entry->second.subscriptions;
                for (auto it = subscriptions.begin(); it != subscriptions.end();)
                    it = it->id == id ? subscriptions.erase(it) : it + 1;
            }
        }
        Json::Value reply;
        reply["op"] = "unsubscribed";
        reply["id"] = id;
        Reply(send, reply);
        return;
    }

    Json::Value error;
    error["op"] = "error";
    error["id"] = id;
    error["message"] = "unknown op '" + op + "' (subscribe, unsubscribe)";
    Reply(send, error);
}

void DebugEventHub::RemoveClient(const void* client)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _clients.erase(client);
}

uint64_t DebugEventHub::AddSink(Sink sink)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const uint64_t id = _nextSink++;
    _sinks.emplace(id, std::move(sink));
    return id;
}

void DebugEventHub::RemoveSink(uint64_t id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _sinks.erase(id);
}

void DebugEventHub::Publish(const std::string& emulatorId, const std::string& topic, Json::Value event)
{
    std::vector<Send> targets;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        event["op"] = "event";
        event["topic"] = topic;
        event["seq"] = static_cast<Json::UInt64>(++_seq[emulatorId]);
        event["emulator"] = emulatorId;
        event["cpu"] = "main";
        // Under the lock: a sink removed is never called again (a sink only queues)
        for (const auto& [id, sink] : _sinks)
            if (sink)
                sink(topic, event);
        for (const auto& [client, entry] : _clients)
        {
            for (const Subscription& subscription : entry.subscriptions)
            {
                if ((subscription.emulator.empty() || subscription.emulator == emulatorId) &&
                    subscription.topics.count(topic))
                {
                    targets.push_back(entry.send);
                    break;  // one copy per connection
                }
            }
        }
    }
    if (targets.empty())
        return;
    const std::string text = CompactJson(event);
    for (const Send& send : targets)
        if (send)
            send(text);
}

void DebugEventHub::OnStateChange(Message* message)
{
    auto* payload = message ? dynamic_cast<EmulatorStateChangePayload*>(message->obj) : nullptr;
    if (!payload)
        return;
    Json::Value event;
    switch (payload->_payloadNumber)
    {
        case StatePaused:
            event["event"] = "paused";
            if (payload->pauseCause == PauseCause::Breakpoint)
            {
                event["reason"] = "breakpoint";
                AddBreakpointFields(event, payload->breakpointId, payload->address, payload->hitKind);
            }
            else
                event["reason"] = "pause";
            break;
        case StateResumed:
        case StateRun:
            event["event"] = "resumed";
            break;
        default:
            return;
    }
    Publish(payload->emulatorId.toString(), kTopicDebug, event);
}

void DebugEventHub::OnCpuStep(Message* message)
{
    // Only the end of a direct run carries a payload; the other step notifications are refresh hints
    auto* payload = message ? dynamic_cast<CpuStepPayload*>(message->obj) : nullptr;
    if (!payload)
        return;
    Json::Value event;
    event["event"] = "step_done";
    event["reason"] = payload->stopped ? "breakpoint" : "step";
    if (payload->stopped)
        AddBreakpointFields(event, payload->breakpointId, payload->address, payload->hitKind);
    Publish(payload->emulatorId.toString(), kTopicDebug, event);
}

void DebugEventHub::OnBreakpointsChanged(Message* message)
{
    auto* payload = message ? dynamic_cast<BreakpointsChangedPayload*>(message->obj) : nullptr;
    if (!payload)
        return;
    Json::Value event;
    event["event"] = "breakpoints_changed";
    event["ids"] = Json::Value(Json::arrayValue);
    for (uint16_t id : payload->ids)
        event["ids"].append(id);
    Publish(payload->emulatorId.toString(), kTopicDebug, event);
}

void DebugEventHub::OnScriptCallbacksChanged(Message* message)
{
    auto* payload = message ? dynamic_cast<ScriptCallbacksChangedPayload*>(message->obj) : nullptr;
    if (!payload)
        return;
    const std::string emulatorId = payload->emulatorId.toString();
    Json::Value event;
    event["event"] = "callbacks_changed";
    event["id"] = payload->id;
    // The callback as GET /callbacks shows it now (a registration, a resume, a policy, a suspension), or removed: true
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager ? manager->GetEmulator(emulatorId) : nullptr;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    ScriptCallbackManager* callbacks =
        context && context->pDebugManager ? context->pDebugManager->GetScriptCallbacks() : nullptr;
    CallbackInfo info;
    if (payload->id == 0)
    {
        // Id 0: the manager's own state (the hooks went unpaced or back)
        if (callbacks)
            event["hooks"] = StateNodeToJson(callback_control::HooksNode(callbacks->HookStatus()));
    }
    else if (callbacks && callbacks->Get(payload->id, info))
        event["callback"] = StateNodeToJson(callback_control::InfoNode(info));
    else if (callbacks)
        event["removed"] = true;
    Publish(emulatorId, kTopicDebug, event);
}

void DebugEventHub::OnAsmSync(Message* message)
{
    auto* payload = message ? dynamic_cast<AsmSyncPayload*>(message->obj) : nullptr;
    if (!payload)
        return;
    Json::Value event;
    event["event"] = "asm_sync_" + payload->event;
    event["assembler"] = payload->assembler;
    if (payload->event == "built")
    {
        event["generation"] = static_cast<Json::UInt64>(payload->generation);
        event["complete"] = payload->complete;
        event["labels"] = payload->labels;
        event["hints"] = payload->hints;
    }
    Publish(payload->emulatorId.toString(), kTopicAsmSync, event);
}
