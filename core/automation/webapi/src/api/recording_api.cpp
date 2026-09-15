// WebAPI Video Recording Implementation
// Implements /video/record endpoints (MCP automation, M7k)

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/emulatorcontext.h>
#include <base/featuremanager.h>
#include <json/json.h>

#ifdef ENABLE_RECORDING
#include "recordingmanager.h"
#endif

#include <atomic>
#include <ctime>
#include <filesystem>
#include <string>

#include "../emulator_api.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

#ifdef ENABLE_RECORDING

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);

/// Helper: Get emulator or return 404
static std::shared_ptr<Emulator> getEmulatorOrError(const std::string& id,
                                                   std::function<void(const HttpResponsePtr&)>& callback)
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
    }

    return emulator;
}

namespace
{
/// Append recording stats to a JSON object (shared by status/stop responses)
void appendRecordingStats(Json::Value& target, const RecordingManager* rm)
{
    const RecordingManager::RecordingStats stats = rm->GetStats();
    target["frames_recorded"] = static_cast<Json::UInt64>(stats.framesRecorded);
    target["audio_samples_recorded"] = static_cast<Json::UInt64>(stats.audioSamplesRecorded);
    target["recorded_duration"] = stats.recordedDuration;
    target["emulated_duration"] = stats.emulatedDuration;
    target["output_file_size"] = static_cast<Json::UInt64>(stats.outputFileSize);
    target["average_frame_time_ms"] = stats.averageFrameTime;
    target["recent_fps"] = stats.recentFps;
}

/// Default output: tempdir/unreal-mcp/video-<safe-emulator-id>-<stamp>.<ext>
std::string defaultRecordingPath(const std::string& id, const std::string& extension)
{
    std::string safeId = id;
    for (char& c : safeId)
    {
        if (c == '-')
            c = '_';
    }

    static std::atomic<unsigned> counter{0};
    const long long stamp = static_cast<long long>(std::time(nullptr)) * 1000 + (counter++ % 1000);

    std::filesystem::path dir = std::filesystem::temp_directory_path() / "unreal-mcp";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    return (dir / ("video-" + safeId + "-" + std::to_string(stamp) + "." + extension)).string();
}

/// Attach output file existence/size info to a response object
void appendOutputFile(Json::Value& target, const std::string& path)
{
    target["output"] = path;
    if (!path.empty())
    {
        std::error_code ec;
        target["file_exists"] = std::filesystem::exists(path, ec);
        target["file_size"] = ec || !std::filesystem::is_regular_file(path, ec) ?
                                  0 : static_cast<Json::UInt64>(std::filesystem::file_size(path, ec));
    }
}
}  // namespace

