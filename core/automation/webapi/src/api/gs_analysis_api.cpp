// The GS card CPU analysis (gs-debugger P5.6, requirements §4.13 X8 / X10 / X11): coverage / CDL, the RAM heat map,
// the reads of never-written RAM, and the switch. One report for every plane (gsanalysisreport.h); `text` carries the
// lines of CLI 'gs coverage' / 'gs heatmap' / 'gs analysis'. Reads never stop the machine.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/sound/chips/gs/gsanalysisreport.h>
#include <json/json.h>

#include <string>

#include "../common/statenode_json.h"
#include "../emulator_api.h"

using namespace drogon;

namespace api
{
namespace v1
{
extern void addCorsHeaders(HttpResponsePtr& resp);
extern void ReplyDeviceState(const StateNode& node, std::function<void(const HttpResponsePtr&)>& callback);

namespace
{
void ReplyError(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode code, const std::string& error,
                const std::string& message)
{
    Json::Value body;
    body["error"] = error;
    body["message"] = message;
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

/// The query of a coverage / heat-map read from the request's parameters; false after replying 400
bool QueryFrom(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>& callback,
               GSAnalysisQuery& query)
{
    std::string error;
    if (GSAnalysisQueryFromParams([&req](const std::string& key) { return req->getParameter(key); }, query, error))
        return true;
    ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", error);
    return false;
}
}  // namespace

void EmulatorAPI::getStateAudioGSAnalysis(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback,
                                          const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator not found with ID: " + id);
    ReplyDeviceState(DeviceState::GsAnalysisStatus(emulator->GetContext(), req->getParameter("cpu")), callback);
}

void EmulatorAPI::getStateAudioGSCoverage(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback,
                                          const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator not found with ID: " + id);
    GSAnalysisQuery query;
    if (!QueryFrom(req, callback, query))
        return;
    ReplyDeviceState(DeviceState::GsCoverage(emulator->GetContext(), query), callback);
}

void EmulatorAPI::getStateAudioGSHeatmap(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback,
                                         const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator not found with ID: " + id);
    GSAnalysisQuery query;
    if (!QueryFrom(req, callback, query))
        return;
    ReplyDeviceState(DeviceState::GsHeatmap(emulator->GetContext(), query), callback);
}

void EmulatorAPI::postControlAudioGSAnalysis(const HttpRequestPtr& req,
                                             std::function<void(const HttpResponsePtr&)>&& callback,
                                             const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator not found with ID: " + id);
    GSAnalysisControlRequest request;
    if (auto json = req->getJsonObject())
    {
        if (json->isMember("action") && !(*json)["action"].isString())
            return ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "'action' is a string");
        request.action = json->get("action", "").asString();
        request.cpu = json->get("cpu", "").asString();
        if (json->isMember("decay"))
        {
            const Json::Value& decay = (*json)["decay"];
            if (!decay.isIntegral() || decay.asInt64() < 0 || decay.asInt64() > 255)
                return ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request",
                                  "'decay' is 0-255 (heat lost per frame)");
            request.decay = decay.asInt();
        }
    }
    else
    {
        request.action = req->getParameter("action");
        request.cpu = req->getParameter("cpu");
    }
    const GSAnalysisReply reply = GSAnalysisControl(emulator->GetContext(), request);
    if (reply.status != 200)
    {
        const StateNode* error = reply.body.find("error");
        return ReplyError(callback, reply.status == 404 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest,
                          reply.status == 404 ? "Not Found" : "Bad Request", error ? error->s : std::string());
    }
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(reply.body));
    addCorsHeaders(resp);
    callback(resp);
}
}  // namespace v1
}  // namespace api
