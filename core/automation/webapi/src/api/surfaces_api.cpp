// Surfaces API: the targets of the control surfaces (actions::TargetState; Stream Deck integration targeting.md),
// the readouts and the activity feed over HTTP. Every route is one verb of actions::surfaces::Call, the code the CLI,
// MCP, Lua and Python run too: the args come from the query and the JSON body, the reply is the verb's body with its
// status. The WebSocket `call` of a route runs this same handler; the pushes (`target`, `readout`, `activity`) are the
// `surface` topic (control surfaces S4).

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <emulator/actions/surfacesapi.h>

#include "../common/statenode_json.h"

using namespace drogon;

namespace api
{
namespace v1
{

void addCorsHeaders(drogon::HttpResponsePtr& resp);

namespace
{
using Callback = std::function<void(const HttpResponsePtr&)>;

void Respond(const Callback& callback, int status, const Json::Value& body)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(status));
    addCorsHeaders(resp);
    callback(resp);
}

/// The query's parameters, then the JSON body's members (the body wins); false when the body is not JSON
bool ArgsOf(const HttpRequestPtr& req, StateNode& args)
{
    args = StateNode::Object();
    for (const auto& [name, value] : req->getParameters())
        args[name] = value;
    if (req->body().empty())
        return true;
    const std::shared_ptr<Json::Value> json = req->getJsonObject();
    if (!json)
        return false;
    if (json->isObject())
        for (const std::string& name : json->getMemberNames())
            args[name] = JsonValueToStateNode((*json)[name]);
    return true;
}

void Run(const HttpRequestPtr& req, const Callback& callback, const char* verb,
         std::initializer_list<std::pair<const char*, std::string>> path = {})
{
    StateNode args;
    if (!ArgsOf(req, args))
    {
        Json::Value body;
        body["ok"] = false;
        body["error"] = "Bad Request";
        body["message"] = "the body is not JSON";
        Respond(callback, 400, body);
        return;
    }
    for (const auto& [name, value] : path)
        args[name] = value;
    const actions::surfaces::Reply reply = actions::surfaces::Call(verb, args);
    Respond(callback, reply.status, StateNodeToJson(reply.body));
}
}  // namespace

void EmulatorAPI::getSurfaceViews(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    Run(req, callback, "views");
}

void EmulatorAPI::postSurfaceView(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    Run(req, callback, "view_add");
}

void EmulatorAPI::putSurfaceView(const HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& view) const
{
    Run(req, callback, "view_update", {{"view", view}});
}

void EmulatorAPI::deleteSurfaceView(const HttpRequestPtr& req, SurfaceCallback&& callback,
                                    const std::string& view) const
{
    Run(req, callback, "view_remove", {{"view", view}});
}

void EmulatorAPI::getSurfaceTarget(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    Run(req, callback, "target");
}

void EmulatorAPI::getSurfaceSessions(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    Run(req, callback, "sessions");
}

void EmulatorAPI::postSurfaceSession(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    Run(req, callback, "session_open");
}

void EmulatorAPI::getSurfaceSession(const HttpRequestPtr& req, SurfaceCallback&& callback,
                                    const std::string& session) const
{
    Run(req, callback, "session", {{"session", session}});
}

void EmulatorAPI::deleteSurfaceSession(const HttpRequestPtr& req, SurfaceCallback&& callback,
                                       const std::string& session) const
{
    Run(req, callback, "session_close", {{"session", session}});
}

void EmulatorAPI::putSurfacePin(const HttpRequestPtr& req, SurfaceCallback&& callback,
                                const std::string& session) const
{
    Run(req, callback, "pin", {{"session", session}});
}

void EmulatorAPI::getSurfaceReadouts(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    Run(req, callback, "readouts");
}

void EmulatorAPI::getSurfaceActivity(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    Run(req, callback, "activity");
}

}  // namespace v1
}  // namespace api
