// Actions API: the core ActionRegistry over HTTP (control surfaces actions.md §6, architecture §3, §4).
// GET /api/v1/controllers/actions lists the catalogue with whether each action can run now for this caller;
// POST /api/v1/controllers/actions/{action} runs one on the instance's actions worker and answers when it is done.
// The routes are outside /api/v1/emulator/, so the pre-routing gate does not see them: the registry applies the gate
// of the caller's origin itself (a companion token, a carried session, else the implicit session of the client).

#include "../emulator_api.h"

#include <cmath>
#include <memory>
#include <string>

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <emulator/actions/actionregistry.h>
#include <emulator/actions/contextstate.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/execution/automationgate.h>

#include "../common/companion_helper.h"
#include "../common/session_helper.h"
#include "../common/statenode_json.h"

using namespace drogon;

namespace api
{
namespace v1
{

void addCorsHeaders(drogon::HttpResponsePtr& resp);

namespace
{
void RespondJson(const std::function<void(const HttpResponsePtr&)>& callback, int status, const Json::Value& body)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(status));
    addCorsHeaders(resp);
    callback(resp);
}

Json::Value ErrorJson(const std::string& error, const std::string& message)
{
    Json::Value body;
    body["ok"] = false;
    body["error"] = error;
    body["message"] = message;
    return body;
}

StateNode JsonToStateNode(const Json::Value& value)
{
    if (value.isBool())
        return StateNode(value.asBool());
    if (value.isInt64() || value.isUInt64())
        return StateNode(static_cast<int64_t>(value.asInt64()));
    if (value.isDouble())
        return StateNode(value.asDouble());
    if (value.isString())
        return StateNode(value.asString());
    if (value.isArray())
    {
        StateNode array = StateNode::Array();
        for (const Json::Value& item : value)
            array.push(JsonToStateNode(item));
        return array;
    }
    if (value.isObject())
    {
        StateNode object = StateNode::Object();
        for (const std::string& name : value.getMemberNames())
            object[name] = JsonToStateNode(value[name]);
        return object;
    }
    return {};
}

/// The instance a request names (?instance= or the body's "instance": an id or an index); without one the selected
/// instance, else the only one. "" with `error` set when none fits
std::string InstanceOf(const std::string& named, std::string& error)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    if (!named.empty())
    {
        std::shared_ptr<Emulator> emulator = getEmulatorByIdOrIndex(named);
        if (!emulator)
            error = "no instance '" + named + "'";
        return emulator ? emulator->GetId() : std::string();
    }
    const std::string selected = manager->GetSelectedEmulatorId();
    if (!selected.empty() && manager->GetEmulator(selected))
        return selected;
    const std::vector<std::string> ids = manager->GetEmulatorIds();
    if (ids.size() == 1)
        return ids.front();
    error = ids.empty() ? "no machine" : "several instances: name one (instance)";
    return {};
}

/// Who asks: a companion (its token, checked by the pre-routing advice), else the MCP server or a WebAPI client with
/// the session it carries; the identity is the automation gate's (one implicit session per client)
actions::ActionOrigin OriginOf(const HttpRequestPtr& req)
{
    actions::ActionOrigin origin;
    if (const std::optional<companion::CompanionInfo> companion = webapi_companion::CompanionOf(req))
    {
        origin.origin = execution::Origin::Companion;
        origin.identity = companion->id;
        origin.client = companion->name;
        return origin;
    }
    const webapi_session::GateClient client = webapi_session::GateClientOf(req);
    origin.origin = client.surface == "mcp" ? execution::Origin::Mcp : execution::Origin::WebApi;
    origin.identity = client.identity;
    origin.client = client.client;
    origin.peer = client.peer;
    origin.lease = std::chrono::duration_cast<std::chrono::milliseconds>(automation_gate::kImplicitLease);
    if (const std::optional<unreal::UUID> session = webapi_session::SessionOf(req))
        origin.session = session->toString();
    return origin;
}

/// The value by the action's kind: press - omitted / true / false / {pressed}; delta - a number / {delta, rate};
/// level - a number / {level}; text - a string / {text}. {params} in the object or beside "value" in the body
bool ValueOf(const Json::Value& body, actions::ValueKind kind, actions::ActionValue& value, std::string& error)
{
    value = actions::ActionValue{};
    value.kind = kind;
    const Json::Value& raw = body.isObject() ? body["value"] : Json::Value::nullSingleton();
    auto number = [&](const Json::Value& v, double& out, const char* what) {
        if (!v.isNumeric())
        {
            error = std::string("value: a ") + what + " takes a number";
            return false;
        }
        out = v.asDouble();
        if (!std::isfinite(out))
        {
            error = "value: not a finite number";
            return false;
        }
        return true;
    };
    switch (kind)
    {
        case actions::ValueKind::Press:
            if (raw.isBool())
                value.pressed = raw.asBool();
            else if (raw.isObject() && raw.isMember("pressed"))
                value.pressed = raw["pressed"].asBool();
            else if (!raw.isNull() && !raw.isObject())
            {
                error = "value: a press takes true / false or {pressed}";
                return false;
            }
            break;
        case actions::ValueKind::Delta:
            if (raw.isObject())
            {
                if (!number(raw["delta"], value.delta, "delta"))
                    return false;
                if (raw.isMember("rate") && !number(raw["rate"], value.rate, "delta"))
                    return false;
            }
            else if (!number(raw, value.delta, "delta"))
                return false;
            break;
        case actions::ValueKind::Level:
            if (!number(raw.isObject() ? raw["level"] : raw, value.level, "level"))
                return false;
            value.level = std::max(0.0, std::min(1.0, value.level));
            value.pressed = value.level > 0;
            break;
        case actions::ValueKind::Text:
        {
            const Json::Value& text = raw.isObject() ? raw["text"] : raw;
            if (!text.isString())
            {
                error = "value: a text action takes a string";
                return false;
            }
            value.text = text.asString();
            break;
        }
    }
    const Json::Value& params = raw.isObject() && raw.isMember("params") ? raw["params"] : body["params"];
    if (params.isObject())
        value.params = JsonToStateNode(params);
    else if (!params.isNull())
    {
        error = "params: an object";
        return false;
    }
    return true;
}
}  // namespace

