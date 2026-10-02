// CD audio API: the ATAPI CD drives' audio over HTTP (PLAN #83).
// A thin adapter over CdAudioControl (core/src/emulator/io/ide/cdaudiocontrol.h):
// the request becomes a CdAudioRequest, the CdAudioReply is the body and its status.
//
//   GET  /api/v1/emulator/{id}/state/cdaudio          every CD drive: disc, tracks, audio status, head, volume, mixer row
//   POST /api/v1/emulator/{id}/cdaudio/{verb}         status | play | pause | resume | stop | volume | mixer;
//                                                     options as JSON body keys or query parameters ("drive" picks the drive)

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/ide/cdaudiocontrol.h>
#include <emulator/state/statenodejson.h>

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
    void Respond(std::function<void(const HttpResponsePtr&)>& callback, int status, const std::string& body)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(static_cast<HttpStatusCode>(status));
        resp->setContentTypeCode(CT_APPLICATION_JSON);
        resp->setBody(body);
        addCorsHeaders(resp);
        callback(resp);
    }

    void Error(std::function<void(const HttpResponsePtr&)>& callback, int status, const std::string& message)
    {
        Json::Value error;
        error["ok"] = false;
        error["error"] = status == 404 ? "unknown-emulator" : "bad-request";
        error["message"] = message;
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        Respond(callback, status, Json::writeString(writer, error));
    }

    /// No emulator for an active-emulator route: none at all, or several and none selected
    std::shared_ptr<Emulator> NoActiveEmulator(std::function<void(const HttpResponsePtr&)>& callback, const std::string& route)
    {
        const size_t count = EmulatorManager::GetInstance()->GetEmulatorIds().size();
        Error(callback, count == 0 ? 404 : 400,
              count == 0 ? "No emulator available (none running)" : "Multiple emulators running. Please specify emulator ID in path: " + route);
        return nullptr;
    }

    std::string OptionText(const Json::Value& value)
    {
        if (value.isBool())
            return value.asBool() ? "true" : "false";
        if (value.isNull())
            return "";
        return value.asString();
    }

    CdAudioRequest RequestFrom(const HttpRequestPtr& req, const std::string& verb)
    {
        CdAudioRequest request;
        request.verb = verb;
        auto take = [&request](const std::string& name, const std::string& value) {
            if (name == "drive")
                request.drive = value;
            else
                request.options[name] = value;
        };
        for (const auto& [name, value] : req->getParameters())
            take(name, value);
        if (auto json = req->getJsonObject())
        {
            for (const std::string& name : json->getMemberNames())
                take(name, OptionText((*json)[name]));
        }
        return request;
    }
}  // namespace

/// @brief GET /api/v1/emulator/{id}/state/cdaudio - every CD drive's audio (CdAudioControl::State)
void EmulatorAPI::getStateCdAudio(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return Error(callback, 404, "Emulator not found with ID: " + id);
    const StateNode state = CdAudioControl::State(emulator->GetContext());
    Respond(callback, 200, StateNodeToJsonText(state));
}

void EmulatorAPI::getStateCdAudioActive(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();
    if (!emulator)
        emulator = NoActiveEmulator(callback, "/api/v1/emulator/{id}/state/cdaudio");
    if (!emulator)
        return;
    getStateCdAudio(req, std::move(callback), emulator->GetId());
}

/// @brief POST /api/v1/emulator/{id}/cdaudio/{verb} - a CD audio verb (CdAudioControl::Execute)
void EmulatorAPI::postCdAudioVerb(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id, const std::string& verb) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
    {
        Respond(callback, 404, R"({"ok":false,"error":"unknown-emulator","message":"Emulator not found"})");
        return;
    }
    const CdAudioReply reply = CdAudioControl(emulator->GetContext()).Execute(RequestFrom(req, verb));
    Respond(callback, reply.HttpStatus(), reply.ToJson());
}

void EmulatorAPI::postCdAudioVerbActive(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                        const std::string& verb) const
{
    auto emulator = getEmulatorWithGlobalSelection();
    if (!emulator)
        emulator = NoActiveEmulator(callback, "/api/v1/emulator/{id}/cdaudio/{verb}");
    if (!emulator)
        return;
    postCdAudioVerb(req, std::move(callback), emulator->GetId(), verb);
}

}  // namespace v1
}  // namespace api
