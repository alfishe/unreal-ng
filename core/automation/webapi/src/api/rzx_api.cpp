/// @brief WebAPI RZX playback endpoints (docs/inprogress/2026-09-29-rzx-replay,
/// design §13): play a recording (path or upload), stop, status.
///
/// Example: POST /api/v1/emulator/e1/rzx/play {"path": "/games/eric.rzx"} on a
/// 48K plays at once; with a 128K recording the model switches first and the
/// answer names the new emulator ("emulator_id", "model_switched": true).

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/rzx/rzxlauncher.h>
#include <json/json.h>

#include "../common/upload_helper.h"
#include "../emulator_api.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

extern void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
    void Reply(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body, HttpStatusCode code)
    {
        auto resp = HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(code);
        addCorsHeaders(resp);
        callback(resp);
    }

    void ReplyError(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode code,
                    const std::string& error, const std::string& message)
    {
        Json::Value body;
        body["error"] = error;
        body["message"] = message;
        Reply(callback, body, code);
    }

    /// The playback status as JSON (shared by play, stop and status)
    Json::Value StatusJson(const rzx::SessionStatus& status)
    {
        Json::Value json;
        json["loaded"] = status.loaded;
        json["active"] = status.active;
        if (!status.loaded)
            return json;

        const rzx::PlayerStatus& player = status.player;
        json["path"] = status.path;
        json["creator"] = status.creator;
        json["version"] = status.version;
        json["snapshot"] = status.snapshot;
        json["state"] = rzx::StateName(player.state);
        json["frame"] = Json::UInt64(player.frame);
        json["total_frames"] = Json::UInt64(player.totalFrames);
        json["progress"] = player.totalFrames ? static_cast<double>(player.frame) / player.totalFrames : 0.0;
        json["block"] = player.block + 1;
        json["blocks"] = player.blocks;
        json["interrupts"] = Json::UInt64(player.interrupts);
        json["desyncs"] = Json::UInt64(player.desyncs);
        json["drift"] = player.drift;
        json["max_drift"] = player.maxDrift;
        json["keyframes"] = Json::UInt64(player.keyframes);
        json["keyframe_bytes"] = Json::UInt64(player.keyframeBytes);
        json["keyframe_interval"] = player.keyframeInterval;
        if (!player.stopReason.empty())
            json["reason"] = player.stopReason;
        if (player.desyncs > 0)
        {
            const rzx::Desync& d = player.firstDesync;
            Json::Value first;
            first["kind"] = rzx::DesyncName(d.kind);
            first["frame"] = Json::UInt64(d.frame);
            first["block"] = d.block + 1;
            first["expected"] = d.expected;
            first["actual"] = d.actual;
            first["pc"] = d.pc;
            if (d.kind == rzx::DesyncKind::TooManyIns)
                first["port"] = d.port;
            json["first_desync"] = first;
        }

        Json::Value options;
        options["desync_mode"] = rzx::RzxLauncher::DesyncModeName(status.options.desyncMode);
        options["ei_short_frame_blocks_int"] = status.options.eiShortFrameBlocksInt;
        options["ld_air_parity_quirk"] = status.options.ldAirParityQuirk;
        options["ignore_later_snapshots"] = status.options.ignoreLaterSnapshots;
        json["options"] = options;
        return json;
    }

    /// Options from a JSON body (absent fields keep their defaults); false
    /// with `error` on a bad value
    bool ParseOptions(const Json::Value& body, rzx::PlayerOptions& options, bool& switchModel, std::string& error)
    {
        if (body.isMember("desync_mode") &&
            !rzx::RzxLauncher::ParseDesyncMode(body["desync_mode"].asString(), options.desyncMode))
        {
            error = "desync_mode '" + body["desync_mode"].asString() + "': expected strict or tolerant";
            return false;
        }
        if (body.isMember("ei_short_frame_blocks_int"))
            options.eiShortFrameBlocksInt = body["ei_short_frame_blocks_int"].asBool();
        if (body.isMember("ld_air_parity_quirk"))
            options.ldAirParityQuirk = body["ld_air_parity_quirk"].asBool();
        if (body.isMember("ignore_later_snapshots"))
            options.ignoreLaterSnapshots = body["ignore_later_snapshots"].asBool();
        if (body.isMember("switch_model"))
            switchModel = body["switch_model"].asBool();
        return true;
    }

    /// A boolean query / header option ("true", "1")
    bool FlagFrom(const HttpRequestPtr& req, const std::string& name, bool fallback)
    {
        std::string value = req->getParameter(name);
        if (value.empty())
            value = req->getHeader("X-Rzx-" + name);
        if (value.empty())
            return fallback;
        return value == "true" || value == "1";
    }
}  // namespace

