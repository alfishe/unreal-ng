/// @file signature_api.cpp
/// @brief WebAPI media signatures (core/swsignature, Steam Deck client input-and-profiles.md §4):
///   GET /api/v1/signature?path=&details=&external=      the signatures of a host file
///   GET /api/v1/signature/compare?path=&with=           a known title against a candidate: exact / content / fuzzy / memory
///   GET /api/v1/emulator/{id}/signature?details=        the signatures of the media in the machine's tape and floppy slots
/// Every route is a SignatureControl verb, shared with the CLI, MCP and Python (core/swsignature/src/signaturecontrol.h);
/// the game profile a signature chooses is the controller host verb `match` (GET /api/v1/controllers/match).

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <json/json.h>

#include <map>
#include <string>

#include "../common/statenode_json.h"
#include "../emulator_api.h"
#ifdef ENABLE_SWSIGNATURE
#include "signaturecontrol.h"
#endif

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

extern void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
void RespondJson(int status, const Json::Value& body, std::function<void(const HttpResponsePtr&)>& callback)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(status));
    addCorsHeaders(resp);
    callback(resp);
}

void RespondError(int status, const std::string& error, const std::string& message,
                  std::function<void(const HttpResponsePtr&)>& callback)
{
    Json::Value body;
    body["ok"] = false;
    body["error"] = error;
    body["message"] = message;
    RespondJson(status, body, callback);
}

#ifdef ENABLE_SWSIGNATURE
void RespondVerb(EmulatorContext* context, const std::string& verb, const HttpRequestPtr& req,
                 std::function<void(const HttpResponsePtr&)>& callback)
{
    unrealng::swsignature::SignatureRequest request;
    request.verb = verb;
    for (const auto& [name, value] : req->getParameters())
        request.options[name] = value;
    const unrealng::swsignature::SignatureReply reply = unrealng::swsignature::SignatureControl(context).Execute(request);
    RespondJson(reply.HttpStatus(), StateNodeToJson(reply.ToValue()), callback);
}
#endif
}  // namespace

void EmulatorAPI::signatureFile(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
#ifdef ENABLE_SWSIGNATURE
    RespondVerb(nullptr, "file", req, callback);
#else
    (void)req;
    RespondError(503, "unavailable", "built without the signature library (ENABLE_SWSIGNATURE)", callback);
#endif
}

void EmulatorAPI::signatureCompare(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
#ifdef ENABLE_SWSIGNATURE
    RespondVerb(nullptr, "compare", req, callback);
#else
    (void)req;
    RespondError(503, "unavailable", "built without the signature library (ENABLE_SWSIGNATURE)", callback);
#endif
}

void EmulatorAPI::signatureMedia(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
    {
        RespondError(404, "unknown-emulator", "Emulator not found with ID: " + id, callback);
        return;
    }
    if (emulator->IsDestroying())
    {
        RespondError(503, "unavailable", "Emulator is shutting down", callback);
        return;
    }
#ifdef ENABLE_SWSIGNATURE
    RespondVerb(emulator->GetContext(), "media", req, callback);
#else
    (void)req;
    RespondError(503, "unavailable", "built without the signature library (ENABLE_SWSIGNATURE)", callback);
#endif
}

}  // namespace v1
}  // namespace api
