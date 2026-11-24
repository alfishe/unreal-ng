/// @file jobs_api.cpp
/// @brief The jobs routes (PLAN #122 S2, docs/emulator/design/control-interfaces/jobs.md). Thin: list, status,
/// cancel and start are jobs::Execute (emulator/jobs/jobcontrol.h), shared with CLI, Lua, Python and MCP. The one
/// thing done here is the long-poll: GET /api/v1/jobs/{job}?wait_ms=N registers a watch on the registry and a
/// timer on the event loop and returns; whichever fires first answers, so no server thread waits for a job.

#include "../common/statenode_json.h"
#include "../emulator_api.h"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/jobs/jobcontrol.h>
#include <emulator/jobs/jobregistry.h>
#include <json/json.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{
// CORS helper (defined in emulator_api.cpp)
extern void addCorsHeaders(HttpResponsePtr& resp);
} // namespace v1
} // namespace api

namespace
{
using Callback = std::function<void(const HttpResponsePtr&)>;

void Respond(const Callback& callback, const jobs::Reply& reply)
{
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(reply.ToValue()));
    resp->setStatusCode(static_cast<HttpStatusCode>(reply.HttpStatus()));
    addCorsHeaders(resp);
    callback(resp);
}

/// The query parameters an action takes, as options
jobs::Options OptionsFromQuery(const HttpRequestPtr& req, const std::string& action)
{
    jobs::Options options;
    for (const std::string& name : jobs::OptionsFor(action))
    {
        const std::string value = req->getParameter(name);
        if (!value.empty())
            options[name] = value;
    }
    return options;
}

/// A JSON body's scalar members as options (an array of scalars joined by commas); false (answered) otherwise
bool OptionsFromJson(const HttpRequestPtr& req, jobs::Options& options, const Callback& callback)
{
    auto json = req->getJsonObject();
    if (!json || !json->isObject())
        return true;
    for (const std::string& name : json->getMemberNames())
    {
        const Json::Value& v = (*json)[name];
        if (v.isNull())
            continue;
        if (v.isArray())
        {
            std::string joined;
            bool scalars = true;
            for (const Json::Value& item : v)
            {
                scalars = scalars && !item.isObject() && !item.isArray() && !item.isNull();
                if (scalars)
                    joined += (joined.empty() ? "" : ",") +
                              (item.isBool() ? std::string(item.asBool() ? "true" : "false") : item.asString());
            }
            if (scalars)
            {
                options[name] = joined;
                continue;
            }
        }
        if (v.isObject() || v.isArray())
        {
            jobs::Reply refusal;
            refusal.error = jobs::Error::BadRequest;
            refusal.message = "'" + name + "' must be a number, a string, true / false or a list of them";
            Respond(callback, refusal);
            return false;
        }
        options[name] = v.isBool() ? (v.asBool() ? "true" : "false") : v.asString();
    }
    return true;
}

/// One long-poll: answered once, by the job's end or by the timer
struct Poll
{
    std::atomic<bool> answered{false};
    Callback callback;
    jobs::Options options;
    uint64_t job = 0;
    uint64_t waitMs = 0;
    uint64_t token = 0;
};

/// The status answer of a long-poll, with wait_ms and ended (as the `wait` action answers)
void AnswerPoll(const std::shared_ptr<Poll>& poll)
{
    jobs::Reply reply = jobs::Execute(nullptr, "status", poll->options);
    if (reply.Ok())
    {
        const StateNode* state = reply.body.find("state");
        const bool ended = state != nullptr && state->s != "queued" && state->s != "running";
        reply.body["wait_ms"] = poll->waitMs;
        reply.body["ended"] = ended;
        if (!ended && state != nullptr)
            reply.text += "\nStill " + state->s + " after " + std::to_string(poll->waitMs) + " ms: wait again";
    }
    Respond(poll->callback, reply);
}
} // namespace

