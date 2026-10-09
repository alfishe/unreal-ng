/// @file asm_api.cpp
/// @brief WebAPI assembler-source endpoints (unreal-asm tdd §7, architecture.md §8):
///   GET  /api/v1/asm/formats                  the source codecs and their versions
///   GET  /api/v1/asm/dialects                 the dialects that can be read and written
///   GET  /emulator/{id}/asm/files?drive=A     the files on a disk, each with the format detected
///   POST /emulator/{id}/asm/detect  {path | data + name, file?}
///   POST /emulator/{id}/asm/decode  {path | data + name, file?, codec?, version?, codepage?, output?}
///   POST /emulator/{id}/asm/encode  {text | input, codec, version?, codepage?, lineend?, output?, start?}
///   POST /emulator/{id}/asm/convert {path | data + name, file?, to, codec?, version?, from?, z80n?, output?}
///   GET  /emulator/{id}/asm/sync               the assembler running in the machine and its text (?assembler=)
///   POST /emulator/{id}/asm/sync/probe         every assembler that identifies in RAM
///   POST /emulator/{id}/asm/sync/extract {assembler?, as: text | file | dialect, to?, codepage?, output?}
/// A path is a host file or disk:A/NAME.T. Every route turns its request into an AsmControl verb, shared with the CLI,
/// MCP, Lua, Python and the Qt disk browser (core/src/debugger/asm/asmcontrol.h).

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <json/json.h>

#include <map>
#include <string>

#include "../common/statenode_json.h"
#include "../emulator_api.h"
#include "debugger/asm/asmcontrol.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

extern void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
void Respond(const AsmReply& reply, std::function<void(const HttpResponsePtr&)>& callback)
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

void RespondAsm(const std::string& id, const std::string& verb, std::map<std::string, std::string> options,
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
    Respond(AsmControl(emulator->GetContext()).Execute({verb, std::move(options)}), callback);
}

/// A JSON body's scalar members as options (null: absent); false after an answer when a member is an object or array
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
        if (v.isObject() || v.isArray())
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

void EmulatorAPI::asmFormats(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    (void)req;
    Respond(AsmControl(nullptr).Execute({"formats", {}}), callback);
}

void EmulatorAPI::asmDialects(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    (void)req;
    Respond(AsmControl(nullptr).Execute({"dialects", {}}), callback);
}

void EmulatorAPI::asmFiles(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (!req->getParameter("drive").empty())
        options["drive"] = req->getParameter("drive");
    RespondAsm(id, "files", std::move(options), callback);
}

void EmulatorAPI::asmVerb(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id,
                          const std::string& verb) const
{
    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    RespondAsm(id, verb, std::move(options), callback);
}

void EmulatorAPI::asmSyncStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (!req->getParameter("assembler").empty())
        options["assembler"] = req->getParameter("assembler");
    RespondAsm(id, "sync-status", std::move(options), callback);
}

void EmulatorAPI::asmSync(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id,
                          const std::string& action) const
{
    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    RespondAsm(id, "sync-" + action, std::move(options), callback);
}

}  // namespace v1
}  // namespace api
