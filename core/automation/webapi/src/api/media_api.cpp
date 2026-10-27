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
#include <common/base64.h>

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
                else if (name == "descriptor" && (*json)[name].isObject())
                {
                    // A composition descriptor inline: MediaControl takes its JSON text as the path
                    Json::StreamWriterBuilder writer;
                    writer["indentation"] = "";
                    request.path = Json::writeString(writer, (*json)[name]);
                }
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

/// @brief GET /api/v1/emulator/{id}/media/{slot}  (and /media/formats, /media/targets?path=,
/// /media/compose?path=<descriptor file or JSON text>[&fs=&codepage=&free=])
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
    else if (slot == "targets")
    {
        request.verb = "targets";
        request.path = req->getParameter("path");
    }
    else if (slot == "compose")
    {
        request.verb = "compose";
        request.path = req->getParameter("path");
        for (const char* option : {"fs", "codepage", "free"})
        {
            const std::string value = req->getParameter(option);
            if (!value.empty())
                request.options[option] = value;
        }
    }
    else
    {
        request.verb = "info";
        request.selector = slot;
    }
    RespondReply(callback, MediaControl(emulator->GetContext()).Execute(request));
}

/// @brief GET /api/v1/emulator/{id}/media/{slot}/{verb}: the read-only file verbs
/// volumes, files (?path=<dir>&volume=&recursive=true) and read (?path=<file>&volume=[&out=<host file>]);
/// read with raw=true answers the file's bytes (application/octet-stream) instead of base64 in the envelope
void EmulatorAPI::getMediaFiles(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id, const std::string& slot, const std::string& verb) const
{
    auto emulator = FindEmulator(id, callback);
    if (!emulator)
        return;
    if (verb != "volumes" && verb != "files" && verb != "read")
    {
        MediaReply bad;
        bad.result = MediaResult::Fail(MediaError::BadRequest, "GET takes volumes, files and read; '" + verb + "' is a POST");
        RespondReply(callback, bad);
        return;
    }
    MediaRequest request;
    request.verb = verb;
    request.selector = slot;
    CollectOptions(req, request);
    bool raw = false;
    if (auto it = request.options.find("raw"); it != request.options.end())
    {
        raw = verb == "read" && (it->second.empty() || it->second == "true" || it->second == "1");
        request.options.erase(it);
    }
    const MediaReply reply = MediaControl(emulator->GetContext()).Execute(request);
    const StateNode* data = reply.body.find("data");
    if (!raw || !reply.result.Ok() || !data)
    {
        RespondReply(callback, reply);
        return;
    }
    std::vector<uint8_t> bytes;
    base64::Decode(data->s, bytes);
    auto resp = HttpResponse::newHttpResponse();
    resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
    resp->setBody(std::string(bytes.begin(), bytes.end()));
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/media/{slot}/{verb}
/// verb: insert, swap, eject, save, export, discard, rescan, create, protect, layers, changes, flatten, volumes,
/// files, read, write (the bytes as base64 'data', a host file 'in', or an application/octet-stream body), delete,
/// rename ('to'), mkdir ('parents').
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

    // write: the file's bytes as the body itself (application/octet-stream), ?path=NAME.T
    if (verb == "write" && req->contentType() == CT_APPLICATION_OCTET_STREAM && !request.options.count("data") &&
        !request.options.count("in"))
    {
        const std::string_view body = req->body();
        request.options["data"] = base64::Encode(std::vector<uint8_t>(body.begin(), body.end()));
    }

    RespondReply(callback, MediaControl(emulator->GetContext()).Execute(request));
}

}  // namespace v1
}  // namespace api