namespace api
{
namespace v1
{
/// @brief GET /api/v1/jobs[?instance=all|<id>&state=active|finished|all&limit=N]
void EmulatorAPI::listJobs(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    jobs::Options options = OptionsFromQuery(req, "list");
    options.emplace("instance", "all");
    Respond(callback, jobs::Execute(nullptr, "list", options));
}

/// @brief GET /api/v1/emulator/{id}/jobs[?state=&limit=]
void EmulatorAPI::listInstanceJobs(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    jobs::Options options = OptionsFromQuery(req, "list");
    // A removed instance's jobs stay listed until they age out: its id filters them all the same
    options["instance"] = emulator ? emulator->GetId() : id;
    Respond(callback, jobs::Execute(nullptr, "list", options));
}

/// @brief POST /api/v1/emulator/{id}/jobs {"domain": "ttd" | "analysis", "verb": "...", <the verb's options>}
void EmulatorAPI::startJob(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator || emulator->IsDestroying())
    {
        jobs::Reply refusal;
        refusal.error = emulator ? jobs::Error::Unavailable : jobs::Error::NotFound;
        refusal.message = emulator ? "Emulator is shutting down" : "Emulator not found with ID: " + id;
        Respond(callback, refusal);
        return;
    }
    jobs::Options options;
    if (!OptionsFromJson(req, options, callback))
        return;
    for (const char* name : {"domain", "verb"})
        if (const std::string value = req->getParameter(name); !value.empty())
            options.emplace(name, value);
    Respond(callback, jobs::Execute(emulator.get(), "start", options));
}

/// @brief GET /api/v1/jobs/{job}[?wait_ms=N&result=false]
///
/// wait_ms 0 (or absent): the job now. 1..60000 (jobs::kMaxWaitMs, more is refused): answered when the job ends or
/// after wait_ms, whichever is first, with "ended" and "wait_ms" beside the job's fields. No thread is held: the
/// registry's watch (called by the thread that ends the job) and an event-loop timer race for one answer. When
/// Limits::maxWatches long-polls wait already, the answer is at once (ended false: poll again).
void EmulatorAPI::getJob(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                         const std::string& job) const
{
    jobs::Options options = OptionsFromQuery(req, "wait");
    options["job"] = job;
    const auto waitIt = options.find("wait_ms");
    if (waitIt == options.end() || waitIt->second == "0")
    {
        options.erase("wait_ms");
        Respond(callback, jobs::Execute(nullptr, "status", options));
        return;
    }
    // The checks of `wait` (job id, wait_ms range, result): a refusal answers now
    {
        jobs::Options check = options;
        check["wait_ms"] = "0";
        jobs::Reply checked = jobs::Execute(nullptr, "wait", check);
        if (!checked.Ok())
        {
            Respond(callback, checked);
            return;
        }
    }
    uint64_t waitMs = 0;
    try
    {
        waitMs = std::stoull(waitIt->second);
    }
    catch (...)
    {
        waitMs = jobs::kMaxWaitMs + 1;
    }
    if (waitMs > jobs::kMaxWaitMs || waitIt->second.find_first_not_of("0123456789") != std::string::npos)
    {
        jobs::Reply refusal;
        refusal.error = jobs::Error::BadRequest;
        refusal.message = "wait_ms: milliseconds to wait for the job's end, 0.." + std::to_string(jobs::kMaxWaitMs) +
                          " (wait again for a longer job)";
        Respond(callback, refusal);
        return;
    }

    auto poll = std::make_shared<Poll>();
    poll->callback = std::move(callback);
    options.erase("wait_ms");
    poll->options = options;
    poll->job = std::stoull(job[0] == '#' ? job.substr(1) : job);
    poll->waitMs = waitMs;

    uint64_t token = 0;
    const jobs::Registry::WatchResult watched = jobs::Registry::Instance().Watch(
        poll->job,
        [poll](const jobs::Info&) {
            if (!poll->answered.exchange(true))
                AnswerPoll(poll);
        },
        token);
    if (watched != jobs::Registry::WatchResult::Watching)
    {
        // Ended (or gone) already, or too many long-polls: the answer now
        if (!poll->answered.exchange(true))
            AnswerPoll(poll);
        return;
    }
    poll->token = token;
    drogon::app().getLoop()->runAfter(static_cast<double>(waitMs) / 1000.0, [poll] {
        if (poll->answered.exchange(true))
            return;
        if (jobs::Registry::Alive())
            (void)jobs::Registry::Instance().Unwatch(poll->job, poll->token);
        AnswerPoll(poll);
    });
}

/// @brief DELETE /api/v1/jobs/{job}, POST /api/v1/jobs/{job}/cancel
void EmulatorAPI::cancelJob(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& job) const
{
    (void)req;
    Respond(callback, jobs::Execute(nullptr, "cancel", {{"job", job}}));
}
} // namespace v1
} // namespace api
