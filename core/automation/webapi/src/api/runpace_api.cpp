/// @file runpace_api.cpp
/// @brief The paced run of an instance (execution::RunPacer, core/src/emulator/execution/runpacer.h; Stream Deck use
/// case D1):
///   GET  /api/v1/emulator/{id}/run_pace  - the pace: preset, interval, running, why it stopped, the ladder
///   POST /api/v1/emulator/{id}/run_pace  - {op: set | next | prev | by | start | stop | toggle | step, preset?,
///                                          interval_ms?, delta?}
/// The `run.state` topic carries the pace too (its `pace` member); the actions run.pace_* call the same pacer.

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include "../common/longcallpool.h"
#include "../common/session_helper.h"
#include "../common/statenode_json.h"
#include "../emulator_api.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/runpacer.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

extern void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
void Answer(int status, const Json::Value& body, std::function<void(const HttpResponsePtr&)>& callback)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(status));
    addCorsHeaders(resp);
    callback(resp);
}

void Error(int status, const std::string& error, const std::string& message,
           std::function<void(const HttpResponsePtr&)>& callback)
{
    Json::Value body;
    body["error"] = error;
    body["message"] = message;
    Answer(status, body, callback);
}

void ReplyPace(const execution::RunReply& reply, std::function<void(const HttpResponsePtr&)>& callback)
{
    Json::Value body = StateNodeToJson(reply.body);
    if (!reply.Ok())
    {
        body["error"] = reply.error;
        body["message"] = reply.message;
    }
    Answer(reply.HttpStatus(), body, callback);
}
}  // namespace

void EmulatorAPI::getRunPace(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        Error(404, "Not Found", "Emulator with specified ID not found", callback);
        return;
    }
    // A read: the pacer's state under its short lock (none yet: the default), never the machine
    const std::shared_ptr<execution::RunPacer> pacer = execution::RunPacer::Find(emulator->GetId());
    const StateNode pace = pacer ? pacer->StateBody(true) : execution::RunPacer::DefaultStateBody(true);
    Json::Value body;
    body["emulator_id"] = emulator->GetId();
    body["pace"] = StateNodeToJson(pace);
    Answer(200, body, callback);
}

void EmulatorAPI::postRunPace(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        Error(404, "Not Found", "Emulator with specified ID not found", callback);
        return;
    }
    auto json = req->getJsonObject();
    const std::string op = json && json->isMember("op") ? (*json)["op"].asString() : std::string();
    static const char* const kOps[] = {"set", "next", "prev", "by", "start", "stop", "toggle", "step"};
    if (std::find_if(std::begin(kOps), std::end(kOps), [&](const char* name) { return op == name; }) == std::end(kOps))
    {
        Error(400, "Bad Request", "op: set, next, prev, by, start, stop, toggle or step", callback);
        return;
    }
    std::optional<size_t> preset;
    std::optional<int> interval;
    int delta = 0;
    if (op == "set")
    {
        if (json->isMember("preset"))
        {
            const Json::Value& value = (*json)["preset"];
            size_t index = 0;
            if (!execution::RunPacer::PresetIndex(value.isString() ? value.asString()
                                                  : value.isIntegral() ? std::to_string(value.asInt64())
                                                                       : std::string(),
                                                  index))
            {
                Error(400, "Bad Request", "preset: full, 25fps, 5fps, 1fps, frame, 100lines, 10lines, 1line or 1op",
                      callback);
                return;
            }
            preset = index;
        }
        if (json->isMember("interval_ms"))
        {
            if (!(*json)["interval_ms"].isNumeric())
            {
                Error(400, "Bad Request", "interval_ms: 1..5000", callback);
                return;
            }
            interval = static_cast<int>(std::clamp<int64_t>((*json)["interval_ms"].asInt64(), -1, 100000));
        }
        if (!preset && !interval)
        {
            Error(400, "Bad Request", "set takes preset and / or interval_ms", callback);
            return;
        }
    }
    if (op == "by")
    {
        if (!json->isMember("delta") || !(*json)["delta"].isNumeric())
        {
            Error(400, "Bad Request", "by takes delta (presets, + finer)", callback);
            return;
        }
        delta = static_cast<int>(std::clamp<int64_t>((*json)["delta"].asInt64(), -16, 16));
    }
    const execution::Requester requester = webapi_session::RequesterOf(req, emulator->GetContext());
    // start / toggle may wait for a pause, step runs a tick: on the long-call pool, never on an I/O thread
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([emulator, op, preset, interval, delta, requester, answer]() {
        const std::shared_ptr<execution::RunPacer> pacer = execution::RunPacer::For(emulator);
        execution::RunReply reply;
        if (op == "set")
            reply = pacer->Set(requester, preset, interval);
        else if (op == "next" || op == "prev")
            reply = pacer->Shift(requester, op == "next" ? 1 : -1);
        else if (op == "by")
            reply = pacer->Shift(requester, delta);
        else if (op == "start")
            reply = pacer->Start(requester);
        else if (op == "stop")
            reply = pacer->Stop();
        else if (op == "toggle")
            reply = pacer->Toggle(requester);
        else
            reply = pacer->Step(requester);
        ReplyPace(reply, *answer);
    });
}

}  // namespace v1
}  // namespace api
