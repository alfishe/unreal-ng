// WebAPI Video Recording Implementation
// Implements the /video/record endpoints (MCP automation, M7k) over capture::RecordingControl, the core layer the CLI,
// Lua, Python and the catalogue actions use too (presets, modes, the option rules)

#include <drogon/HttpResponse.h>
#include <emulator/capture/recordingcontrol.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

#include <filesystem>
#include <map>
#include <string>
#include <system_error>

#include "../common/statenode_json.h"
#include "../emulator_api.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
void Respond(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body, int status)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(status));
    addCorsHeaders(resp);
    callback(resp);
}

void RespondReply(std::function<void(const HttpResponsePtr&)>& callback, const capture::RecordingReply& reply)
{
    Json::Value body = StateNodeToJson(reply.body);
    if (!body.isObject())
        body = Json::Value(Json::objectValue);
    if (!reply.ok)
    {
        body["error"] = reply.error;
        body["message"] = reply.message;
    }
    Respond(callback, body, reply.HttpStatus());
}

/// The recordings a request without a filename writes: <temp>/unreal-mcp, as before
std::string DefaultFolder()
{
    std::error_code ec;
    return (std::filesystem::temp_directory_path(ec) / "unreal-mcp").string();
}

/// The body's fields as RecordingControl options (a string as it is, a number or a bool as text); fields it does not
/// take are left out, as the route always did
std::map<std::string, std::string> OptionsOf(const Json::Value& json)
{
    std::map<std::string, std::string> options;
    if (!json.isObject())
        return options;
    for (const std::string& name : capture::RecordingControl::OptionsFor("start"))
    {
        if (!json.isMember(name) || json[name].isNull())
            continue;
        const Json::Value& value = json[name];
        if (value.isString())
            options[name] = value.asString();
        else if (value.isBool())
            options[name] = value.asBool() ? "true" : "false";
        else if (value.isIntegral())
            options[name] = std::to_string(value.asInt64());
        else if (value.isNumeric())
            options[name] = std::to_string(value.asDouble());
    }
    return options;
}
}  // namespace

/// @brief POST /api/v1/emulator/{id}/video/record
/// @brief Video recording control over the RecordingManager (capture::RecordingControl)
/// @brief Request body: {"action":"start|stop|pause|resume|toggle|status",
///        "preset":"gameplay|music|audio|gif|hq|multitrack" (GET /api/v1/recording/presets; the fields below win),
///        "mode":"single|multi|audio", "format":"gif"|"h264"|"h265"|"vp9"|"rawvideo" (default gif; none with mode
///        audio), "container":"mp4|mov|mkv|webm|avi|gif|wav|flac|mp3|ogg" (the extension when no filename),
///        "profile":"native"|"1080p"|"1440p"|"4k" (a fixed frame, h264/h265 only, sharp nearest scale),
///        "acceleration":"auto"|"hardware"|"software", "fps":50, "scale":1..4, "region":"full"|"screen",
///        "filename":"...", "audio":"aac" (optional; default none = video only), "video_bitrate":kbps,
///        "audio_bitrate":kbps}
void EmulatorAPI::videoRecord(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";
        Respond(callback, error, 404);
        return;
    }
    auto json = req->getJsonObject();
    const std::string action = json && json->isMember("action") ? (*json)["action"].asString() : "start";
    capture::RecordingControl control(*emulator, DefaultFolder());
    RespondReply(callback, control.Execute(action, json ? OptionsOf(*json) : std::map<std::string, std::string>{}));
}

/// @brief GET /api/v1/emulator/{id}/video/record/status
/// @brief Current recording state + live statistics
void EmulatorAPI::videoRecordStatus([[maybe_unused]] const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";
        Respond(callback, error, 404);
        return;
    }
    capture::RecordingControl control(*emulator, DefaultFolder());
    RespondReply(callback, control.Execute("status"));
}

/// @brief GET /api/v1/recording/presets
/// @brief The core's recording presets (the Qt Quick Record menu shows the same ones)
void EmulatorAPI::recordingPresets([[maybe_unused]] const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback) const
{
    Json::Value body(Json::objectValue);
    body["presets"] = StateNodeToJson(capture::RecordingPresetsValue());
    Respond(callback, body, 200);
}

}  // namespace v1
}  // namespace api
