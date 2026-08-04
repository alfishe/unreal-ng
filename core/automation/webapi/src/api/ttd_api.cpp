/// @file ttd_api.cpp
/// @brief WebAPI TTD (Time-Travel Debug) endpoints.
///
/// Per parent TDD §10.4. Full TTD automation surface:
///
/// Phase 2 (core TTD):
///   GET  /ttd/status        — session info
///   POST /ttd/start         — begin recording
///   POST /ttd/stop          — stop recording (history retained)
///   POST /ttd/invalidate    — drop all history, return to Idle
///   POST /ttd/seek          — seek to (frame, tInFrame)
///   POST /ttd/step-back     — step back one frame
///   POST /ttd/step-forward  — step forward one frame
///   POST /ttd/resume        — resume recording from a past point
///   GET  /ttd/position      — current TTDTimePoint
///   GET  /ttd/markers       — list external-event markers (replay barriers)
///
/// Phase 4 (reverse search):
///   POST /ttd/dump          — serialize session to .ttd file
///   POST /ttd/find-last     — reverse search: find last access at address
///   POST /ttd/step-instruction — step one instruction back or forward
///
/// Phase 4 (reverse execution):
///   POST /ttd/reverse-step       — step back N instructions or T t-states
///   POST /ttd/reverse-continue   — run backward until any PC matches
///
/// The status endpoint surfaces every field of TTDSessionInfo so automation
/// clients and the divergence-test harness can poll the recorder without
/// linking against the TTD headers directly.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/notifications.h>  // EmulatorFramePayload
#include <emulator/platform.h>       // NC_VIDEO_FRAME_REFRESH
#include <json/json.h>

#include <sstream>

#include "3rdparty/message-center/messagecenter.h"
#include "../emulator_api.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_external_events.h"
#include "debugger/ttd/ttd_probe.h"

#include <fstream>
#include "debugger/ttd/ttd_probe.h"

#include <fstream>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper functions declared in emulator_api.h / emulator_api.cpp.
extern void addCorsHeaders(HttpResponsePtr& resp);
// getEmulatorByIdOrIndex is a free function in api::v1 (emulator_api.h) —
// visible here without an EmulatorAPI instance.

/// @brief Synchronous pause discipline for TTD state mutations.
///
/// Emulator::Pause() is asynchronous: it sets the _isPaused flag and returns
/// immediately, and the Z80 thread only notices at the top of the next frame
/// iteration. If a TTD seek/step ran immediately after Pause(), the in-flight
/// frame could overwrite the freshly restored framebuffer / emulator state
/// — the user would see a stale screen and border that didn't match the
/// target snapshot.
///
/// This helper closes that race by waiting for the Z80 thread to actually
/// park before the caller mutates state. After the mutation, callers MUST
/// also invoke NotifyFrameRefresh() so any attached UI surface (unreal-qt
/// widget, unreal-screen-viewer, debug visualization window) repaints with
/// the freshly rebuilt framebuffer — when the emulator is paused, MainLoop
/// doesn't run and therefore doesn't post NC_VIDEO_FRAME_REFRESH itself.
///
/// Returns true if pause was confirmed, false on timeout. Callers proceed
/// regardless — the mutation is still correct, just slightly racy on timeout.
static bool PauseAndConfirm(const std::shared_ptr<Emulator>& emulator,
                            uint32_t timeout_ms = 1000)
{
    if (!emulator)
        return false;
    emulator->Pause();
    return emulator->WaitForPauseConfirmation(timeout_ms);
}

