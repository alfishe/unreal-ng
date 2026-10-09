/// @file symbols_api.cpp
/// @brief WebAPI symbol endpoints (symbols/tdd.md section 8):
///   GET    /symbols/formats              the symbol codecs (also without an instance: /api/v1/symbols/formats)
///   GET    /symbols/detect?path=         the codecs that could read a file, the one chosen
///   GET    /symbols/sets                 the symbol sets and the label count
///   PUT    /symbols/sets   {id, enabled?, priority?}
///   DELETE /symbols/sets?id=
///   POST   /symbols/import {path, format?, set?, space?, base?, policy?}
///   POST   /symbols/export {path, format?, sets? (array or "a,b"), pages?}
///
/// Every route turns its request into a SymbolControl verb: the verbs, their checks and their answers are shared with
/// the CLI, MCP, Lua and Python (core/src/debugger/labels/symbolcontrol.h).

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <json/json.h>

#include <map>
#include <string>

#include "../common/statenode_json.h"
#include "../emulator_api.h"
#include "debugger/labels/symbolcontrol.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

extern void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
void Respond(const SymbolReply& reply, std::function<void(const HttpResponsePtr&)>& callback)
{
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(reply.ToValue()));
    resp->setStatusCode(static_cast<HttpStatusCode>(reply.HttpStatus()));
    addCorsHeaders(resp);
    callback(resp);
}

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

void RespondSymbols(const std::string& id, const std::string& verb, std::map<std::string, std::string> options,
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
    Respond(SymbolControl(emulator->GetContext()).Execute({verb, std::move(options)}), callback);
}

/// The query parameters a verb takes as options (an unrelated parameter, a cache buster, is ignored)
std::map<std::string, std::string> OptionsFromQuery(const HttpRequestPtr& req, const std::string& verb)
{
    std::map<std::string, std::string> options;
    for (const std::string& name : SymbolControl::OptionsFor(verb))
    {
        const std::string value = req->getParameter(name);
        if (!value.empty())
            options[name] = value;
    }
    return options;
}

/// A JSON body's members as options (null: absent; an array of strings: a comma list, as "sets"). False after an
/// answer when a member is an object or holds one
bool OptionsFromJson(const HttpRequestPtr& req, std::map<std::string, std::string>& options,
                     std::function<void(const HttpResponsePtr&)>& callback)
{
    auto json = req->getJsonObject();
    if (!json || !json->isObject())
        return true;
    for (const std::string& name : json->getMemberNames())
    {
        const Json::Value& v = (*json)[name];
        if (v.isNull())
            continue;
        if (v.isArray())
        {
            std::string list;
            for (const Json::Value& item : v)
            {
                if (!item.isString())
                {
                    RespondError(HttpStatusCode::k400BadRequest, "Bad Request", "'" + name + "' must be a list of strings", callback);
                    return false;
                }
                list += (list.empty() ? "" : ",") + item.asString();
            }
            options[name] = list;
            continue;
        }
        if (v.isObject())
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

void EmulatorAPI::symbolFormats(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    (void)req;
    Respond(SymbolControl(static_cast<LabelManager*>(nullptr)).Execute({"formats", {}}), callback);
}

void EmulatorAPI::symbolFormatsOf(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    (void)req;
    RespondSymbols(id, "formats", {}, callback);
}

void EmulatorAPI::symbolDetect(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    RespondSymbols(id, "detect", OptionsFromQuery(req, "detect"), callback);
}

void EmulatorAPI::symbolSets(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    (void)req;
    RespondSymbols(id, "sets", {}, callback);
}

void EmulatorAPI::symbolSetChange(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    RespondSymbols(id, "set", std::move(options), callback);
}

void EmulatorAPI::symbolSetDrop(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    RespondSymbols(id, "drop", OptionsFromQuery(req, "drop"), callback);
}

void EmulatorAPI::symbolImport(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    RespondSymbols(id, "import", std::move(options), callback);
}

void EmulatorAPI::symbolExport(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    RespondSymbols(id, "export", std::move(options), callback);
}

}  // namespace v1
}  // namespace api