/// @brief POST /api/v1/emulator/{id}/video/record
/// @brief Video recording control over the RecordingManager
/// @brief Request body: {"action":"start|stop|pause|resume",
///        "format":"gif" (native; mp4/avi/mkv depend on ffmpeg availability),
///        "fps":50, "scale":1..4, "region":"full"|"screen", "filename":"..."}
void EmulatorAPI::videoRecord(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    try
    {
        auto json = req->getJsonObject();
        const std::string action = json && json->isMember("action") ? (*json)["action"].asString() : "start";

        EmulatorContext* context = emulator->GetContext();
        RecordingManager* rm = context ? context->pRecordingManager : nullptr;
        if (!rm)
        {
            Json::Value error;
            error["error"] = "Internal Server Error";
            error["message"] = "Recording manager not available for this emulator";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k500InternalServerError);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        if (action == "start")
        {
            if (rm->IsRecording() || rm->IsPaused())
            {
                Json::Value error;
                error["error"] = "Conflict";
                error["message"] = "A recording is already active — stop it first";

                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k409Conflict);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }

            const std::string format = json && json->isMember("format") && (*json)["format"].isString() ?
                                           (*json)["format"].asString() : "gif";

            // Map codec-style names to container extensions for the default filename
            std::string extension = format;
            if (format == "h264" || format == "h265" || format == "hevc" || format == "vp9")
                extension = "mp4";
            else if (format == "rawvideo")
                extension = "avi";

            const std::string filename =
                json && json->isMember("filename") && !(*json)["filename"].asString().empty() ?
                    (*json)["filename"].asString() : defaultRecordingPath(id, extension);

            // Configuration setters refuse changes mid-recording — apply while paused
            const bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
            if (wasRunning)
            {
                emulator->Pause();
            }

            float fps = json && json->isMember("fps") ? (*json)["fps"].asFloat() : 50.0f;
            if (fps < 1.0f) fps = 1.0f;
            if (fps > 100.0f) fps = 100.0f;
            rm->SetVideoFrameRate(fps);

            uint32_t scale = 1;
            if (json && json->isMember("scale"))
            {
                scale = (*json)["scale"].asUInt();
                if (scale < 1) scale = 1;
                if (scale > 4) scale = 4;
                rm->SetScaleFactor(scale);
            }

            const std::string region = json && json->isMember("region") ? (*json)["region"].asString() : "full";
            const VideoCaptureRegion captureRegion =
                (region == "screen" || region == "main") ? VideoCaptureRegion::MainScreen : VideoCaptureRegion::FullFrame;
            rm->SetCaptureRegion(captureRegion);

            // Auto-enable the 'recording' feature (StartRecording fails cleanly without it)
            FeatureManager* fm = context->pFeatureManager;
            const bool featureWasOff = fm && !fm->isEnabled(Features::kRecording);
            if (featureWasOff)
            {
                fm->setFeature(Features::kRecording, true);
            }

            // Video-only recording (empty audio codec; GIF has no audio track)
            const bool started = rm->StartRecording(filename, format, "");

            if (wasRunning)
            {
                emulator->Resume();
            }

            if (!started)
            {
                if (featureWasOff)
                {
                    fm->setFeature(Features::kRecording, false);
                }

                Json::Value error;
                error["error"] = "Recording start failed";
                error["message"] = rm->GetLastRecordingError();

                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k500InternalServerError);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }

            Json::Value ret;
            ret["status"] = "success";
            ret["message"] = "Recording started — run the emulator to collect frames";
            ret["recording"] = true;
            ret["format"] = format;
            ret["fps"] = fps;
            ret["scale"] = scale;
            ret["region"] = captureRegion == VideoCaptureRegion::MainScreen ? "screen" : "full";
            ret["feature_auto_enabled"] = featureWasOff;
            appendOutputFile(ret, filename);

            auto resp = HttpResponse::newHttpJsonResponse(ret);
            addCorsHeaders(resp);
            callback(resp);
        }
        else if (action == "stop")
        {
            if (!rm->IsRecording() && !rm->IsPaused())
            {
                Json::Value error;
                error["error"] = "Conflict";
                error["message"] = "No active recording to stop";

                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k409Conflict);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }

            rm->StopRecording();

            Json::Value ret;
            ret["status"] = "success";
            ret["message"] = "Recording stopped";
            ret["recording"] = false;
            appendRecordingStats(ret, rm);
            appendOutputFile(ret, rm->GetOutputFilename());

            auto resp = HttpResponse::newHttpJsonResponse(ret);
            addCorsHeaders(resp);
            callback(resp);
        }
        else if (action == "pause")
        {
            if (!rm->IsRecording())
            {
                Json::Value error;
                error["error"] = "Conflict";
                error["message"] = "No active recording to pause";

                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k409Conflict);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }

            rm->PauseRecording();

            Json::Value ret;
            ret["status"] = "success";
            ret["message"] = "Recording paused";
            ret["recording"] = rm->IsRecording();
            ret["paused"] = rm->IsPaused();
            appendOutputFile(ret, rm->GetOutputFilename());

            auto resp = HttpResponse::newHttpJsonResponse(ret);
            addCorsHeaders(resp);
            callback(resp);
        }
        else if (action == "resume")
        {
            if (!rm->IsPaused())
            {
                Json::Value error;
                error["error"] = "Conflict";
                error["message"] = "Recording is not paused";

                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k409Conflict);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }

            rm->ResumeRecording();

            Json::Value ret;
            ret["status"] = "success";
            ret["message"] = "Recording resumed";
            ret["recording"] = rm->IsRecording();
            ret["paused"] = rm->IsPaused();
            appendOutputFile(ret, rm->GetOutputFilename());

            auto resp = HttpResponse::newHttpJsonResponse(ret);
            addCorsHeaders(resp);
            callback(resp);
        }
        else
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "Unknown action '" + action + "' (expected start|stop|pause|resume)";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
        }
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Video recording operation failed";
        error["message"] = e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief GET /api/v1/emulator/{id}/video/record/status
/// @brief Current recording state + live statistics
void EmulatorAPI::videoRecordStatus(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    try
    {
        EmulatorContext* context = emulator->GetContext();
        RecordingManager* rm = context ? context->pRecordingManager : nullptr;
        if (!rm)
        {
            Json::Value error;
            error["error"] = "Internal Server Error";
            error["message"] = "Recording manager not available for this emulator";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k500InternalServerError);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        Json::Value ret;
        ret["emulator_id"] = id;
        ret["recording"] = rm->IsRecording();
        ret["paused"] = rm->IsPaused();
        ret["feature_enabled"] = rm->isFeatureEnabled();
        ret["realtime_capable"] = rm->IsRealtimeCapable();
        if (!rm->GetLastRecordingError().empty())
        {
            ret["last_error"] = rm->GetLastRecordingError();
        }
        appendRecordingStats(ret, rm);
        appendOutputFile(ret, rm->GetOutputFilename());

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Video recording status failed";
        error["message"] = e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

#else  // !ENABLE_RECORDING

/// @brief POST /api/v1/emulator/{id}/video/record (stub — recording support compiled out)
void EmulatorAPI::videoRecord(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    Json::Value error;
    error["error"] = "Not Implemented";
    error["message"] = "Recording support is disabled in this build (ENABLE_RECORDING=OFF)";

    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(HttpStatusCode::k501NotImplemented);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/video/record/status (stub — recording support compiled out)
void EmulatorAPI::videoRecordStatus(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id) const
{
    Json::Value error;
    error["error"] = "Not Implemented";
    error["message"] = "Recording support is disabled in this build (ENABLE_RECORDING=OFF)";

    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(HttpStatusCode::k501NotImplemented);
    addCorsHeaders(resp);
    callback(resp);
}

#endif  // ENABLE_RECORDING

}  // namespace v1
}  // namespace api