/// @brief Notify UI surfaces that the framebuffer has changed.
///
/// Posts NC_VIDEO_FRAME_REFRESH exactly as MainLoop::OnFrameEnd() does, so
/// every observer (unreal-qt MainWindow, EmulatorBinding, debug visualization
/// window) repaints with the current framebuffer. Required after TTD
/// seek/step-back/step-forward because those paths rebuild the framebuffer
/// in-place via RestoreCheckpoint -> Screen::RenderOnlyMainScreen() but do
/// NOT run a MainLoop iteration, so the observers never see a frame event
/// and keep displaying the pre-seek frame.
static void NotifyFrameRefresh(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    if (!context)
        return;

    // IMPORTANT: use GetId() (UUID-as-string), NOT GetSymbolicId().
    // MainWindow::handleMessageScreenRefresh filters incoming
    // EmulatorFramePayload by comparing _emulatorId against
    // _emulator->GetUUID(). GetSymbolicId() is a human-readable label
    // (often empty for WebAPI-created instances), which would parse to
    // a nil UUID in EmulatorFramePayload's constructor and never match —
    // the refresh would be silently dropped and the emulator screen would
    // never repaint after seek/step. mainloop.cpp:402 uses GetId() too.
    const std::string emulatorId = emulator.GetId();
    const uint32_t frameCounter = context->emulatorState.frame_counter;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_VIDEO_FRAME_REFRESH,
                       new EmulatorFramePayload(emulatorId, frameCounter));
}

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
///   "baseline_frames_captured": <uint64>,
///   "session_heap_bytes":   <uint64>   // real heap footprint of session
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
    ret["session_heap_bytes"]       = Json::UInt64(0);
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
        ret["session_heap_bytes"]       = Json::UInt64(info.sessionHeapBytes);
        ret["write_journal_enabled"]    = info.writeJournalEnabled;
        ret["ttd_available"]            = true;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// ---------------------------------------------------------------------------
// Internal helper: reject scrub-style operations during active recording.
// Returns true if the request was rejected (callback already invoked).
//
// Scrubbing (seek / step-back / step-forward) during Recording trashes
// emulator state — RestoreCheckpoint overwrites the live emulator with old
// captured data, and the next OnFrameBoundary would append a checkpoint at
// the restored (older) frame, breaking the timeline's sorted invariant.
// Callers MUST StopRecording first.
// ---------------------------------------------------------------------------
static bool rejectIfRecording(ttd::TimeTravelManager* mgr,
                               std::function<void(const HttpResponsePtr&)>& callback)
{
    if (!mgr || !mgr->IsRecording())
        return false;  // Not recording — caller may proceed.

    Json::Value error;
    error["error"]   = "Conflict";
    error["message"] = "Cannot scrub while recording is active — call "
                       "POST /ttd/stop first. Scrubbing during recording "
                       "would overwrite live emulator state with restored "
                       "checkpoint data and corrupt the timeline.";
    error["state"]   = ttd::TTDSessionStateToString(mgr->GetState());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(HttpStatusCode::k409Conflict);
    addCorsHeaders(resp);
    callback(resp);
    return true;
}

// ---------------------------------------------------------------------------
// Internal helper: resolve emulator + context + TTD manager, or send error.
// Returns nullptr on failure (error response already sent).
// On success, `outEmulator` (if non-null) receives the emulator pointer so
// callers can Pause/Resume it around state-mutating TTD operations.
// ---------------------------------------------------------------------------
static ttd::TimeTravelManager* resolveTTD(
    const std::string& id,
    std::function<void(const HttpResponsePtr&)>& callback,
    bool requireManager = true,
    std::shared_ptr<Emulator>* outEmulator = nullptr)
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

    if (outEmulator)
        *outEmulator = emulator;

    return mgr;
}

