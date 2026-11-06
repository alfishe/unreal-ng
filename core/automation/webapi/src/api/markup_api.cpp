/// @file markup_api.cpp
/// @brief WebAPI routes of the memory markup (docs/inprogress/2026-11-05-software-metadata-profile §5):
///   GET    /markup?from=&to=&range=&layer=&limit=   what the CPU range holds: spans, instruction starts, comments
///   GET    /markup/layers                         every layer counted, the runtime layer's feed, the revision
///   PUT    /markup          {from | range, to?, kind, title?, cpu?}   the user layer: mark a range as a region kind
///   DELETE /markup?from=&to=&range=               the user layer forgets its claims there
///   PUT    /markup/comment  {address, text?}      the user layer's comment (empty text removes it)
///   DELETE /markup/layers?layer=                  empty a layer (user by default)
///   POST   /markup/save-profile {profile?, analysis?, confidence?, keep?}   into a software profile's memory section
///
/// Every route turns its request into a markup::Execute action (core/src/debugger/markup/markupcontrol.h): the
/// checks, the answer and its "text" are shared with the CLI, MCP, Lua and Python. A WebSocket `call` of a route runs
/// the same handler; the `debug.markup` topic pushes the layers' counts on every change.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <json/json.h>

#include <map>
#include <string>

#include "../common/statenode_json.h"
#include "../emulator_api.h"
#include "debugger/markup/markupcontrol.h"

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

void RespondMarkup(const std::string& id, const std::string& action, const markup::Options& options,
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
    const markup::Reply reply = markup::Execute(*emulator, action, options);
    Json::Value body = StateNodeToJson(reply.ToValue());
    if (reply.Ok())
    {
        body["emulator_id"] = id;
        body["action"] = action;
    }
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(reply.HttpStatus()));
    addCorsHeaders(resp);
    callback(resp);
}

/// The query parameters an action takes (others, a cache buster, are ignored)
markup::Options OptionsFromQuery(const HttpRequestPtr& req, const std::string& action)
{
    markup::Options options;
    for (const std::string& name : markup::OptionsFor(action))
    {
        const std::string value = req->getParameter(name);
        if (!value.empty())
            options[name] = value;
    }
    return options;
}

/// The body's members as options (strings, numbers, booleans; analysis also as an array of kinds). False after an
/// answer for any other value
bool OptionsFromJson(const HttpRequestPtr& req, markup::Options& options,
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
        if (v.isArray() && name == "analysis")
        {
            std::string kinds;
            for (const Json::Value& kind : v)
                kinds += (kinds.empty() ? "" : ",") + kind.asString();
            options[name] = kinds;
            continue;
        }
        if (v.isObject() || v.isArray())
        {
            RespondError(HttpStatusCode::k400BadRequest, "Bad Request",
                         "'" + name + "' must be a number, a string or true / false", callback);
            return false;
        }
        options[name] = v.isBool() ? (v.asBool() ? "true" : "false") : v.asString();
    }
    return true;
}
}  // namespace

void EmulatorAPI::markupShow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    RespondMarkup(id, "show", OptionsFromQuery(req, "show"), callback);
}

void EmulatorAPI::markupLayers(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    (void)req;
    RespondMarkup(id, "layers", {}, callback);
}

void EmulatorAPI::markupMark(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    markup::Options options;
    if (!OptionsFromJson(req, options, callback))
        return;
    RespondMarkup(id, "mark", options, callback);
}

void EmulatorAPI::markupUnmark(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    RespondMarkup(id, "unmark", OptionsFromQuery(req, "unmark"), callback);
}

void EmulatorAPI::markupComment(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    markup::Options options;
    if (!OptionsFromJson(req, options, callback))
        return;
    RespondMarkup(id, "comment", options, callback);
}

void EmulatorAPI::markupClear(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    RespondMarkup(id, "clear", OptionsFromQuery(req, "clear"), callback);
}

void EmulatorAPI::markupSaveProfile(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id) const
{
    markup::Options options;
    if (!OptionsFromJson(req, options, callback))
        return;
    RespondMarkup(id, "save", options, callback);
}

}  // namespace v1
}  // namespace api
