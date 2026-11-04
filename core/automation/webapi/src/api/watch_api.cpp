// Frame watches on the WebAPI (docs/inprogress/2026-11-03-run-until-watches): run_until (a direct run until a
// frame-end condition, a screen text, a screen change / stillness or a game event holds) and the armed watches of a
// running machine. The verbs and reports are core framewatch's (the CLI, Lua, Python and MCP say the same); every
// route is also a WebSocket call, and an armed watch's hit is pushed as watch_hit on debug.events
// (debug-event-hub.cpp)

#include "../common/longcallpool.h"
#include "../common/session_helper.h"
#include "../common/statenode_json.h"
#include "../emulator_api.h"

#include <debugger/watch/framewatch.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/execution/runcontrol.h>

using namespace drogon;

namespace api
{
namespace v1
{

extern void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
void Reply(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body, int status)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(status));
    addCorsHeaders(resp);
    callback(resp);
}

void BadRequest(std::function<void(const HttpResponsePtr&)>& callback, const std::string& message)
{
    Json::Value error;
    error["error"] = "Bad Request";
    error["message"] = message;
    Reply(callback, error, 400);
}

std::shared_ptr<Emulator> EmulatorOrError(const std::string& id, std::function<void(const HttpResponsePtr&)>& callback)
{
    auto* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager ? manager->GetEmulator(id) : nullptr;
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";
        Reply(callback, error, 404);
    }
    return emulator;
}

/// The body's members as the core's options: strings as they are, numbers in decimal, booleans true / false. False
/// with `error` for a nested value
bool OptionsFromBody(const HttpRequestPtr& req, std::vector<std::pair<std::string, std::string>>& options,
                     std::string& error)
{
    auto json = req->getJsonObject();
    if (!json || !json->isObject())
    {
        error = "Body: a JSON object, e.g. {\"text\": \"PRESS\", \"max_frames\": 500}";
        return false;
    }
    for (const std::string& key : json->getMemberNames())
    {
        const Json::Value& value = (*json)[key];
        if (value.isBool())
            options.emplace_back(key, value.asBool() ? "true" : "false");
        else if (value.isUInt64())
            options.emplace_back(key, std::to_string(value.asUInt64()));
        else if (value.isString())
            options.emplace_back(key, value.asString());
        else
        {
            error = "'" + key + "': a string, a non-negative number or a boolean";
            return false;
        }
    }
    return true;
}

void ReplyOutcome(std::function<void(const HttpResponsePtr&)>& callback, const framewatch::Outcome& outcome)
{
    Json::Value body = StateNodeToJson(outcome.body);
    body["text"] = outcome.text;
    Reply(callback, body, outcome.status);
}
}  // namespace

/// @brief POST /api/v1/emulator/{id}/run_until - body {if?, text?, screen? (changed | stable), stable_frames?, event?,
/// match? (any | all), max_frames? (1..200000, default 3000), breakpoints?}: whole frames until the rules hold at a
/// frame end; answers {met, outcome (met | budget | breakpoint | aborted), frames, frame, pc, ttd_frame?, ms, hit?:
/// {frame, reasons, text_line?, stable_for?, event_fires?}, text, state, stop}. A long call (LongCallPool)
void EmulatorAPI::runUntil(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([req, answer, id]() {
        std::shared_ptr<Emulator> emulator = EmulatorOrError(id, *answer);
        if (!emulator)
            return;
        std::vector<std::pair<std::string, std::string>> options;
        std::string error;
        framewatch::Spec spec;
        unsigned maxFrames = 0;
        bool breakpoints = false;
        if (!OptionsFromBody(req, options, error) || !framewatch::RunOptions(options, spec, maxFrames, breakpoints, error))
        {
            BadRequest(*answer, error);
            return;
        }
        const execution::RunReply reply = execution::RunControl(emulator).RunUntil(
            webapi_session::RequesterOf(req, emulator->GetContext()), spec, maxFrames, breakpoints);
        Json::Value body = reply.status == execution::RunReply::Status::Revoked ? webapi_session::RevokedBody()
                                                                                : StateNodeToJson(reply.body);
        if (!reply.Ok() && reply.status != execution::RunReply::Status::Revoked)
        {
            body["error"] = reply.error;
            body["message"] = reply.message;
        }
        Reply(*answer, body, reply.HttpStatus());
    });
}

/// @brief GET /api/v1/emulator/{id}/watches - {count, watches: [{id, label?, spec, then, once, armed_frame, hits,
/// last_hit?, error?}], text}
void EmulatorAPI::getWatches(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    std::shared_ptr<Emulator> emulator = EmulatorOrError(id, callback);
    if (emulator)
        ReplyOutcome(callback, framewatch::ListWatches(emulator->GetContext()));
}

/// @brief POST /api/v1/emulator/{id}/watches - body {if?, text?, screen?, stable_frames?, event?, match?, label?,
/// then? (pause | notify), once? (default true)}: arms a watch checked at every frame end of the running machine:
/// 201 {id, watch, text}; 400 a bad spec or 16 armed
void EmulatorAPI::addWatch(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    std::shared_ptr<Emulator> emulator = EmulatorOrError(id, callback);
    if (!emulator)
        return;
    std::vector<std::pair<std::string, std::string>> options;
    std::string error;
    if (!OptionsFromBody(req, options, error))
    {
        BadRequest(callback, error);
        return;
    }
    ReplyOutcome(callback, framewatch::AddWatch(emulator->GetContext(), options));
}

/// @brief DELETE /api/v1/emulator/{id}/watches/{watch} - one watch by id, or "all": {removed: [ids], text}; 404 unknown
void EmulatorAPI::removeWatch(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id, const std::string& watch) const
{
    std::shared_ptr<Emulator> emulator = EmulatorOrError(id, callback);
    if (emulator)
        ReplyOutcome(callback, framewatch::RemoveWatch(emulator->GetContext(), watch));
}

}  // namespace v1
}  // namespace api
