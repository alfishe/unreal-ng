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

/// A stop's breakpoint fields (PauseEvent, protocol §3.16): the CPU whose breakpoint it was, and for a card the
/// instruction of the access (the main CPU's own position is in the registers)
void AddBreakpointFields(Json::Value& event, uint16_t breakpointId, uint16_t address, BreakpointHitKind kind,
                         const std::string& cpu, uint16_t cpuPc)
{
    event["cpu"] = cpu;
    event["breakpoint_id"] = breakpointId;
    event["address"] = address;
    event["access"] = BreakpointHitKindName(kind);
    if (cpu != "main")
        event["cpu_pc"] = cpuPc;
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
    _observers.emplace_back(NC_FRAME_WATCH,
                            messageCenter.AddObserver(NC_FRAME_WATCH, [this](int, Message* message) { OnFrameWatch(message); }));
    _observers.emplace_back(NC_SECTION_TIMER, messageCenter.AddObserver(NC_SECTION_TIMER, [this](int, Message* message) {
                                OnSectionTimer(message);
                            }));
    _observers.emplace_back(NC_DEBUG_EDITED,
                            messageCenter.AddObserver(NC_DEBUG_EDITED, [this](int, Message* message) { OnEdited(message); }));
    _observers.emplace_back(NC_RUN_CONTROL_CHANGED,
                            messageCenter.AddObserver(NC_RUN_CONTROL_CHANGED,
                                                      [this](int, Message* message) { OnRunControl(message); }));
    _observers.emplace_back(NC_LABEL_CHANGED,
                            messageCenter.AddObserver(NC_LABEL_CHANGED,
                                                      [this](int, Message* message) { OnLabelsChanged(message); }));
    _observers.emplace_back(NC_DEBUG_TARGETS_CHANGED,
                            messageCenter.AddObserver(NC_DEBUG_TARGETS_CHANGED,
                                                      [this](int, Message* message) { OnTargetsChanged(message); }));
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
        if (!event.isMember("cpu"))
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
                AddBreakpointFields(event, payload->breakpointId, payload->address, payload->hitKind, payload->cpu,
                                    payload->cpuPc);
            }
            else if (payload->pauseCause == PauseCause::Step)
            {
                // Another CPU's step ended (a sound card's): that CPU and its instruction
                event["reason"] = "step";
                event["cpu"] = payload->cpu;
                event["cpu_pc"] = payload->cpuPc;
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
        AddBreakpointFields(event, payload->breakpointId, payload->address, payload->hitKind, payload->cpu,
                            payload->cpuPc);
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

void DebugEventHub::OnFrameWatch(Message* message)
{
    auto* payload = message ? dynamic_cast<FrameWatchPayload*>(message->obj) : nullptr;
    if (!payload)
        return;
    // watch_hit (a running machine got there: frame, reasons, paused), watch_added, watch_removed, watch_error
    Json::Value event;
    event["event"] = "watch_" + payload->event;
    event["id"] = payload->id;
    if (!payload->label.empty())
        event["label"] = payload->label;
    if (payload->event == "hit" || payload->event == "error")
        event["frame"] = static_cast<Json::UInt64>(payload->frame);
    if (payload->event == "hit")
    {
        event["reasons"] = Json::Value(Json::arrayValue);
        for (const std::string& reason : payload->reasons)
            event["reasons"].append(reason);
        if (!payload->textLine.empty())
            event["text_line"] = payload->textLine;
        event["paused"] = payload->paused;
        event["removed"] = payload->removed;
    }
    if (!payload->error.empty())
        event["error"] = payload->error;
    Publish(payload->emulatorId.toString(), kTopicDebug, event);
}

void DebugEventHub::OnSectionTimer(Message* message)
{
    auto* payload = message ? dynamic_cast<SectionTimerPayload*>(message->obj) : nullptr;
    if (!payload)
        return;
    // section_timer_started / _stopped / _cleared; section_timer_break: the pass a condition stopped the machine at
    Json::Value event;
    event["event"] = "section_timer_" + payload->event;
    if (payload->event == "break")
    {
        event["condition"] = payload->condition;
        event["section"] = payload->section;
        event["frame"] = static_cast<Json::UInt64>(payload->frame);
        event["t"] = payload->t;
        event["elapsed"] = static_cast<Json::UInt64>(payload->elapsed);
        event["net"] = static_cast<Json::UInt64>(payload->net);
        event["interrupts"] = payload->interrupts;
    }
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

void DebugEventHub::OnEdited(Message* message)
{
    auto* payload = message ? dynamic_cast<DebugEditedPayload*>(message->obj) : nullptr;
    if (!payload)
        return;
    // A tool changed the machine (registers, memory, a page, a port, other state): views re-read what they show.
    // The edits since the last event come merged: a script writing byte by byte is a few events with counts
    const std::string emulatorId = payload->emulatorId.toString();
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager ? manager->GetEmulator(emulatorId) : nullptr;
    if (!emulator)
        return;
    const ToolEditSummary edits = emulator->TakeToolEdits();
    if (edits.count == 0)
        return;  // an earlier event took them
    Json::Value event;
    event["event"] = "edited";
    event["source"] = edits.source;
    event["cpu"] = edits.cpu.empty() ? std::string("main") : edits.cpu;
    event["what"] = edits.what;
    event["count"] = edits.count;
    if (!edits.page.empty())
        event["page"] = edits.page;
    if (edits.address >= 0)
        event["address"] = edits.address;
    if (edits.length > 0)
        event["length"] = edits.length;
    Publish(emulatorId, kTopicDebug, event);
}

void DebugEventHub::OnRunControl(Message* message)
{
    auto* payload = message ? dynamic_cast<RunControlChangedPayload*>(message->obj) : nullptr;
    if (!payload)
        return;
    // The run-control claim changed: taken, released, revoked, expired, retake_refused, superseded
    Json::Value event;
    event["event"] = "run_control";
    event["change"] = payload->event;
    event["surface"] = payload->surfaceLabel;
    if (!payload->client.empty())
        event["client"] = payload->client;
    if (!payload->ownerUuid.empty())
        event["owner"] = payload->ownerUuid;
    if (!payload->by.empty())
        event["by"] = payload->by;
    Publish(payload->emulatorId.toString(), kTopicDebug, event);
}

void DebugEventHub::OnLabelsChanged(Message* message)
{
    // A label manager without an instance (a standalone one in a tool) has nothing to name
    auto* payload = message ? dynamic_cast<LabelsChangedPayload*>(message->obj) : nullptr;
    if (!payload || payload->emulatorId.isNil())
        return;
    Json::Value event;
    event["event"] = "labels_changed";
    Publish(payload->emulatorId.toString(), kTopicDebug, event);
}

void DebugEventHub::OnTargetsChanged(Message* message)
{
    auto* payload = message ? dynamic_cast<DebugTargetsChangedPayload*>(message->obj) : nullptr;
    if (!payload)
        return;
    Json::Value event;
    event["event"] = "targets_changed";
    event["targets"] = Json::Value(Json::arrayValue);
    for (const auto& [cpu, available] : payload->targets)
    {
        Json::Value target;
        target["cpu"] = cpu;
        target["available"] = available;
        event["targets"].append(target);
    }
    Publish(payload->emulatorId.toString(), kTopicDebug, event);
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
