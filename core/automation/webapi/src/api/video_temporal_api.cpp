/// @file video_temporal_api.cpp
/// @brief Temporal effects (ZX DLSS de-flicker) endpoints: status and algorithm
/// switch. Thin: the report and the switch are TemporalStatus helpers shared
/// with CLI, Lua and Python (MCP capture_media calls these routes).

#include "../../../temporalstatus.h"
#include "../common/statenode_json.h"
#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

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
void Respond(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body, HttpStatusCode code)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

/// Error body: error, message and the offered algorithm names
void RespondError(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode code, const char* error,
                  const std::string& message)
{
    Json::Value body;
    body["error"] = error;
    body["message"] = message;
    body["algorithms"] = Json::Value(Json::arrayValue);
    for (const std::string& name : TemporalStatus::OfferedAlgorithms())
        body["algorithms"].append(name);
    Respond(callback, body, code);
}

/// The emulator's context, or a 404 / 500 already sent
EmulatorContext* ResolveContext(const std::string& id, std::function<void(const HttpResponsePtr&)>& callback)
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        RespondError(callback, k404NotFound, "Not Found", "Emulator with specified ID not found");
        return nullptr;
    }
    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pScreen)
    {
        RespondError(callback, k500InternalServerError, "Internal Error", "Unable to access emulator screen");
        return nullptr;
    }
    return context;
}
} // namespace

namespace api
{
namespace v1
{
/// @brief GET /api/v1/emulator/{id}/video/temporal
void EmulatorAPI::getVideoTemporal(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    (void)req;
    if (EmulatorContext* context = ResolveContext(id, callback))
        Respond(callback, StateNodeToJson(TemporalStatus::Report(context)), k200OK);
}

/// @brief PUT|POST /api/v1/emulator/{id}/video/temporal — body {"algorithm": "<name>"|""|"off"}
void EmulatorAPI::setVideoTemporal(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    EmulatorContext* context = ResolveContext(id, callback);
    if (!context)
        return;
    auto json = req->getJsonObject();
    if (!json || !json->isObject() || !(*json)["algorithm"].isString())
    {
        RespondError(callback, k400BadRequest, "Bad Request",
                     "Body must be {\"algorithm\": \"<name>\"}; \"\" or \"off\" switches the effect off");
        return;
    }
    const std::string name = (*json)["algorithm"].asString();
    if (!TemporalStatus::Set(context, name))
    {
        RespondError(callback, k400BadRequest, "Bad Request",
                     "Unknown temporal algorithm '" + name + "'. Valid: " + TemporalStatus::OfferedList() + ", off");
        return;
    }
    Respond(callback, StateNodeToJson(TemporalStatus::Report(context)), k200OK);
}
} // namespace v1
} // namespace api
