/// @file analysis_api.cpp
/// @brief WebAPI routes of the analysis layers (docs/emulator/analysis/analysis-layers.md):
///   POST /analysis/start   {level?: facts | counters, mode?: z80 | physical}
///   POST /analysis/stop
///   GET  /analysis/status
///   GET  /analysis/explain?address=&page=
///
/// Every route turns its request into an analysis::Execute action (core/src/debugger/analyzers/analysis/
/// analysiscontrol.h): the checks, the answer and its "text" are shared with the CLI, MCP, Lua and Python. A WebSocket
/// `call` of a route runs the same handler.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <json/json.h>

#include <map>
#include <string>

#include "../common/statenode_json.h"
#include "../emulator_api.h"
#include "debugger/analyzers/analysis/analysiscontrol.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

extern void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
void RespondError(HttpStatusCode status, const std::string& phrase, const std::string& message,
                  std::function<void(const HttpResponsePtr&)>& callback)
{
    Json::Value error;
    error["error"] = phrase;
    error["message"] = message;
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(status);
    addCorsHeaders(resp);
    callback(resp);
}

void RespondAnalysis(const std::string& id, const std::string& action, const analysis::Options& options,
                     std::function<void(const HttpResponsePtr&)>& callback)
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
    {
        RespondError(HttpStatusCode::k404NotFound, "Not Found", "Emulator not found with ID: " + id, callback);
        return;
    }
    if (emulator->IsDestroying())
    {
        RespondError(HttpStatusCode::k503ServiceUnavailable, "Service Unavailable", "Emulator is shutting down", callback);
        return;
    }
    const analysis::Reply reply = analysis::Execute(*emulator, action, options);
    Json::Value body = StateNodeToJson(reply.ToValue());
    if (reply.Ok())
    {
        body["emulator_id"] = id;
        body["action"] = action;
    }
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(reply.HttpStatus()));
    addCorsHeaders(resp);
    callback(resp);
}

/// The query parameters an action takes (others, a cache buster, are ignored)
analysis::Options OptionsFromQuery(const HttpRequestPtr& req, const std::string& action)
{
    analysis::Options options;
    for (const std::string& name : analysis::OptionsFor(action))
    {
        const std::string value = req->getParameter(name);
        if (!value.empty())
            options[name] = value;
    }
    return options;
}

/// The body's members as options (strings, numbers, booleans). False after an answer for any other value
bool OptionsFromJson(const HttpRequestPtr& req, analysis::Options& options, std::function<void(const HttpResponsePtr&)>& callback)
{
    auto json = req->getJsonObject();
    if (!json || !json->isObject())
        return true;
    for (const std::string& name : json->getMemberNames())
    {
        const Json::Value& v = (*json)[name];
        if (v.isNull())
            continue;
        if (v.isObject() || v.isArray())
        {
            RespondError(HttpStatusCode::k400BadRequest, "Bad Request", "'" + name + "' must be a number, a string or true / false",
                         callback);
            return false;
        }
        options[name] = v.isBool() ? (v.asBool() ? "true" : "false") : v.asString();
    }
    return true;
}
}  // namespace

void EmulatorAPI::analysisStart(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    analysis::Options options;
    if (!OptionsFromJson(req, options, callback))
        return;
    RespondAnalysis(id, "start", options, callback);
}

void EmulatorAPI::analysisStop(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    analysis::Options options;
    if (!OptionsFromJson(req, options, callback))
        return;
    RespondAnalysis(id, "stop", options, callback);
}

void EmulatorAPI::analysisStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    RespondAnalysis(id, "status", OptionsFromQuery(req, "status"), callback);
}

void EmulatorAPI::analysisExplain(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    RespondAnalysis(id, "explain", OptionsFromQuery(req, "explain"), callback);
}

}  // namespace v1
}  // namespace api
