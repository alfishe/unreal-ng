// Media API: the drive collection over HTTP.
// Every route is a thin adapter over MediaControl (core/src/emulator/media/mediacontrol.h):
// the request becomes a MediaRequest, the MediaReply is the response body and its status.
// Design: docs/inprogress/2026-09-28-storage-manager/media-control-design.md §3.9

#include "../emulator_api.h"
#include "../common/upload_helper.h"

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/media/mediacontrol.h>

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

    void RespondReply(std::function<void(const HttpResponsePtr&)>& callback, const MediaReply& reply)
    {
        Respond(callback, reply.HttpStatus(), reply.ToJson());
    }

    /// The emulator, or a 404 in the media envelope
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
        return value.asString();  // numbers and strings alike
    }

    /// Options from the JSON body (every key but "path") and the query string
    void CollectOptions(const HttpRequestPtr& req, MediaRequest& request)
    {
        for (const auto& [name, value] : req->getParameters())
        {
            if (name == "path")
                request.path = value;
            else
                request.options[name] = value;
        }
        if (auto json = req->getJsonObject())
        {
            for (const std::string& name : json->getMemberNames())
            {
                if (name == "path")
                    request.path = (*json)[name].asString();
                else
                    request.options[name] = OptionText((*json)[name]);
            }
        }
    }
}  // namespace

/// @brief GET /api/v1/emulator/{id}/media
void EmulatorAPI::getMediaList(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    (void)req;
    auto emulator = FindEmulator(id, callback);
    if (!emulator)
        return;
    MediaRequest request;
    request.verb = "list";
    RespondReply(callback, MediaControl(emulator->GetContext()).Execute(request));
}

/// @brief GET /api/v1/emulator/{id}/media/{slot}  (and /media/formats)
void EmulatorAPI::getMediaSlot(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id, const std::string& slot) const
{
    auto emulator = FindEmulator(id, callback);
    if (!emulator)
        return;
    MediaRequest request;
    if (slot == "formats")
    {
        request.verb = "formats";
        const std::string kind = req->getParameter("kind");
        if (!kind.empty())
            request.options["kind"] = kind;
    }
    else
    {
        request.verb = "info";
        request.selector = slot;
    }
    RespondReply(callback, MediaControl(emulator->GetContext()).Execute(request));
}

/// @brief POST /api/v1/emulator/{id}/media/{slot}/{verb}
/// verb: insert, swap, eject, save, export, discard, rescan, create, protect.
/// insert / swap also take the file itself (multipart/form-data or a raw body
/// with X-Filename), staged like the /disk upload and deleted on eject
void EmulatorAPI::postMediaVerb(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id, const std::string& slot, const std::string& verb) const
{
    auto emulator = FindEmulator(id, callback);
    if (!emulator)
        return;

    MediaRequest request;
    request.verb = verb;
    request.selector = slot;
    CollectOptions(req, request);

    // An uploaded file instead of a path
    const bool takesSource = verb == "insert" || verb == "swap";
    if (takesSource && request.path.empty() && !req->getJsonObject())
    {
        auto content = extractMediaContent(req, MediaType::Disk);
        if (!content.valid)
        {
            MediaReply bad;
            bad.result = MediaResult::Fail(MediaError::BadRequest, content.errorMsg);
            RespondReply(callback, bad);
            return;
        }
        if (content.isEmbedded)
        {
            std::string stageError;
            request.path = UploadHelper::Instance().stageUpload(content.data, content.filename, MediaType::Disk, stageError);
            if (request.path.empty())
            {
                MediaReply failed;
                failed.result = MediaResult::Fail(MediaError::IoError, "failed to stage the upload: " + stageError);
                RespondReply(callback, failed);
                return;
            }
            request.upload = true;
        }
        else
        {
            request.path = content.path;
        }
    }

    RespondReply(callback, MediaControl(emulator->GetContext()).Execute(request));
}

}  // namespace v1
}  // namespace api
