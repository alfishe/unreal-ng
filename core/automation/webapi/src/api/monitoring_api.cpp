// Monitoring API: the host process, the clients, each instance's pace and the summaries a monitoring page shows
// (monitoring::Call, core/src/emulator/monitoring/monitoringapi.h; Stream Deck use case C10) over HTTP. Every route is
// one verb of monitoring::Call, the code the CLI, MCP, Lua and Python run too: the args come from the query, the
// reply is the verb's body with its status. The WebSocket `call` of a route runs this same handler; the topics of the
// same names (`host.stats`, `emulator.stats`, `debug.breakpoints`, ...) push the changes. Reads only published state:
// they run on the I/O thread and never wait for an emulation thread.

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <emulator/monitoring/monitoringapi.h>

#include "../common/statenode_json.h"

using namespace drogon;

namespace api
{
namespace v1
{

void addCorsHeaders(drogon::HttpResponsePtr& resp);

namespace
{
void RunMonitor(const HttpRequestPtr& req, const std::function<void(const HttpResponsePtr&)>& callback,
                const std::string& verb, const std::string& instance = {})
{
    StateNode args = StateNode::Object();
    for (const auto& [name, value] : req->getParameters())
        args[name] = value;
    if (!instance.empty())
        args["instance"] = instance;
    const monitoring::Reply reply = monitoring::Call(verb, args);
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(reply.body));
    resp->setStatusCode(static_cast<HttpStatusCode>(reply.status));
    addCorsHeaders(resp);
    callback(resp);
}
}  // namespace

void EmulatorAPI::getMonitor(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    RunMonitor(req, callback, "all");
}

void EmulatorAPI::getMonitorName(const HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& name) const
{
    RunMonitor(req, callback, name);
}

void EmulatorAPI::getBreakpointGroups(const HttpRequestPtr& req, SurfaceCallback&& callback,
                                      const std::string& id) const
{
    RunMonitor(req, callback, "breakpoint_groups", id);
}

}  // namespace v1
}  // namespace api
