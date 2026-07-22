/// @file ttd_api.cpp
/// @brief WebAPI TTD (Time-Travel Debug) endpoints.
///
/// Per parent TDD §10.4. Full TTD automation surface (Phase 2 complete):
///   GET  /ttd/status      — session info
///   POST /ttd/start       — begin recording
///   POST /ttd/stop        — stop recording (history retained)
///   POST /ttd/invalidate  — drop all history, return to Idle
///   POST /ttd/seek        — seek to (frame, tInFrame)
///   POST /ttd/step-back   — step back one frame
///   POST /ttd/step-forward — step forward one frame
///   POST /ttd/resume      — resume recording from a past point
///   GET  /ttd/position    — current TTDTimePoint
///   GET  /ttd/markers     — list external-event markers (replay barriers)
///
/// The status endpoint surfaces every field of TTDSessionInfo so automation
/// clients and the divergence-test harness can poll the recorder without
/// linking against the TTD headers directly.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

#include <sstream>

#include "../emulator_api.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_external_events.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);
extern std::shared_ptr<Emulator> getEmulatorByIdOrIndex(const std::string& idOrIndex);

/// @brief GET /api/v1/emulator/{id}/ttd/status
///
/// Returns the current TTD session state for the requested emulator instance.
///
/// Response shape (parent TDD §10.4):
/// @code
/// {
///   "state": "idle" | "recording" | "detached",
///   "session_start_frame": <uint64>,
///   "current_end_frame":   <uint64>,
///   "checkpoint_count":    <uint64>,
///   "page_store_bytes":     <uint64>,   // capacity, for budget checks
///   "page_store_used_bytes": <uint64>,  // live slot bytes
///   "baseline_frames_captured": <uint64>
/// }
/// @endcode
///
/// Status codes:
///   - 200 OK on success (state field reflects the actual session state,
///     including "idle" when TTD is not active or the manager is missing)
///   - 404 when the emulator instance is not found
///   - 503 when the emulator is shutting down (IsDestroying)
///
/// Thread-safety: GetSessionInfo() takes a snapshot of the manager's
/// observable state. It is safe to call from the HTTP thread while the
/// emulator thread is mutating the timeline (per TDD §7.2 the only writer
/// to those fields is the emulator thread, and the reads are word-sized).
void EmulatorAPI::getTTDStatus(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"]   = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (emulator->IsDestroying())
    {
        Json::Value error;
        error["error"]   = "Service Unavailable";
        error["message"] = "Emulator is shutting down";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k503ServiceUnavailable);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context)
    {
        Json::Value error;
        error["error"]   = "Internal Error";
        error["message"] = "Unable to access emulator context";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Build a default "idle" payload when the manager is missing (e.g. minimal
    // builds without TTD compiled in). The endpoint is still useful as a
    // capability probe: automation clients can detect "TTD not available"
    // by the absence of progression in session_start_frame / checkpoint_count.
    Json::Value ret;
    ret["state"]                    = ttd::TTDSessionStateToString(ttd::TTDSessionState::Idle);
    ret["session_start_frame"]      = Json::UInt64(0);
    ret["current_end_frame"]        = Json::UInt64(0);
    ret["checkpoint_count"]         = Json::UInt64(0);
    ret["page_store_bytes"]         = Json::UInt64(0);
    ret["page_store_used_bytes"]    = Json::UInt64(0);
    ret["baseline_frames_captured"] = Json::UInt64(0);
    ret["ttd_available"]            = false;

    if (ttd::TimeTravelManager* mgr = context->pTimeTravelManager)
    {
        ttd::TTDSessionInfo info = mgr->GetSessionInfo();
        ret["state"]                    = ttd::TTDSessionStateToString(info.state);
        ret["session_start_frame"]      = Json::UInt64(info.sessionStartFrame);
        ret["current_end_frame"]        = Json::UInt64(info.currentEndFrame);
        ret["checkpoint_count"]         = Json::UInt64(info.checkpointCount);
        ret["page_store_bytes"]         = Json::UInt64(info.pageStoreBytes);
        ret["page_store_used_bytes"]    = Json::UInt64(info.pageStoreUsedBytes);
        ret["baseline_frames_captured"] = Json::UInt64(info.baselineFramesCaptured);
        ret["ttd_available"]            = true;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// ---------------------------------------------------------------------------
// Internal helper: resolve emulator + context + TTD manager, or send error.
// Returns nullptr on failure (error response already sent).
// ---------------------------------------------------------------------------
static ttd::TimeTravelManager* resolveTTD(
    const std::string& id,
    std::function<void(const HttpResponsePtr&)>& callback,
    bool requireManager = true)
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"]   = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return nullptr;
    }

    if (emulator->IsDestroying())
    {
        Json::Value error;
        error["error"]   = "Service Unavailable";
        error["message"] = "Emulator is shutting down";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k503ServiceUnavailable);
        addCorsHeaders(resp);
        callback(resp);
        return nullptr;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context)
    {
        Json::Value error;
        error["error"]   = "Internal Error";
        error["message"] = "Unable to access emulator context";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return nullptr;
    }

    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;
    if (requireManager && !mgr)
    {
        Json::Value error;
        error["error"]   = "Not Available";
        error["message"] = "TTD engine not available in this build";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k501NotImplemented);
        addCorsHeaders(resp);
        callback(resp);
        return nullptr;
    }

    return mgr;
}

