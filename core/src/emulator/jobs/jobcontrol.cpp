#include "emulator/jobs/jobcontrol.h"

#include <algorithm>
#include <memory>
#include <sstream>

#include "debugger/analyzers/analysis/analysiscontrol.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/state/statenodejson.h"

namespace jobs
{
namespace
{
Reply Refuse(Error error, std::string message)
{
    Reply reply;
    reply.error = error;
    reply.message = std::move(message);
    return reply;
}

bool ParseU64(const std::string& text, uint64_t& value)
{
    if (text.empty() || text.size() > 19 || text.find_first_not_of("0123456789") != std::string::npos)
        return false;
    value = std::stoull(text);
    return true;
}

bool ParseFlag(const std::string& text, bool& value)
{
    return ParseAsync(text, value);
}

/// The job option: "12" or "#12"
bool JobOf(const Options& options, uint64_t& id, Reply& refusal)
{
    const auto it = options.find("job");
    if (it == options.end() || it->second.empty())
    {
        refusal = Refuse(Error::BadRequest, "job: the job's id is required (jobs list shows them)");
        return false;
    }
    std::string text = it->second;
    if (!text.empty() && text[0] == '#')
        text.erase(0, 1);
    if (!ParseU64(text, id) || id == 0)
    {
        refusal = Refuse(Error::BadRequest, "job: a job id, a positive integer (got '" + it->second + "')");
        return false;
    }
    return true;
}

StateNode LimitsNode()
{
    const Limits limits = Registry::Instance().GetLimits();
    StateNode node = StateNode::Object();
    node["workers"] = static_cast<uint64_t>(limits.workers);
    node["max_queued"] = static_cast<uint64_t>(limits.maxQueued);
    node["keep_finished"] = static_cast<uint64_t>(limits.keepFinished);
    node["keep_ms"] = limits.keepMs;
    node["keep_result_bytes"] = static_cast<uint64_t>(limits.keepResultBytes);
    node["max_wait_ms"] = kMaxWaitMs;
    return node;
}

Reply List(Emulator* emulator, const Options& options)
{
    std::string instance = emulator != nullptr ? emulator->GetId() : std::string();
    if (const auto it = options.find("instance"); it != options.end() && !it->second.empty())
    {
        if (it->second == "all")
            instance.clear();
        else if (it->second != "this")
            instance = it->second;
        else if (emulator == nullptr)
            return Refuse(Error::NotFound, "instance=this: no emulator selected");
    }
    std::string state = "all";
    if (const auto it = options.find("state"); it != options.end() && !it->second.empty())
    {
        state = it->second;
        if (state != "all" && state != "active" && state != "finished")
            return Refuse(Error::BadRequest, "state: active (queued, running), finished or all (got '" + state + "')");
    }
    uint64_t limit = 50;
    if (const auto it = options.find("limit"); it != options.end() && (!ParseU64(it->second, limit) || limit > 1000))
        return Refuse(Error::BadRequest, "limit: the newest N jobs, 0..1000 (0: all; default 50)");

    std::vector<Info> list = Registry::Instance().List(instance, state == "active");
    if (state == "finished")
        list.erase(std::remove_if(list.begin(), list.end(), [](const Info& i) { return !IsFinal(i.state); }), list.end());
    const size_t total = list.size();
    if (limit != 0 && list.size() > limit)
        list.erase(list.begin(), list.end() - static_cast<std::ptrdiff_t>(limit));

    Reply reply;
    StateNode items = StateNode::Array();
    size_t active = 0;
    for (const Info& info : list)
    {
        items.push(info.ToNode(false));
        active += IsFinal(info.state) ? 0 : 1;
    }
    reply.body["instance"] = instance.empty() ? std::string("all") : instance;
    reply.body["state"] = state;
    reply.body["count"] = static_cast<uint64_t>(list.size());
    reply.body["total"] = static_cast<uint64_t>(total);
    reply.body["active"] = static_cast<uint64_t>(active);
    reply.body["jobs"] = std::move(items);
    reply.body["limits"] = LimitsNode();
    std::ostringstream text;
    text << "Jobs (" << (instance.empty() ? std::string("all instances") : "instance " + instance) << ", " << state
         << "): " << list.size() << " shown";
    if (total > list.size())
        text << " of " << total;
    if (list.empty())
        text << "\n  (none)";
    for (const Info& info : list)
        text << "\n  " << info.Text(false);
    reply.text = text.str();
    return reply;
}

Reply Status(const Options& options, bool wait)
{
    uint64_t id = 0;
    if (Reply refusal; !JobOf(options, id, refusal))
        return refusal;
    bool withResult = true;
    if (const auto it = options.find("result"); it != options.end() && !ParseFlag(it->second, withResult))
        return Refuse(Error::BadRequest, "result: true (the default: the result when the job ended) or false");
    uint64_t waitMs = 30000;
    if (const auto it = options.find("wait_ms"); wait && it != options.end() && (!ParseU64(it->second, waitMs) || waitMs > kMaxWaitMs))
        return Refuse(Error::BadRequest, "wait_ms: milliseconds to wait for the job's end, 0.." + std::to_string(kMaxWaitMs) +
                                             " (default 30000; wait again for a longer job)");
    std::optional<Info> info = wait ? Registry::Instance().Wait(id, std::chrono::milliseconds(waitMs))
                                    : Registry::Instance().Get(id);
    if (!info)
        return Refuse(Error::NotFound, "no job " + std::to_string(id) + " (it never was, or it ended long ago: at most " +
                                           std::to_string(Registry::Instance().GetLimits().keepFinished) +
                                           " finished jobs are kept, for " +
                                           std::to_string(Registry::Instance().GetLimits().keepMs / 60000) + " min)");
    Reply reply;
    reply.body = info->ToNode(withResult);
    if (wait)
    {
        reply.body["wait_ms"] = waitMs;
        reply.body["ended"] = IsFinal(info->state);
    }
    reply.text = info->Text(withResult);
    if (wait && !IsFinal(info->state))
        reply.text += "\nStill " + std::string(StateName(info->state)) + " after " + std::to_string(waitMs) +
                      " ms: wait again";
    return reply;
}

Reply Cancel(const Options& options)
{
    uint64_t id = 0;
    if (Reply refusal; !JobOf(options, id, refusal))
        return refusal;
    const Registry::CancelResult result = Registry::Instance().Cancel(id);
    const std::optional<Info> info = Registry::Instance().Get(id);
    const std::string name = "job " + std::to_string(id) + (info ? " (" + info->kind + ")" : std::string());
    switch (result)
    {
        case Registry::CancelResult::NotFound:
            return Refuse(Error::NotFound, "no job " + std::to_string(id));
        case Registry::CancelResult::Finished:
            return Refuse(Error::Conflict, name + " has ended already (" + (info ? StateName(info->state) : "gone") + ")");
        case Registry::CancelResult::NotCancellable:
            return Refuse(Error::Conflict, name + " cannot stop: its work runs to its end (wait for it)");
        case Registry::CancelResult::Cancelled:
        case Registry::CancelResult::Requested:
            break;
    }
    Reply reply;
    if (info)
        reply.body = info->ToNode(false);
    reply.body["cancel"] = result == Registry::CancelResult::Cancelled ? "cancelled" : "requested";
    reply.text = result == Registry::CancelResult::Cancelled
                     ? name + " cancelled before it ran"
                     : name + " asked to stop; it ends when its work returns (jobs wait for its end)";
    return reply;
}

Error FromTTD(ttd::TTDControlError error)
{
    switch (error)
    {
        case ttd::TTDControlError::None:
            return Error::None;
        case ttd::TTDControlError::BadRequest:
            return Error::BadRequest;
        case ttd::TTDControlError::NotFound:
            return Error::NotFound;
        case ttd::TTDControlError::NotAvailable:
        case ttd::TTDControlError::InsufficientStorage:
            return Error::Unavailable;
        case ttd::TTDControlError::Conflict:
        case ttd::TTDControlError::Internal:
            return Error::Conflict;
    }
    return Error::Conflict;
}

Error FromAnalysis(analysis::Error error)
{
    switch (error)
    {
        case analysis::Error::None:
            return Error::None;
        case analysis::Error::BadRequest:
            return Error::BadRequest;
        case analysis::Error::NotFound:
            return Error::NotFound;
        case analysis::Error::NotAvailable:
            return Error::Unavailable;
        case analysis::Error::Conflict:
            return Error::Conflict;
    }
    return Error::Conflict;
}

Reply Start(Emulator* emulator, const Options& options)
{
    const auto domainIt = options.find("domain");
    const auto verbIt = options.find("verb");
    if (domainIt == options.end() || (domainIt->second != "ttd" && domainIt->second != "analysis"))
        return Refuse(Error::BadRequest, "domain: ttd (verbs dump, load, probe-frames, memory-series, find-event, "
                                         "replay-check, export-clip, journal-build, branch-create, find-last, "
                                         "reverse-continue) or analysis "
                                         "(build, trace, experiment, cover)");
    if (verbIt == options.end() || verbIt->second.empty())
        return Refuse(Error::BadRequest, "verb: the long verb to run as a job (dump, probe-frames, build, ...)");
    if (emulator == nullptr)
        return Refuse(Error::NotFound, "no emulator selected: a job runs on an instance");
    std::map<std::string, std::string> verbOptions;
    for (const auto& [name, value] : options)
        if (name != "domain" && name != "verb")
            verbOptions[name] = value;
    verbOptions["async"] = "true";

    Reply reply;
    if (domainIt->second == "ttd")
    {
        const ttd::TTDReply r = ttd::TTDControl(emulator->GetContext()).Execute({verbIt->second, verbOptions});
        if (!r.Ok())
            return Refuse(FromTTD(r.error), r.message);
        reply.body = r.body;
    }
    else
    {
        const analysis::Reply r = analysis::Execute(*emulator, verbIt->second, verbOptions);
        if (!r.Ok())
            return Refuse(FromAnalysis(r.error), r.message);
        reply.body = r.body;
        reply.text = r.text;
    }
    reply.accepted = true;
    if (const StateNode* text = reply.body.find("text"); reply.text.empty() && text != nullptr)
        reply.text = text->s;
    return reply;
}
}  // namespace

int ErrorHttpStatus(Error error)
{
    switch (error)
    {
        case Error::None:
            return 200;
        case Error::BadRequest:
            return 400;
        case Error::NotFound:
            return 404;
        case Error::Conflict:
            return 409;
        case Error::Unavailable:
            return 503;
    }
    return 500;
}

const char* ErrorPhrase(Error error)
{
    switch (error)
    {
        case Error::None:
            return "OK";
        case Error::BadRequest:
            return "Bad Request";
        case Error::NotFound:
            return "Not Found";
        case Error::Conflict:
            return "Conflict";
        case Error::Unavailable:
            return "Service Unavailable";
    }
    return "Internal Server Error";
}

StateNode Reply::ToValue() const
{
    if (!Ok())
    {
        StateNode value = StateNode::Object();
        value["error"] = ErrorPhrase(error);
        value["message"] = message;
        return value;
    }
    StateNode value = body;
    value["text"] = text;
    return value;
}

const std::vector<std::string>& Actions()
{
    static const std::vector<std::string> actions = {"list", "status", "wait", "cancel", "start"};
    return actions;
}

const std::vector<std::string>& OptionsFor(const std::string& action)
{
    static const std::vector<std::string> none;
    static const std::vector<std::string> list = {"instance", "state", "limit"};
    static const std::vector<std::string> status = {"job", "result"};
    static const std::vector<std::string> wait = {"job", "wait_ms", "result"};
    static const std::vector<std::string> cancel = {"job"};
    static const std::vector<std::string> any = {"*"};
    if (action == "list")
        return list;
    if (action == "status")
        return status;
    if (action == "wait")
        return wait;
    if (action == "cancel")
        return cancel;
    if (action == "start")
        return any;
    return none;
}

Reply Execute(Emulator* emulator, const std::string& action, const Options& options)
{
    const std::vector<std::string>& actions = Actions();
    if (std::find(actions.begin(), actions.end(), action) == actions.end())
        return Refuse(Error::BadRequest, "unknown action '" + action + "' (list, status, wait, cancel, start)");
    const std::vector<std::string>& known = OptionsFor(action);
    if (!(known.size() == 1 && known.front() == "*"))
    {
        for (const auto& [name, value] : options)
        {
            (void)value;
            if (std::find(known.begin(), known.end(), name) == known.end())
            {
                std::string names;
                for (const std::string& option : known)
                    names += (names.empty() ? "" : ", ") + option;
                return Refuse(Error::BadRequest, action + ": unknown option '" + name + "' (options: " + names + ")");
            }
        }
    }
    if (action == "list")
        return List(emulator, options);
    if (action == "status")
        return Status(options, false);
    if (action == "wait")
        return Status(options, true);
    if (action == "cancel")
        return Cancel(options);
    return Start(emulator, options);
}

bool ParseAsync(const std::string& text, bool& async)
{
    if (text.empty() || text == "true" || text == "1" || text == "yes" || text == "on")
    {
        async = true;
        return true;
    }
    if (text == "false" || text == "0" || text == "no" || text == "off")
    {
        async = false;
        return true;
    }
    return false;
}

uint64_t SubmitOnInstance(const std::string& emulatorId, const std::string& kind, const std::string& lane,
                          const std::string& summary, bool cancellable,
                          std::function<Outcome(Emulator&, Context&)> call, std::function<void(Emulator&)> onCancel,
                          std::string& error)
{
    Spec spec;
    spec.kind = kind;
    spec.emulatorId = emulatorId;
    spec.lane = lane;
    spec.summary = summary;
    spec.cancellable = cancellable;
    spec.work = [emulatorId, call = std::move(call)](Context& context) -> Outcome {
        Outcome outcome;
        const std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(emulatorId);
        if (!emulator)
        {
            outcome.ok = false;
            outcome.status = 404;
            outcome.error = "the instance " + emulatorId + " is gone";
            return outcome;
        }
        // Held while the verb runs: a removal cancels the job, then waits for this lease before it frees anything
        const Emulator::ContextLease lease = emulator->LeaseContext();
        if (!lease)
        {
            outcome.ok = false;
            outcome.status = 503;
            outcome.error = "the instance " + emulatorId + " is being removed";
            return outcome;
        }
        if (context.CancelRequested())
        {
            outcome.ok = false;
            outcome.cancelled = true;
            outcome.status = 409;
            outcome.error = "cancelled before it ran";
            return outcome;
        }
        return call(*emulator, context);
    };
    if (onCancel)
        spec.onCancel = [emulatorId, onCancel = std::move(onCancel)] {
            if (const std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(emulatorId))
                onCancel(*emulator);
        };
    return Registry::Instance().Submit(std::move(spec), error);
}

StateNode AcceptedBody(uint64_t id)
{
    StateNode body = StateNode::Object();
    const std::optional<Info> info = Registry::Instance().Get(id);
    body["job"] = id;
    body["state"] = info ? StateName(info->state) : "queued";
    body["kind"] = info ? info->kind : std::string();
    body["emulator_id"] = info ? info->emulatorId : std::string();
    body["summary"] = info ? info->summary : std::string();
    body["async"] = true;
    body["status_url"] = "/api/v1/jobs/" + std::to_string(id);
    body["wait_url"] = "/api/v1/jobs/" + std::to_string(id) + "?wait_ms=30000";
    body["text"] = AcceptedText(id);
    return body;
}

std::string AcceptedText(uint64_t id)
{
    const std::optional<Info> info = Registry::Instance().Get(id);
    std::ostringstream text;
    text << "Job " << id;
    if (info)
    {
        text << " (" << info->kind << (info->summary.empty() ? "" : ": " + info->summary) << ") " << StateName(info->state);
        if (!info->emulatorId.empty())
            text << " on " << info->emulatorId;
    }
    text << ". Its end: GET /api/v1/jobs/" << id << "?wait_ms=30000 (long-poll, up to " << kMaxWaitMs
         << " ms a call), MCP jobs action=wait job=" << id << ", CLI jobs wait " << id
         << "; cancel: DELETE /api/v1/jobs/" << id << " or jobs action=cancel";
    return text.str();
}

std::string Summary(const std::string& verb, const std::map<std::string, std::string>& options)
{
    std::string summary = verb;
    for (const auto& [name, value] : options)
    {
        if (name == "async")
            continue;
        std::string shown = value.size() > 48 ? value.substr(0, 45) + "..." : value;
        summary += " " + name + "=" + shown;
    }
    return summary;
}

StateNode ProgressTopic(const std::string& emulatorId)
{
    StateNode node = StateNode::Object();
    StateNode active = StateNode::Array();
    for (const Info& info : Registry::Instance().List(emulatorId, true))
    {
        StateNode item = StateNode::Object();
        item["job"] = info.id;
        item["kind"] = info.kind;
        item["summary"] = info.summary;
        item["state"] = StateName(info.state);
        item["done"] = info.done;
        item["total"] = info.total;
        item["unit"] = info.unit;
        item["percent"] = static_cast<uint64_t>(info.Percent());
        if (info.hasHits)
            item["hits"] = info.hits;
        item["cancel_requested"] = info.cancelRequested;
        active.push(std::move(item));
    }
    node["count"] = static_cast<uint64_t>(active.size());
    node["active"] = std::move(active);
    return node;
}

StateNode DoneTopic(const std::string& emulatorId)
{
    std::vector<Info> list = Registry::Instance().List(emulatorId, false);
    list.erase(std::remove_if(list.begin(), list.end(), [](const Info& i) { return !IsFinal(i.state); }), list.end());
    std::sort(list.begin(), list.end(), [](const Info& a, const Info& b) {
        return a.endedMs != b.endedMs ? a.endedMs > b.endedMs : a.id > b.id;
    });
    if (list.size() > 8)
        list.resize(8);
    StateNode node = StateNode::Object();
    StateNode recent = StateNode::Array();
    for (const Info& info : list)
    {
        StateNode item = StateNode::Object();
        item["job"] = info.id;
        item["kind"] = info.kind;
        item["summary"] = info.summary;
        item["state"] = StateName(info.state);
        item["status"] = info.outcome.status;
        item["error"] = info.outcome.error;
        item["ended_ms"] = info.endedMs;
        recent.push(std::move(item));
    }
    node["last_job"] = list.empty() ? uint64_t{0} : list.front().id;
    node["recent"] = std::move(recent);
    return node;
}

std::string ResultText(int status, const StateNode& value, size_t maxChars)
{
    std::string json = StateNodeToJsonText(value);
    if (json.size() > maxChars)
        json = json.substr(0, maxChars) + "... (" + std::to_string(json.size() - maxChars) +
               " more chars: the job's result has it whole)";
    return "HTTP " + std::to_string(status) + ": " + json;
}

}  // namespace jobs