/// @brief POST /api/v1/emulator/{id}/ttd/start
///
/// Optional JSON body:
/// {
///   "mode": "gaming" | "development"   // gaming = no journal, development = full journal (default)
///   "enable_write_journal": bool       // explicit override (takes precedence over mode)
/// }
void EmulatorAPI::startTTD(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto* mgr = resolveTTD(id, callback);
    if (!mgr) return;

    // Parse optional config from JSON body
    bool enableWriteJournal = true;  // default: development mode
    auto json = req->getJsonObject();
    if (json)
    {
        if (json->isMember("enable_write_journal"))
        {
            enableWriteJournal = (*json)["enable_write_journal"].asBool();
        }
        else if (json->isMember("mode"))
        {
            const std::string mode = (*json)["mode"].asString();
            if (mode == "gaming")
                enableWriteJournal = false;
            // "development" or any other value keeps the default (true)
        }
    }

    bool alreadyRecording = mgr->IsRecording();
    if (!alreadyRecording)
    {
        mgr->SetEnableWriteJournal(enableWriteJournal);
    }
    bool ok = mgr->StartRecording();

    Json::Value ret;
    ret["started"]              = ok && !alreadyRecording;
    ret["already_active"]       = alreadyRecording;
    ret["state"]                = ttd::TTDSessionStateToString(mgr->GetState());
    ret["write_journal_enabled"] = mgr->GetEnableWriteJournal();

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
    std::shared_ptr<Emulator> emulator;
    auto* mgr = resolveTTD(id, callback, /*requireManager=*/true, &emulator);
    if (!mgr) return;

    // Defense-in-depth: engine-level SeekTo also rejects, but we want to
    // return a clear 409 with an actionable message rather than a 200
    // with reached=false.
    if (rejectIfRecording(mgr, callback)) return;

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

    // Pause the emulator while we mutate TTD state so the emulator thread
    // can't advance frame_counter past the restored checkpoint before the
    // caller sees the result. PauseAndConfirm() blocks until the Z80 thread
    // has actually parked, closing the race in which the in-flight frame
    // loop would overwrite the freshly restored framebuffer. The emulator
    // stays paused after a successful seek — the TTD state machine is now
    // Detached and the next /ttd/resume call will Resume() it.
    PauseAndConfirm(emulator);

    ttd::TimeTravelManager::TTDSeekResult result;
    bool reached = mgr->SeekTo(target, &result);

    // SeekTo rebuilt the framebuffer in-place via RestoreCheckpoint ->
    // Screen::RenderOnlyMainScreen(). The emulator is paused, so MainLoop
    // won't post NC_VIDEO_FRAME_REFRESH on its own — do it explicitly so
    // every observer (unreal-qt widget, screen viewer, debug visualization)
    // repaints with the target snapshot's screen + border.
    if (emulator)
        NotifyFrameRefresh(*emulator);

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
    std::shared_ptr<Emulator> emulator;
    auto* mgr = resolveTTD(id, callback, /*requireManager=*/true, &emulator);
    if (!mgr) return;

    if (rejectIfRecording(mgr, callback)) return;

    // Same pause-around-state-mutation discipline as seekTTD: wait for the
    // Z80 thread to park so it can't overwrite the restored framebuffer,
    // then notify observers to repaint.
    PauseAndConfirm(emulator);

    bool ok = mgr->StepBackFrame();
    ttd::TTDTimePoint pos = mgr->CurrentPosition();

    if (emulator)
        NotifyFrameRefresh(*emulator);

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
    std::shared_ptr<Emulator> emulator;
    auto* mgr = resolveTTD(id, callback, /*requireManager=*/true, &emulator);
    if (!mgr) return;

    if (rejectIfRecording(mgr, callback)) return;

    // Same pause-around-state-mutation discipline as seekTTD: wait for the
    // Z80 thread to park so it can't overwrite the restored framebuffer,
    // then notify observers to repaint.
    PauseAndConfirm(emulator);

    bool ok = mgr->StepForwardFrame();
    ttd::TTDTimePoint pos = mgr->CurrentPosition();

    if (emulator)
        NotifyFrameRefresh(*emulator);

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
    std::shared_ptr<Emulator> emulator;
    auto* mgr = resolveTTD(id, callback, /*requireManager=*/true, &emulator);
    if (!mgr) return;

    ttd::TTDTimePoint from = mgr->CurrentPosition();

    auto jsonBody = req->getJsonObject();
    if (jsonBody && jsonBody->isMember("frame"))
    {
        from.frame    = (*jsonBody)["frame"].asUInt64();
        from.tInFrame = jsonBody->isMember("tinframe") ? static_cast<uint32_t>((*jsonBody)["tinframe"].asUInt()) : 0;
    }

    bool ok = mgr->ResumeRecordingFrom(from);

    // Mirror of seekTTD: now that the TTD state machine is Recording again,
    // resume emulator execution so the capture can continue.
    if (ok && emulator)
        emulator->Resume();

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

// -------------------------------------------------------------------------
// Phase 4 — Reverse search + dump + instruction step
// -------------------------------------------------------------------------

/// @brief POST /api/v1/emulator/{id}/ttd/dump
void EmulatorAPI::dumpTTD(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto* mgr = resolveTTD(id, callback);
    if (!mgr) return;

    auto json = req->getJsonObject();
    if (!json || !json->isMember("path"))
    {
        Json::Value err;
        err["error"] = "Missing 'path' in request body";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const std::string path = (*json)["path"].asString();
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
    {
        Json::Value err;
        err["error"] = "Cannot open file: " + path;
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string errMsg;
    bool ok = mgr->SerializeSession(out, errMsg);
    auto bytes = out.tellp();

    Json::Value ret;
    ret["ok"] = ok;
    if (ok)
    {
        ret["path"]  = path;
        ret["bytes"] = Json::Int64(static_cast<long long>(bytes));
    }
    else
    {
        ret["error"] = errMsg;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/ttd/find-last
void EmulatorAPI::findLastTTD(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    std::shared_ptr<Emulator> emulator;
    auto* mgr = resolveTTD(id, callback, /*requireManager=*/true, &emulator);
    if (!mgr) return;

    if (rejectIfRecording(mgr, callback)) return;

    auto json = req->getJsonObject();
    if (!json || !json->isMember("addr"))
    {
        Json::Value err;
        err["error"] = "Missing 'addr' in request body";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = static_cast<uint16_t>((*json)["addr"].asUInt());

    if (json->isMember("access"))
        q.access = ttd::TTDAccessTypeFromString((*json)["access"].asCString());

    if (json->isMember("value"))
    {
        q.value = static_cast<uint8_t>((*json)["value"].asUInt());
        q.hasValueFilter = true;
    }

    if (json->isMember("pc_from"))
    {
        q.pcFrom = static_cast<uint16_t>((*json)["pc_from"].asUInt());
        q.hasPcFilter = true;
    }

    if (json->isMember("pc_to"))
    {
        q.pcTo = static_cast<uint16_t>((*json)["pc_to"].asUInt());
        if (!q.hasPcFilter) q.hasPcFilter = true;
    }

    if (emulator)
    {
        const uint32_t frameT = emulator->GetContext()->config.frame;
        if (json->isMember("before_frame"))
        {
            uint64_t f = (*json)["before_frame"].asUInt64();
            uint32_t tin = json->isMember("before_tin") ? (*json)["before_tin"].asUInt() : 0;
            q.beforeGlobalT = f * frameT + tin;
        }
    }

    PauseAndConfirm(emulator);

    ttd::TTDExternalEvent marker;
    auto result = mgr->FindLastAccess(q, &marker);

    if (emulator)
        NotifyFrameRefresh(*emulator);

    Json::Value ret;
    if (result)
    {
        ret["found"]      = true;
        ret["frame"]      = Json::UInt64(result->time.frame);
        ret["tinframe"]   = Json::UInt(result->time.tInFrame);
        ret["pc"]         = Json::UInt(result->pc);
        ret["value"]      = Json::UInt(result->value);
        ret["phys_page"]  = Json::UInt(result->physPage);
        ret["access"]     = ttd::TTDAccessTypeToString(result->access);
    }
    else if (marker.reason[0] != '\0')
    {
        ret["found"]  = false;
        ret["blocked"] = true;
        ret["marker_frame"]    = Json::UInt64(marker.time.frame);
        ret["marker_tinframe"] = Json::UInt(marker.time.tInFrame);
        ret["marker_kind"]     = ttd::TTDExternalEventKindToString(marker.kind);
        ret["marker_reason"]   = marker.reason;
    }
    else
    {
        ret["found"] = false;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/ttd/step-instruction
void EmulatorAPI::stepInstructionTTD(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback,
                                       const std::string& id) const
{
    std::shared_ptr<Emulator> emulator;
    auto* mgr = resolveTTD(id, callback, /*requireManager=*/true, &emulator);
    if (!mgr) return;

    if (rejectIfRecording(mgr, callback)) return;

    auto json = req->getJsonObject();
    std::string dir = "back";
    if (json && json->isMember("dir"))
        dir = (*json)["dir"].asString();

    const bool forward = (dir == "forward" || dir == "fwd");

    PauseAndConfirm(emulator);

    bool ok = forward ? mgr->StepForwardInstruction() : mgr->StepBackInstruction();
    ttd::TTDTimePoint pos = mgr->CurrentPosition();

    if (emulator)
        NotifyFrameRefresh(*emulator);

    Json::Value ret;
    ret["stepped"]  = ok;
    ret["dir"]      = forward ? "forward" : "back";
    ret["frame"]    = Json::UInt64(pos.frame);
    ret["tinframe"] = Json::UInt(pos.tInFrame);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/ttd/reverse-step
///
/// Body: { "count"?: int, "tstates"?: int }  — exactly one of the two.
///   count    : step back N instructions (M1 boundaries)
///   tstates  : step back N t-states (lands at nearest M1 <= target)
///
/// Response: { "reached": bool, "frame": int, "tinframe": int }
void EmulatorAPI::reverseStepTTD(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    std::shared_ptr<Emulator> emulator;
    auto* mgr = resolveTTD(id, callback, /*requireManager=*/true, &emulator);
    if (!mgr) return;

    if (rejectIfRecording(mgr, callback)) return;

    auto json = req->getJsonObject();
    const bool hasCount   = json && json->isMember("count");
    const bool hasTstates = json && json->isMember("tstates");

    if (hasCount && hasTstates)
    {
        Json::Value err;
        err["error"] = "Specify exactly one of 'count' or 'tstates' (not both)";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    if (!hasCount && !hasTstates)
    {
        Json::Value err;
        err["error"] = "Missing required field: 'count' or 'tstates'";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    PauseAndConfirm(emulator);

    bool ok = false;
    if (hasTstates)
    {
        const uint64_t t = (*json)["tstates"].asUInt64();
        ok = mgr->ReverseStepTStates(t);
    }
    else
    {
        const uint32_t n = static_cast<uint32_t>((*json)["count"].asUInt64());
        ok = mgr->ReverseStepInstructions(n);
    }

    ttd::TTDTimePoint pos = mgr->CurrentPosition();
    if (emulator)
        NotifyFrameRefresh(*emulator);

    Json::Value ret;
    ret["reached"] = ok;
    ret["mode"]    = hasTstates ? "tstates" : "count";
    ret["frame"]   = Json::UInt64(pos.frame);
    ret["tinframe"] = Json::UInt(pos.tInFrame);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/ttd/reverse-continue
///
/// Body: { "pcs": [int, ...] }  — non-empty list of reverse breakpoints.
///
/// Response: { "matched": bool, "pc": int, "frame": int, "tinframe": int,
///             "blocked_by_marker"?: { ... } }
void EmulatorAPI::reverseContinueTTD(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback,
                                       const std::string& id) const
{
    std::shared_ptr<Emulator> emulator;
    auto* mgr = resolveTTD(id, callback, /*requireManager=*/true, &emulator);
    if (!mgr) return;

    if (rejectIfRecording(mgr, callback)) return;

    auto json = req->getJsonObject();
    if (!json || !json->isMember("pcs") || !(*json)["pcs"].isArray())
    {
        Json::Value err;
        err["error"] = "Missing or invalid 'pcs' (expected a JSON array of integers)";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const auto& pcsArr = (*json)["pcs"];
    if (pcsArr.empty())
    {
        Json::Value err;
        err["error"] = "'pcs' array must be non-empty";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::vector<uint16_t> bps;
    bps.reserve(pcsArr.size());
    for (Json::ArrayIndex i = 0; i < pcsArr.size(); ++i)
        bps.push_back(static_cast<uint16_t>(pcsArr[i].asUInt()));

    PauseAndConfirm(emulator);

    auto result = mgr->ReverseContinue(bps);
    if (emulator)
        NotifyFrameRefresh(*emulator);

    Json::Value ret;
    ret["matched"]  = result.matched;
    ret["pc"]       = result.pc;
    ret["frame"]    = Json::UInt64(result.arrivedAt.frame);
    ret["tinframe"] = Json::UInt(result.arrivedAt.tInFrame);

    if (result.blockingMarker.reason[0] != '\0')
    {
        Json::Value m;
        m["kind"]   = ttd::TTDExternalEventKindToString(result.blockingMarker.kind);
        m["reason"] = result.blockingMarker.reason;
        m["frame"]  = Json::UInt64(result.blockingMarker.time.frame);
        m["tinframe"] = Json::UInt(result.blockingMarker.time.tInFrame);
        ret["blocked_by_marker"] = m;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
