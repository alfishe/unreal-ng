// Slots API: the machine's ZX-bus slots over HTTP (ZX-bus slots architecture.md §9).
// Every route is a thin adapter over SlotControl (core/src/emulator/slots/slotcontrol.h): the request becomes a
// SlotControlRequest, the SlotControlReply is the response body and its status. A change restarts the machine (owner
// decision Q6): the reply names the new emulator id; a refusal is HTTP 409 with the plan as the body.

#include "../emulator_api.h"
#include "../common/statenode_json.h"

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/slots/slotcontrol.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function defined in emulator_api.cpp
void addCorsHeaders(drogon::HttpResponsePtr& resp);

namespace
{
    void RespondReply(std::function<void(const HttpResponsePtr&)>& callback, const SlotControlReply& reply)
    {
        auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(reply.ToValue()));
        resp->setStatusCode(static_cast<HttpStatusCode>(reply.httpStatus));
        addCorsHeaders(resp);
        callback(resp);
    }

    /// The emulator's id (by id, symbolic id or index), or a 404 in the slots envelope
    bool ResolveId(const std::string& idOrIndex, std::string& id, std::function<void(const HttpResponsePtr&)>& callback)
    {
        auto emulator = getEmulatorByIdOrIndex(idOrIndex);
        if (!emulator)
        {
            SlotControlReply reply;
            reply.status = "no-machine";
            reply.httpStatus = 404;
            reply.message = "Emulator not found with ID: " + idOrIndex;
            RespondReply(callback, reply);
            return false;
        }
        id = emulator->GetId();
        return true;
    }

    bool Flag(const Json::Value& value)
    {
        if (value.isBool())
            return value.asBool();
        if (value.isNumeric())
            return value.asInt() != 0;
        const std::string text = value.asString();
        return text == "true" || text == "1" || text == "yes" || text == "on";
    }

    /// Card options from the body: a string ("dip=ym,saa gsRam=2m") or an object ({"dip": "ym,saa"} or
    /// {"dip": ["ym", "saa"]})
    std::string OptionsText(const Json::Value& value)
    {
        if (value.isString())
            return value.asString();
        std::string text;
        if (value.isObject())
        {
            for (const std::string& name : value.getMemberNames())
            {
                const Json::Value& v = value[name];
                std::string values;
                if (v.isArray())
                {
                    for (const Json::Value& item : v)
                        values += (values.empty() ? "" : ",") + item.asString();
                    if (values.empty())
                        values = "none";
                }
                else
                {
                    values = v.asString();
                }
                text += (text.empty() ? "" : " ") + name + "=" + values;
            }
        }
        return text;
    }

    /// The body's fields (JSON) and the query string
    void CollectChange(const HttpRequestPtr& req, SlotControlRequest& request)
    {
        auto take = [&request](const std::string& name, const Json::Value& value) {
            if (name == "card" || name == "personality")
                request.card = value.asString();
            else if (name == "options")
                request.options = OptionsText(value);
            else if (name == "adapter")
                request.adapter = value.asString();
            else if (name == "replaceIfIncompatible" || name == "replace")
                request.replaceIfIncompatible = Flag(value);
            else if (name == "dryRun" || name == "dry_run")
                request.dryRun = Flag(value);
            else if (name == "mediaDisposition" || name == "media")
                request.media = value.asString();
        };
        for (const auto& [name, value] : req->getParameters())
            take(name, Json::Value(value));
        if (auto json = req->getJsonObject())
        {
            for (const std::string& name : json->getMemberNames())
                take(name, (*json)[name]);
        }
    }
}  // namespace

/// @brief GET /api/v1/emulator/{id}/slots
void EmulatorAPI::getSlots(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    (void)req;
    SlotControlRequest request;
    if (!ResolveId(id, request.emulatorId, callback))
        return;
    request.verb = "list";
    RespondReply(callback, SlotControl::Execute(request));
}

/// @brief GET /api/v1/emulator/{id}/slots/catalog
void EmulatorAPI::getSlotsCatalog(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    (void)req;
    SlotControlRequest request;
    if (!ResolveId(id, request.emulatorId, callback))
        return;
    request.verb = "catalog";
    RespondReply(callback, SlotControl::Execute(request));
}

/// @brief GET /api/v1/emulator/{id}/slots/matrix?table=
void EmulatorAPI::getSlotsMatrix(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    SlotControlRequest request;
    if (!ResolveId(id, request.emulatorId, callback))
        return;
    request.verb = "matrix";
    request.table = req->getParameter("table");
    RespondReply(callback, SlotControl::Execute(request));
}

/// @brief POST /api/v1/emulator/{id}/slots/{slot}/{verb}  verb: plug, remove, options (= set)
/// Body: {card, options, adapter, replaceIfIncompatible, dryRun, mediaDisposition}
void EmulatorAPI::postSlotVerb(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id, const std::string& slot, const std::string& verb) const
{
    SlotControlRequest request;
    if (!ResolveId(id, request.emulatorId, callback))
        return;
    request.verb = verb == "options" ? "set" : verb;
    if (request.verb != "plug" && request.verb != "remove" && request.verb != "set")
    {
        SlotControlReply reply;
        reply.status = "bad-request";
        reply.httpStatus = 400;
        reply.message = "unknown slot verb '" + verb + "' (plug, remove, options)";
        RespondReply(callback, reply);
        return;
    }
    request.slot = slot == "auto" ? std::string() : slot;
    CollectChange(req, request);
    RespondReply(callback, SlotControl::Execute(request));
}

/// @brief PUT /api/v1/emulator/{id}/slots/{slot}/options  Body: {options, replaceIfIncompatible, dryRun, ...}
void EmulatorAPI::putSlotOptions(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id, const std::string& slot) const
{
    SlotControlRequest request;
    if (!ResolveId(id, request.emulatorId, callback))
        return;
    request.verb = "set";
    request.slot = slot;
    CollectChange(req, request);
    RespondReply(callback, SlotControl::Execute(request));
}

}  // namespace v1
}  // namespace api