/// @brief POST /api/v1/emulator/:id/rzx/play
void EmulatorAPI::playRzx(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                          const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
        return ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator not found");
    if (emulator->IsDestroying())
        return ReplyError(callback, HttpStatusCode::k503ServiceUnavailable, "Service Unavailable",
                          "Emulator is shutting down");

    // Path in a JSON body, or the file itself (multipart / raw body)
    auto content = extractMediaContent(req, MediaType::Snapshot);
    if (!content.valid)
        return ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", content.errorMsg);

    rzx::LaunchRequest request;
    request.emulatorId = emulator->GetId();
    emulator.reset();

    // Options: JSON body fields, or query parameters / X-Rzx-* headers with an upload
    if (auto json = req->getJsonObject())
    {
        std::string error;
        if (!ParseOptions(*json, request.options, request.switchModel, error))
            return ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", error);
    }
    else
    {
        const std::string mode = req->getParameter("desync_mode");
        if (!mode.empty() && !rzx::RzxLauncher::ParseDesyncMode(mode, request.options.desyncMode))
            return ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request",
                              "desync_mode '" + mode + "': expected strict or tolerant");
        request.options.eiShortFrameBlocksInt = FlagFrom(req, "ei_short_frame_blocks_int", false);
        request.options.ldAirParityQuirk = FlagFrom(req, "ld_air_parity_quirk", false);
        request.options.ignoreLaterSnapshots = FlagFrom(req, "ignore_later_snapshots", false);
        request.switchModel = FlagFrom(req, "switch_model", true);
    }

    if (content.isEmbedded)
    {
        std::string stageError;
        request.path =
            UploadHelper::Instance().stageUpload(content.data, content.filename, MediaType::Snapshot, stageError);
        if (request.path.empty())
            return ReplyError(callback, HttpStatusCode::k500InternalServerError, "Internal Error",
                              "Failed to stage upload: " + stageError);
    }
    else
    {
        request.path = content.path;
    }

    const rzx::LaunchResult result = rzx::RzxLauncher::Play(request);
    Json::Value ret;
    ret["status"] = result.play.Ok() ? "success" : "error";
    ret["path"] = request.path;
    if (content.isEmbedded)
        ret["uploaded"] = true;
    if (result.emulator)
        ret["emulator_id"] = result.emulator->GetId();
    ret["model_switched"] = result.modelSwitched;
    if (result.modelSwitched)
        ret["previous_emulator_id"] = result.previousEmulatorId;

    if (!result.play.Ok())
    {
        ret["error"] = rzx::PlayErrorName(result.play.error);
        ret["message"] = result.play.message;
        if (result.play.error == rzx::PlayError::ModelMismatch)
        {
            ret["required_model"] = result.play.requiredModel;
            ret["required_ram_kb"] = result.play.requiredRamKb;
        }
        const HttpStatusCode code = result.play.error == rzx::PlayError::ModelMismatch ||
                                            result.play.error == rzx::PlayError::Refused
                                        ? HttpStatusCode::k409Conflict
                                        : HttpStatusCode::k400BadRequest;
        return Reply(callback, ret, code);
    }

    if (result.modelSwitched)
        ret["model"] = result.switchedToModel;
    ret["message"] = result.modelSwitched ? "Model switched to " + result.switchedToModel + "; RZX playing"
                                          : std::string("RZX playing");
    ret["rzx"] = StatusJson(result.emulator->GetRzxStatus());
    Reply(callback, ret, HttpStatusCode::k200OK);
}

/// @brief POST /api/v1/emulator/:id/rzx/stop
void EmulatorAPI::stopRzx(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                          const std::string& id) const
{
    (void)req;
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
        return ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator not found");

    const bool stopped = emulator->StopRzx();
    Json::Value ret;
    ret["status"] = "success";
    ret["stopped"] = stopped;
    ret["message"] = stopped ? "RZX playback stopped; the machine runs live" : "No RZX playback was running";
    ret["rzx"] = StatusJson(emulator->GetRzxStatus());
    Reply(callback, ret, HttpStatusCode::k200OK);
}

/// @brief POST /api/v1/emulator/:id/rzx/seek {"frame": N}
void EmulatorAPI::seekRzx(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                          const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
        return ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator not found");
    auto json = req->getJsonObject();
    if (!json || !json->isMember("frame") || !(*json)["frame"].isIntegral() || (*json)["frame"].asInt64() < 0)
        return ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request",
                          "'frame' (a frame count, 0 = the start) is required");

    std::string error;
    if (!emulator->SeekRzx((*json)["frame"].asUInt64(), &error))
        return ReplyError(callback, HttpStatusCode::k409Conflict, "Conflict", error);
    Json::Value ret;
    ret["status"] = "success";
    ret["rzx"] = StatusJson(emulator->GetRzxStatus());
    ret["summary"] = rzx::RzxLauncher::StatusLine(emulator->GetRzxStatus());
    Reply(callback, ret, HttpStatusCode::k200OK);
}

/// @brief GET /api/v1/emulator/:id/rzx/status
void EmulatorAPI::getRzxStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    (void)req;
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
        return ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator not found");

    Json::Value ret = StatusJson(emulator->GetRzxStatus());
    ret["status"] = "success";
    ret["summary"] = rzx::RzxLauncher::StatusLine(emulator->GetRzxStatus());
    Reply(callback, ret, HttpStatusCode::k200OK);
}

}  // namespace v1
}  // namespace api
