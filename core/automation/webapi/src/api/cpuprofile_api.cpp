/// @file cpuprofile_api.cpp
/// @brief WebAPI routes of the CPU time profile (docs/emulator/analysis/cpu-profile.md):
///   POST /cpuprofile/start   {stack?, beam?, frames?, ring_kib?, stream?, reset?}
///   POST /cpuprofile/stop
///   POST /cpuprofile/reset
///   POST /cpuprofile/mark    {name, drop?}
///   GET  /cpuprofile/status
///   GET  /cpuprofile/report?since=&top=&sort=self|inclusive|calls&callers=&beam=
///   GET  /cpuprofile/frames?last=&top=
///   POST /cpuprofile/export  {format: kit|json|entries|sym|ann, path?, since?}
///
/// Every route is one profile::Execute operation (core/src/debugger/analyzers/profile/profilecontrol.h): the checks,
/// the answer and its "text" are shared with the CLI, MCP, Lua and Python. A WebSocket `call` of a route runs the same
/// handler. The reads (status, report, frames, export) never hold the CPU.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <json/json.h>

#include <algorithm>
#include <string>

#include "../common/statenode_json.h"
#include "../emulator_api.h"
#include "debugger/analyzers/profile/profilecontrol.h"

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
}  // namespace

void EmulatorAPI::cpuProfile(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id, const std::string& op) const
{
    const auto& ops = profile::Operations();
    if (std::find(ops.begin(), ops.end(), op) == ops.end())
    {
        RespondError(HttpStatusCode::k404NotFound, "Not Found",
                     "no cpuprofile route '" + op + "' (start, stop, reset, mark, status, report, frames, export)", callback);
        return;
    }
    const bool write = op == "start" || op == "stop" || op == "reset" || op == "mark" || op == "export";
    if (write && req->method() != drogon::Post)
    {
        RespondError(HttpStatusCode::k405MethodNotAllowed, "Method Not Allowed", "cpuprofile/" + op + " is a POST", callback);
        return;
    }
    // the options: the query's parameters the operation takes, then the JSON body's members
    profile::Options options;
    for (const std::string& name : profile::OptionsFor(op))
    {
        const std::string value = req->getParameter(name);
        if (!value.empty())
            options[name] = value;
    }
    if (auto json = req->getJsonObject(); json && json->isObject())
        for (const std::string& name : json->getMemberNames())
        {
            const Json::Value& v = (*json)[name];
            if (v.isNull())
                continue;
            if (v.isObject() || v.isArray())
            {
                RespondError(HttpStatusCode::k400BadRequest, "Bad Request", "'" + name + "' must be a number, a string or true / false",
                             callback);
                return;
            }
            options[name] = v.isBool() ? (v.asBool() ? "true" : "false") : v.asString();
        }
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
    const profile::Reply reply = profile::Execute(*emulator, op, options);
    Json::Value body = StateNodeToJson(reply.ToValue());
    if (reply.Ok())
    {
        body["emulator_id"] = id;
        body["op"] = op;
    }
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(reply.HttpStatus()));
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
