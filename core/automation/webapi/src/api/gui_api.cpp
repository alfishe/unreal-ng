// GUI API: the GUI control plane's methods on the WebAPI port (docs/inprogress/2026-11-07-gui-control-plane §4.10).
// The program registered them (GuiPlane, automation/common/guimethods.h); every route hands the call over and answers
// the plane's result with 200 when it comes: {ok, body} or {ok: false, error, message}. 404 no_gui when the program
// has no plane, 400 for a body that is not a JSON object. No session, no companion scope: the routes are open, as
// the plane's own local socket is. The WebSocket `call` of a route runs this same handler.
//
// GET /api/v1/gui                    list: every GUI program on the machine, its windows and views
// GET /api/v1/gui/catalogue          the interfaces, their commands and parameters
// GET /api/v1/gui/state?target=      a node's state, interfaces and machine
// POST /api/v1/gui/{method}          a command ("memory.goto"), the body its parameters and `target`?

#include "../../../common/guimethods.h"
#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <memory>
#include <sstream>
#include <string>

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

void Fail(const Callback& callback, int status, const std::string& error, const std::string& message)
{
    Json::Value body;
    body["ok"] = false;
    body["error"] = error;
    body["message"] = message;
    Respond(callback, status, body);
}

bool Parse(const std::string& text, Json::Value& out)
{
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream in(text);
    return Json::parseFromStream(builder, in, &out, &errors);
}

std::string Compact(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

/// No plane: 404 no_gui, true
bool NoGui(const Callback& callback)
{
    if (GuiPlane::Get())
        return false;
    Fail(callback, 404, GuiPlane::kNoGui, "this program has no GUI control plane");
    return true;
}

/// Hands the call over; the plane's result with 200 (its own JSON, whatever thread answers)
void Forward(Callback callback, const std::string& method, const std::string& target, const Json::Value& params)
{
    GuiPlane::Call(method, target, Compact(params), [callback = std::move(callback)](const std::string& resultJson) {
        Json::Value result;
        if (!Parse(resultJson, result) || !result.isObject())
        {
            Fail(callback, 200, "failed", "the GUI answered no result");
            return;
        }
        Respond(callback, 200, result);
    });
}
}  // namespace

void EmulatorAPI::getGui(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    (void)req;
    if (!NoGui(callback))
        Forward(std::move(callback), "list", {}, Json::Value(Json::objectValue));
}

void EmulatorAPI::getGuiCatalogue(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    (void)req;
    if (NoGui(callback))
        return;
    Json::Value catalogue;
    if (!Parse(GuiPlane::Catalogue(), catalogue) || !catalogue.isObject())
    {
        Fail(callback, 200, "failed", "the GUI gave no catalogue");
        return;
    }
    Respond(callback, 200, catalogue);
}

void EmulatorAPI::getGuiState(const HttpRequestPtr& req, SurfaceCallback&& callback) const
{
    if (NoGui(callback))
        return;
    const std::string target = req->getParameter("target");
    if (target.empty())
    {
        Fail(callback, 400, "bad_request", "state needs ?target= (a node, as GET /api/v1/gui lists them)");
        return;
    }
    Json::Value params(Json::objectValue);
    params["target"] = target;
    Forward(std::move(callback), "state", {}, params);
}

void EmulatorAPI::postGuiMethod(const HttpRequestPtr& req, SurfaceCallback&& callback, const std::string& method) const
{
    if (NoGui(callback))
        return;
    Json::Value params(Json::objectValue);
    if (!req->body().empty())
    {
        const std::shared_ptr<Json::Value> json = req->getJsonObject();
        if (!json || !json->isObject())
        {
            Fail(callback, 400, "bad_request", "the body is the command's parameters as a JSON object");
            return;
        }
        params = *json;
    }
    // `target` names the node; ?target= too (a WebSocket call's query)
    std::string target = req->getParameter("target");
    if (params.isMember("target"))
    {
        if (!params["target"].isString())
        {
            Fail(callback, 400, "bad_request", "target: a node, as GET /api/v1/gui lists them");
            return;
        }
        target = params["target"].asString();
        params.removeMember("target");
    }
    Forward(std::move(callback), method, target, params);
}

}  // namespace v1
}  // namespace api