void EmulatorAPI::getActions(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    std::string error;
    const std::string named = req->getParameter("instance");
    const std::string instance = InstanceOf(named, error);
    if (instance.empty() && !named.empty())
    {
        RespondJson(callback, 404, ErrorJson("Not Found", error));
        return;
    }
    Json::Value body;
    body["ok"] = true;
    body["instance"] = instance.empty() ? Json::Value(Json::nullValue) : Json::Value(instance);
    body["actions"] = Json::Value(Json::arrayValue);
    for (const actions::ActionStatus& status : actions::ActionRegistry::Instance().Statuses(instance, OriginOf(req)))
    {
        Json::Value row;
        row["id"] = status.info.id;
        row["label"] = status.info.label;
        row["group"] = status.info.group;
        row["kind"] = actions::ValueKindName(status.info.kind);
        row["held"] = status.info.held;
        row["gui"] = status.info.gui;
        const std::string scope = automation_gate::CompanionScopeName(status.scope);
        row["scope"] = scope.empty() ? Json::Value(Json::nullValue) : Json::Value(scope);
        row["needs_pause"] = status.needsPause;
        if (!status.info.params.empty())
            row["params"] = status.info.params;
        row["available"] = status.available;
        if (!status.available)
        {
            row["error"] = status.error;
            row["reason"] = status.reason;
        }
        body["actions"].append(row);
    }
    RespondJson(callback, 200, body);
}

void EmulatorAPI::getContext(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    std::string error;
    const std::string named = req->getParameter("instance");
    const std::string instance = InstanceOf(named, error);
    if (instance.empty())
    {
        const bool notFound = !named.empty() || error == "no machine";
        RespondJson(callback, notFound ? 404 : 400, ErrorJson(notFound ? "Not Found" : "Bad Request", error));
        return;
    }
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->GetEmulator(instance);
    const actions::ContextInfo info = actions::ContextState::Instance().Get(instance, emulator.get());
    Json::Value body;
    body["ok"] = true;
    body["instance"] = instance;
    body["context"] = info.context;
    body["set"] = info.set;
    body["by"] = info.set ? Json::Value(info.by) : Json::Value(Json::nullValue);
    body["default"] = info.fallback;
    RespondJson(callback, 200, body);
}

void EmulatorAPI::postAction(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& action) const
{
    actions::ActionRegistry& registry = actions::ActionRegistry::Instance();
    const std::optional<actions::ActionInfo> info = registry.Find(action);
    if (!info)
    {
        RespondJson(callback, 404, ErrorJson("Not Found", "unknown action '" + action + "'"));
        return;
    }
    const std::shared_ptr<Json::Value> json = req->getJsonObject();
    const Json::Value body = json ? *json : Json::Value(Json::objectValue);
    if (!req->body().empty() && !json)
    {
        RespondJson(callback, 400, ErrorJson("Bad Request", "the body is not JSON"));
        return;
    }
    std::string error;
    std::string named = body.isObject() && body["instance"].isString() ? body["instance"].asString()
                        : body.isObject() && body["instance"].isIntegral() ? std::to_string(body["instance"].asInt())
                                                                          : req->getParameter("instance");
    const std::string instance = InstanceOf(named, error);
    if (instance.empty())
    {
        RespondJson(callback, named.empty() && error != "no machine" ? 400 : 404,
                    ErrorJson(named.empty() && error != "no machine" ? "Bad Request" : "Not Found", error));
        return;
    }
    actions::ActionValue value;
    if (!ValueOf(body, info->kind, value, error))
    {
        RespondJson(callback, 400, ErrorJson("Bad Request", error));
        return;
    }
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    registry.Submit(action, std::move(value), instance, OriginOf(req),
                    [req, answer, action, instance](actions::ActionResult result) {
                        Json::Value reply;
                        reply["ok"] = result.ok;
                        reply["action"] = action;
                        reply["instance"] = instance;
                        if (!result.ok)
                            reply["error"] = result.error;
                        reply["message"] = result.message;
                        reply["result"] = StateNodeToJson(result.body);
                        // The automation gate opened (or went on in) a session for this client: the post-handling
                        // advice says so in headers and in "automation_session", as for the /emulator/ routes
                        if (!result.implicitSession.empty())
                        {
                            req->attributes()->insert(webapi_session::kImplicitSessionAttribute,
                                                      result.implicitSession);
                            req->attributes()->insert(webapi_session::kImplicitOpenedAttribute, result.implicitOpened);
                        }
                        RespondJson(*answer, result.HttpStatus(), reply);
                    });
}

}  // namespace v1
}  // namespace api
