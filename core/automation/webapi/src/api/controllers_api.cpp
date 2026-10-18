// Controllers API: the machine's connectors over HTTP (controller-manager surfaces.md §6).
// Every route is a thin adapter over ControllerControl (core/src/emulator/io/controllers/controllercontrol.h): the
// request becomes a ControllerRequest, the ControllerReply is the response body and its status.

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/controllers/controllercontrol.h>

using namespace drogon;
using namespace api::v1;
using namespace controllers;

namespace api
{
namespace v1
{

// Helper function defined in emulator_api.cpp
void addCorsHeaders(drogon::HttpResponsePtr& resp);

namespace
{
    void Respond(std::function<void(const HttpResponsePtr&)>& callback, int status, const std::string& body)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(static_cast<HttpStatusCode>(status));
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        resp->setBody(body);
        addCorsHeaders(resp);
        callback(resp);
    }

    void RespondReply(std::function<void(const HttpResponsePtr&)>& callback, const ControllerReply& reply)
    {
        Respond(callback, reply.HttpStatus(), reply.ToJson());
    }

    /// The emulator, or a 404 in the controllers envelope
    std::shared_ptr<Emulator> FindEmulator(const std::string& id, std::function<void(const HttpResponsePtr&)>& callback)
    {
        auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
        if (!emulator)
            Respond(callback, 404, R"({"ok":false,"error":"unknown-emulator","message":"Emulator not found"})");
        return emulator;
    }

    std::string OptionText(const Json::Value& value)
    {
        if (value.isBool())
            return value.asBool() ? "true" : "false";
        if (value.isNull())
            return "";
        if (value.isArray())
        {
            std::string list;
            for (const Json::Value& item : value)
                list += (list.empty() ? "" : ",") + OptionText(item);
            return list.empty() ? "none" : list;
        }
        return value.asString();  // numbers and strings alike
    }

    /// Options from the query string and the JSON body; "device" is the plug's retro device
    void CollectOptions(const HttpRequestPtr& req, ControllerRequest& request)
    {
        for (const auto& [name, value] : req->getParameters())
        {
            if (name == "device")
                request.device = value;
            else
                request.options[name] = value;
        }
        if (auto json = req->getJsonObject())
            for (const std::string& name : json->getMemberNames())
            {
                if (name == "device")
                    request.device = (*json)[name].asString();
                else
                    request.options[name] = OptionText((*json)[name]);
            }
    }
}  // namespace

/// @brief GET /api/v1/emulator/{id}/controllers
void EmulatorAPI::getControllers(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    (void)req;
    auto emulator = FindEmulator(id, callback);
    if (!emulator)
        return;
    RespondReply(callback, ControllerControl(emulator->GetContext()).Execute({"list", "", "", {}}));
}

/// @brief GET /api/v1/emulator/{id}/controllers/{connector}  (and /controllers/targets?device=,
/// /controllers/auto)
void EmulatorAPI::getController(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id, const std::string& connector) const
{
    auto emulator = FindEmulator(id, callback);
    if (!emulator)
        return;
    ControllerRequest request;
    if (connector == "targets" || connector == "auto")
        request.verb = connector;
    else
    {
        request.verb = "get";
        request.selector = connector;
    }
    CollectOptions(req, request);
    RespondReply(callback, ControllerControl(emulator->GetContext()).Execute(request));
}

/// @brief POST /api/v1/emulator/{id}/controllers/{connector}/{verb}
/// verb: plug, unplug, options, input; POST /controllers/auto/apply starts the port-read watch again
void EmulatorAPI::postControllerVerb(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id, const std::string& connector, const std::string& verb) const
{
    auto emulator = FindEmulator(id, callback);
    if (!emulator)
        return;
    ControllerRequest request;
    if (connector == "auto" && verb == "apply")
    {
        request.verb = "auto";
        request.options["apply"] = "";
    }
    else
    {
        request.verb = verb;
        request.selector = connector;
    }
    CollectOptions(req, request);
    RespondReply(callback, ControllerControl(emulator->GetContext()).Execute(request));
}

}  // namespace v1
}  // namespace api