/// @brief POST /api/v1/emulator/{id}/ttd/start
void EmulatorAPI::startTTD(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto* mgr = resolveTTD(id, callback);
    if (!mgr) return;

    bool alreadyRecording = mgr->IsRecording();
    bool ok = mgr->StartRecording();

    Json::Value ret;
    ret["started"]        = ok && !alreadyRecording;
    ret["already_active"] = alreadyRecording;
    ret["state"]          = ttd::TTDSessionStateToString(mgr->GetState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/ttd/stop
void EmulatorAPI::stopTTD(const HttpRequestPtr& req,
                           std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto* mgr = resolveTTD(id, callback);
    if (!mgr) return;

    bool wasRecording = mgr->IsRecording();
    mgr->StopRecording();

    Json::Value ret;
    ret["stopped"]      = wasRecording;
    ret["state"]        = ttd::TTDSessionStateToString(mgr->GetState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/ttd/invalidate
///
/// Optional JSON body: { "reason": "<string>" }
void EmulatorAPI::invalidateTTD(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto* mgr = resolveTTD(id, callback);
    if (!mgr) return;

    std::string reason = "WebAPI invalidate";

    // Try to parse optional JSON body
    auto jsonBody = req->getJsonObject();
    if (jsonBody && jsonBody->isMember("reason") && (*jsonBody)["reason"].isString())
    {
        reason = (*jsonBody)["reason"].asString();
    }

    mgr->InvalidateSession(reason.c_str());

    Json::Value ret;
    ret["invalidated"] = true;
    ret["reason"]      = reason;
    ret["state"]       = ttd::TTDSessionStateToString(mgr->GetState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/ttd/seek
///
/// JSON body: { "frame": <uint64>, "tinframe": <uint32, optional default 0> }
///
/// Response:
///   { "reached": true/false,
///     "arrived_at": { "frame": <uint64>, "tinframe": <uint32> },
///     "halt_reason": "target" | "external_event" | "out_of_range",
///     "blocking_marker": { ... }  // present only if halt_reason == external_event
///   }
void EmulatorAPI::seekTTD(const HttpRequestPtr& req,
                           std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto* mgr = resolveTTD(id, callback);
    if (!mgr) return;

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("frame"))
    {
        Json::Value error;
        error["error"]   = "Bad Request";
        error["message"] = "Missing required field: frame";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ttd::TTDTimePoint target{};
    target.frame    = (*jsonBody)["frame"].asUInt64();
    target.tInFrame = jsonBody->isMember("tinframe") ? static_cast<uint32_t>((*jsonBody)["tinframe"].asUInt()) : 0;

    ttd::TimeTravelManager::TTDSeekResult result;
    bool reached = mgr->SeekTo(target, &result);

    Json::Value ret;
    ret["reached"] = reached;

    Json::Value arrivedAt;
    arrivedAt["frame"]    = Json::UInt64(result.arrivedAt.frame);
    arrivedAt["tinframe"] = Json::UInt(result.arrivedAt.tInFrame);
    ret["arrived_at"]     = arrivedAt;

    const char* reasonStr = "target";
    switch (result.haltReason)
    {
        case ttd::TimeTravelManager::TTDSeekHaltReason::ExternalEvent: reasonStr = "external_event"; break;
        case ttd::TimeTravelManager::TTDSeekHaltReason::OutOfRange:    reasonStr = "out_of_range"; break;
        default: break;
    }
    ret["halt_reason"] = reasonStr;

    if (result.haltReason == ttd::TimeTravelManager::TTDSeekHaltReason::ExternalEvent)
    {
        Json::Value marker;
        marker["frame"]    = Json::UInt64(result.blockingMarker.time.frame);
        marker["tinframe"] = Json::UInt(result.blockingMarker.time.tInFrame);
        marker["kind"]     = ttd::TTDExternalEventKindToString(result.blockingMarker.kind);
        marker["reason"]   = result.blockingMarker.reason;
        ret["blocking_marker"] = marker;
    }

    ret["state"] = ttd::TTDSessionStateToString(mgr->GetState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/ttd/step-back
void EmulatorAPI::stepBackTTD(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto* mgr = resolveTTD(id, callback);
    if (!mgr) return;

    bool ok = mgr->StepBackFrame();
    ttd::TTDTimePoint pos = mgr->CurrentPosition();

    Json::Value ret;
    ret["stepped"]  = ok;
    ret["frame"]    = Json::UInt64(pos.frame);
    ret["tinframe"] = Json::UInt(pos.tInFrame);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/ttd/step-forward
void EmulatorAPI::stepForwardTTD(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    auto* mgr = resolveTTD(id, callback);
    if (!mgr) return;

    bool ok = mgr->StepForwardFrame();
    ttd::TTDTimePoint pos = mgr->CurrentPosition();

    Json::Value ret;
    ret["stepped"]  = ok;
    ret["frame"]    = Json::UInt64(pos.frame);
    ret["tinframe"] = Json::UInt(pos.tInFrame);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/ttd/resume
///
/// Optional JSON body: { "frame": <uint64>, "tinframe": <uint32> }
/// If omitted, resumes from the current position.
void EmulatorAPI::resumeTTD(const HttpRequestPtr& req,
                             std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    auto* mgr = resolveTTD(id, callback);
    if (!mgr) return;

    ttd::TTDTimePoint from = mgr->CurrentPosition();

    auto jsonBody = req->getJsonObject();
    if (jsonBody && jsonBody->isMember("frame"))
    {
        from.frame    = (*jsonBody)["frame"].asUInt64();
        from.tInFrame = jsonBody->isMember("tinframe") ? static_cast<uint32_t>((*jsonBody)["tinframe"].asUInt()) : 0;
    }

    bool ok = mgr->ResumeRecordingFrom(from);

    Json::Value ret;
    ret["resumed"]  = ok;
    ret["frame"]    = Json::UInt64(from.frame);
    ret["tinframe"] = Json::UInt(from.tInFrame);
    ret["state"]    = ttd::TTDSessionStateToString(mgr->GetState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/ttd/position
void EmulatorAPI::getTTDPosition(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    auto* mgr = resolveTTD(id, callback);
    if (!mgr) return;

    ttd::TTDTimePoint pos = mgr->CurrentPosition();
    ttd::TTDTimePoint end = mgr->SessionEndPosition();

    Json::Value ret;
    Json::Value current;
    current["frame"]    = Json::UInt64(pos.frame);
    current["tinframe"] = Json::UInt(pos.tInFrame);
    ret["current"]      = current;

    Json::Value sessionEnd;
    sessionEnd["frame"]    = Json::UInt64(end.frame);
    sessionEnd["tinframe"] = Json::UInt(end.tInFrame);
    ret["session_end"]     = sessionEnd;

    ret["state"] = ttd::TTDSessionStateToString(mgr->GetState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/ttd/markers
void EmulatorAPI::getTTDMarkers(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto* mgr = resolveTTD(id, callback);
    if (!mgr) return;

    const ttd::TTDExternalEventJournal& journal = mgr->GetExternalEvents();

    Json::Value ret;
    ret["count"] = Json::UInt64(journal.Size());

    Json::Value markers(Json::arrayValue);
    const auto& events = journal.Events();
    for (const auto& e : events)
    {
        Json::Value marker;
        marker["frame"]    = Json::UInt64(e.time.frame);
        marker["tinframe"] = Json::UInt(e.time.tInFrame);
        marker["kind"]     = ttd::TTDExternalEventKindToString(e.kind);
        marker["reason"]   = e.reason;
        markers.append(marker);
    }
    ret["markers"] = markers;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
