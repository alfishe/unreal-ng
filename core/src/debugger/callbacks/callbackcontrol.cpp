#include "callbackcontrol.h"

#include "debugger/callbacks/callbacktext.h"

namespace callback_control
{
namespace
{
/// callback_text's reader over an InfoNode
struct NodeReader
{
    const StateNode& node;

    std::string Text(const char* key) const
    {
        const StateNode* value = node.find(key);
        return value != nullptr && value->kind == StateNode::Kind::String ? value->s : std::string();
    }
    int64_t Number(const char* key) const
    {
        const StateNode* value = node.find(key);
        if (value == nullptr)
            return 0;
        if (value->kind == StateNode::Kind::Int)
            return value->i;
        if (value->kind == StateNode::Kind::Double)
            return static_cast<int64_t>(value->d);
        return 0;
    }
    bool Has(const char* key) const
    {
        return node.find(key) != nullptr;
    }
};

Outcome Failure(int status, const char* error, std::string message)
{
    Outcome outcome;
    outcome.status = status;
    outcome.message = std::move(message);
    outcome.body = StateNode::Object();
    outcome.body["error"] = error;
    outcome.body["message"] = outcome.message;
    return outcome;
}

Outcome Success(std::string message, const CallbackInfo* info)
{
    Outcome outcome;
    outcome.message = std::move(message);
    outcome.body = StateNode::Object();
    outcome.body["status"] = "success";
    outcome.body["message"] = outcome.message;
    if (info != nullptr)
        outcome.body["callback"] = InfoNode(*info);
    return outcome;
}

/// The manager and the callback, or the failure
bool Find(ScriptCallbackManager* manager, const std::string& text, uint32_t& id, CallbackInfo& info, Outcome& failure)
{
    if (manager == nullptr)
    {
        failure = Failure(503, "Service Unavailable", "Script callbacks are not available on this emulator");
        return false;
    }
    if (!ParseId(text, id))
    {
        failure = Failure(400, "Bad Request", "The callback id is a number from 1, not '" + text + "'");
        return false;
    }
    if (!manager->Get(id, info))
    {
        failure = Failure(404, "Not Found", "No callback " + std::to_string(id));
        return false;
    }
    return true;
}
}  // namespace

StateNode InfoNode(const CallbackInfo& info)
{
    StateNode t = StateNode::Object();
    t["id"] = info.id;
    t["on"] = CallbackTriggerName(info.trigger);
    t["owner"] = info.owner;
    t["label"] = info.label;
    t["target"] = info.target;
    t["breakpoint"] = static_cast<unsigned>(info.breakpointId);
    if (!info.event.empty())
        t["event"] = info.event;
    t["policy"] = CallbackPolicyName(info.policy);
    t["state"] = CallbackStateName(info.state);
    if (!info.reason.empty())
        t["reason"] = info.reason;
    t["budget_us"] = info.budgetUs;
    t["ring"] = info.ring;
    t["pending"] = info.pending;
    const CallbackStats& s = info.stats;
    t["hits"] = s.hits;
    t["dropped"] = s.dropped;
    t["calls"] = s.calls;
    t["errors"] = s.errors;
    t["overruns"] = s.overruns;
    t["total_us"] = s.totalUs;
    t["max_us"] = s.maxUs;
    t["p99_us"] = s.p99Us;
    if (s.overruns > 0)
    {
        t["last_overrun_frame"] = s.lastOverrunFrame;
        t["last_overrun_us"] = s.lastOverrunUs;
    }
    if (!s.lastError.empty())
        t["last_error"] = s.lastError;
    return t;
}

StateNode ListNode(const std::vector<CallbackInfo>& callbacks)
{
    StateNode list = StateNode::Object();
    list["count"] = static_cast<uint64_t>(callbacks.size());
    StateNode& items = list["callbacks"];
    items = StateNode::Array();
    for (const CallbackInfo& info : callbacks)
        items.push(InfoNode(info));
    return list;
}

std::string FormatLine(const StateNode& callback)
{
    return callback_text::FormatLine(NodeReader{callback});
}

std::string FormatList(const StateNode& list)
{
    const StateNode* items = list.find("callbacks");
    std::string text = callback_text::ListHeader(items != nullptr ? items->size() : 0);
    if (items != nullptr)
        for (const StateNode& callback : items->items)
            text += "\n" + FormatLine(callback);
    return text;
}

bool ParseId(const std::string& text, uint32_t& id)
{
    if (text.empty() || text.size() > 10 || text.find_first_not_of("0123456789") != std::string::npos)
        return false;
    const unsigned long long value = std::stoull(text);
    if (value == 0 || value > 0xFFFF'FFFFull)
        return false;
    id = static_cast<uint32_t>(value);
    return true;
}

Outcome Resume(ScriptCallbackManager* manager, const std::string& text)
{
    uint32_t id = 0;
    CallbackInfo info;
    Outcome failure;
    if (!Find(manager, text, id, info, failure))
        return failure;
    if (info.state == CallbackState::Disabled || !manager->Resume(id))
        return Failure(409, "Conflict",
                       "Callback " + std::to_string(id) + " is disabled" +
                           (info.reason.empty() ? std::string() : " (" + info.reason + ")") +
                           ": a disabled callback cannot be resumed, the script registers it again");
    manager->Get(id, info);
    return Success("Callback " + std::to_string(id) + " resumed", &info);
}

Outcome Remove(ScriptCallbackManager* manager, const std::string& text)
{
    uint32_t id = 0;
    CallbackInfo info;
    Outcome failure;
    if (!Find(manager, text, id, info, failure))
        return failure;
    if (!manager->Remove(id))
        return Failure(404, "Not Found", "No callback " + std::to_string(id));
    Outcome outcome = Success("Callback " + std::to_string(id) + " removed", nullptr);
    outcome.body["id"] = id;
    outcome.body["removed"] = true;
    return outcome;
}

Outcome SetPolicy(ScriptCallbackManager* manager, const std::string& text, const std::string& policy)
{
    CallbackPolicy parsed = CallbackPolicy::Suspend;
    if (!ParseCallbackPolicy(policy, parsed))
        return Failure(400, "Bad Request", "The policy is suspend, disable or ignore, not '" + policy + "'");
    uint32_t id = 0;
    CallbackInfo info;
    Outcome failure;
    if (!Find(manager, text, id, info, failure))
        return failure;
    if (!manager->SetPolicy(id, parsed))
        return Failure(404, "Not Found", "No callback " + std::to_string(id));
    manager->Get(id, info);
    return Success("Callback " + std::to_string(id) + " policy " + CallbackPolicyName(parsed), &info);
}
}  // namespace callback_control
