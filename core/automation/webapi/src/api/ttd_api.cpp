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
#include "../common/jsonnumber.h"
#include "../emulator_api.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcontrol.h"
#include "../common/statenode_json.h"
#include "debugger/ttd/ttdexternalevents.h"
#include "debugger/ttd/ttdprobe.h"

#include <fstream>
#include "debugger/ttd/ttdprobe.h"

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

// ---------------------------------------------------------------------------
// The verbs TTDControl implements for every surface (Phase 5, Step 1): a route
// turns its JSON body into string options and sends the reply back as JSON
// ---------------------------------------------------------------------------
static void RespondTTD(const std::string& id, const std::string& verb, std::map<std::string, std::string> options,
                       std::function<void(const HttpResponsePtr&)>& callback)
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
    const ttd::TTDReply reply = ttd::TTDControl(emulator->GetContext()).Execute({verb, std::move(options)});
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(reply.ToValue()));
    resp->setStatusCode(static_cast<HttpStatusCode>(reply.HttpStatus()));
    addCorsHeaders(resp);
    callback(resp);
}

/// A JSON body's scalar members as TTDControl options (null: absent). False when a
/// member is an object or an array: the verbs take scalars only
static bool OptionsFromJson(const HttpRequestPtr& req, std::map<std::string, std::string>& options,
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
            Json::Value error;
            error["error"]   = "Bad Request";
            error["message"] = "'" + name + "' must be a number, a string or true / false";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return false;
        }
        options[name] = v.isBool() ? (v.asBool() ? "true" : "false") : v.asString();
    }
    return true;
}


// ---------------------------------------------------------------------------
// The recorded machine and a .ttd file's info as JSON (ttdfileinfo.h). The same
// keys on every surface: Lua / Python tables, CLI labels, MCP summaries.
// ---------------------------------------------------------------------------
namespace
{

Json::Value RecordedMachineJson(const ttd::TTDRecordedMachine& m)
{
    Json::Value v;
    v["model_id"] = Json::UInt(m.modelId);
    v["model"] = m.model.empty() ? Json::Value(Json::nullValue) : Json::Value(m.model);
    v["ram_page_bound"] = Json::UInt(m.ramPageBound);
    // A string: a 64-bit hash does not survive a JSON number (doubles)
    v["rom_signature"] = m.romSignature == 0 ? Json::Value(Json::nullValue)
                                             : Json::Value("0x" + ttd::HashToString(m.romSignature));
    v["peripheral_mask"] = Json::UInt64(m.peripheralMask);
    Json::Value list(Json::arrayValue);
    for (const std::string& name : m.peripherals)
        list.append(name);
    v["peripherals"] = list;
    Json::Value notRecorded(Json::arrayValue);
    for (const std::string& name : m.notRecorded)
        notRecorded.append(name);
    v["not_recorded"] = notRecorded;
    v["general_sound"] = ttd::GeneralSoundName(m.generalSound);
    v["turbo_sound"] = m.turboSound;
    return v;
}

Json::Value FileInfoJson(const ttd::TTDFileInfo& info)
{
    Json::Value v;
    v["ok"] = true;
    v["path"] = info.path;
    v["file_bytes"] = Json::UInt64(info.fileBytes);
    v["schema_version"] = Json::UInt(info.schemaVersion);
    v["flags"] = Json::UInt(info.flags);
    v["captured_at_unix_ms"] = Json::UInt64(info.capturedAtUnixMs);
    v["recorded_by"] = info.emulatorId.empty() ? Json::Value(Json::nullValue) : Json::Value(info.emulatorId);
    v["session_state"] = ttd::TTDSessionStateToString(static_cast<ttd::TTDSessionState>(info.sessionState));
    v["session_start_frame"] = Json::UInt64(info.startFrame);
    v["session_end_frame"] = Json::UInt64(info.endFrame);
    v["checkpoint_count"] = Json::UInt(info.checkpointCount);
    v["page_slot_count"] = Json::UInt(info.pageStoreCount);
    Json::Value sections;
    sections["write_journal"] = info.hasWriteJournal;
    sections["write_journal_complete"] = info.writeJournalComplete;
    sections["coverage_index"] = info.hasCoverageIndex;
    sections["bookmarks"] = info.hasBookmarks;
    sections["input_journal"] = info.hasInputJournal;
    sections["external_events"] = info.hasExternalEvents;
    sections["port_journals"] = info.hasPortJournals;
    sections["top_clock_time"] = info.topClockTime;
    v["sections"] = sections;
    v["machine"] = RecordedMachineJson(info.machine);
    v["peripherals_from_header"] = info.peripheralsFromHeader;
    return v;
}

}  // namespace

/// @brief GET /api/v1/ttd/file-info?path=<file.ttd>
/// A .ttd file's header, sections and recorded machine, read without loading
/// it (no emulator instance involved): provision a matching machine first.
void EmulatorAPI::getTTDFileInfo(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    const std::string path = req->getParameter("path");
    Json::Value ret;
    HttpStatusCode code = HttpStatusCode::k200OK;
    if (path.empty())
    {
        ret["ok"] = false;
        ret["error"] = "query parameter 'path' is required";
        code = HttpStatusCode::k400BadRequest;
    }
    else
    {
        ttd::TTDFileInfo info;
        std::string err;
        if (ttd::ReadTTDFileInfo(path, info, err))
            ret = FileInfoJson(info);
        else
        {
            ret["ok"] = false;
            ret["path"] = path;
            ret["error"] = err;
            code = err.rfind("cannot open", 0) == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest;
        }
    }
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
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
/// Thread-safety: ReadSessionInfo() never walks a session another thread is
/// changing: live only while nothing runs the machine and no other control
/// operation is in progress, else the summary the session-driving thread
/// published (TDD section 7.2; at most ~100 ms old while recording).
void EmulatorAPI::getTTDStatus(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{    (void)req;
    RespondTTD(id, "status", {}, callback);
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
///   "journal": bool    // also record the write journal (default false, D40)
/// }
void EmulatorAPI::startTTD(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    // The write journal is off unless asked for (D40)
    options.emplace("journal", "false");
    RespondTTD(id, "start", std::move(options), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/history-limit
///
/// JSON body: { "frames": <n>, "bytes": <n> } - each optional (missing keeps the
/// current value, 0 = no limit); an empty body only reports. While recording the
/// limit applies at once; otherwise from the next recorded frame
void EmulatorAPI::historyLimitTTD(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "history-limit", std::move(options), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/stop
void EmulatorAPI::stopTTD(const HttpRequestPtr& req,
                           std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{    (void)req;
    RespondTTD(id, "stop", {}, callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/invalidate
///
/// Optional JSON body: { "reason": "<string>" }
void EmulatorAPI::invalidateTTD(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    if (options.find("reason") == options.end())
        options["reason"] = "WebAPI invalidate";
    RespondTTD(id, "invalidate", std::move(options), callback);
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
/// @brief POST /api/v1/emulator/{id}/ttd/export-clip
/// Body: {"from": frame, "to": frame, "path": "/abs/dir", "chunk": 500 (optional)}.
/// Writes the range as a lossless clip (final picture, plane B when the zxdlss
/// feature is on, frame meta) inside the core - one call instead of a seek and a
/// capture per frame. Synchronous; the emulator is paused for the duration.
void EmulatorAPI::exportClipTTD(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    std::shared_ptr<Emulator> emulator;
    auto* mgr = resolveTTD(id, callback, /*requireManager=*/true, &emulator);
    if (!mgr) return;
    if (rejectIfRecording(mgr, callback)) return;

    auto jsonBody = req->getJsonObject();
    if (!jsonBody || !jsonBody->isMember("from") || !jsonBody->isMember("to") || !jsonBody->isMember("path"))
    {
        Json::Value error;
        error["error"]   = "Bad Request";
        error["message"] = "Required fields: from, to, path (absolute directory)";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ttd::TimeTravelManager::TTDClipExportOptions options;
    options.fromFrame = (*jsonBody)["from"].asUInt64();
    options.toFrame = (*jsonBody)["to"].asUInt64();
    options.directory = (*jsonBody)["path"].asString();
    if (jsonBody->isMember("chunk"))
        options.chunkFrames = (*jsonBody)["chunk"].asUInt();

    PauseAndConfirm(emulator);
    const auto result = mgr->ExportClip(options);
    if (emulator)
        NotifyFrameRefresh(*emulator);

    Json::Value ret;
    ret["ok"] = result.ok;
    ret["frames"] = Json::UInt64(result.frames);
    ret["bytes"] = Json::UInt64(result.bytesWritten);
    ret["planeb"] = result.planeB;
    ret["width"] = result.width;
    ret["height"] = result.height;
    ret["seconds"] = result.seconds;
    ret["path"] = options.directory;
    if (!result.ok)
        ret["error"] = result.error;
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    if (!result.ok)
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

void EmulatorAPI::seekTTD(const HttpRequestPtr& req,
                           std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "seek", std::move(options), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/step-back
void EmulatorAPI::stepBackTTD(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    (void)req;
    RespondTTD(id, "step-back", {}, callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/step-forward
void EmulatorAPI::stepForwardTTD(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    (void)req;
    RespondTTD(id, "step-forward", {}, callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/resume
///
/// Optional JSON body: { "frame": <uint64>, "tinframe": <uint32> }
/// If omitted, resumes from the current position.
void EmulatorAPI::resumeTTD(const HttpRequestPtr& req,
                             std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "resume", std::move(options), callback);
}

/// @brief GET /api/v1/emulator/{id}/ttd/position
void EmulatorAPI::getTTDPosition(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    (void)req;
    RespondTTD(id, "position", {}, callback);
}

/// @brief GET /api/v1/emulator/{id}/ttd/markers
void EmulatorAPI::getTTDMarkers(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    (void)req;
    RespondTTD(id, "markers", {}, callback);
}

// -------------------------------------------------------------------------
// TD-4 — agent bookmarks (advisory annotations, never replay barriers).
// Stored beside the external-event journal and serialized in the .ttd
// session; a bookmark never appears as a halt_reason.
// -------------------------------------------------------------------------

/// @brief GET /api/v1/emulator/{id}/ttd/bookmarks
///
/// Response: { "count": N, "bookmarks": [ { "frame", "tinframe", "label" } ] }
/// Time-sorted. Unlike /ttd/markers these entries never halt a seek.
void EmulatorAPI::getTTDBookmarks(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    (void)req;
    RespondTTD(id, "bookmarks", {}, callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/bookmarks
///
/// Body: { "label": "umt entry", "frame": F (optional), "tinframe": T (optional) }
/// When frame is omitted the bookmark is placed at the current position.
/// Labels are keys: non-empty, at most 63 characters, unique per session.
///
/// Status codes:
///   - 201 Created on success
///   - 400 for label contract violations (missing / empty / overlong)
///   - 409 for a duplicate label or a position outside the recorded timeline
void EmulatorAPI::postTTDBookmark(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "bookmark-add", std::move(options), callback);
}

/// @brief DELETE /api/v1/emulator/{id}/ttd/bookmarks/{label}
///
/// Status codes: 200 on success, 404 when the label is unknown.
void EmulatorAPI::deleteTTDBookmark(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id, const std::string& label) const
{
    (void)req;
    RespondTTD(id, "bookmark-delete", {{"label", label}}, callback);
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

/// @brief POST /api/v1/emulator/{id}/ttd/load
///
/// Loads a .ttd session for playback. The file is opened by the EMULATOR
/// process, so the path is resolved on this machine and against this process's
/// working directory.
///
/// A session only restores into an instance of the model it was recorded on -
/// a checkpoint is raw RAM pages plus a chipset snapshot, and pushing a
/// Pentagon recording into a 48K machine would corrupt it silently. The core
/// refuses that and the reason is returned verbatim, naming both model ids, so
/// the caller can provision a matching instance (POST /api/v1/emulator/create)
/// and retry.
///
/// After a successful load the session is Idle: use /ttd/seek to position the
/// emulator inside the loaded timeline.
void EmulatorAPI::loadTTD(const HttpRequestPtr& req,
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
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        Json::Value err;
        err["error"] = "Cannot open file: " + path;
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string errMsg;
    Json::Value ret;
    mgr->SetSessionSourcePath(path);
    if (!mgr->DeserializeSession(in, errMsg))
    {
        ret["ok"] = false;
        ret["error"] = errMsg;
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const ttd::TTDSessionInfo info = mgr->ReadSessionInfo();
    ret["ok"] = true;
    ret["path"] = path;
    ret["checkpoint_count"] = Json::UInt64(info.checkpointCount);
    ret["session_start_frame"] = Json::UInt64(info.sessionStartFrame);
    ret["current_end_frame"] = Json::UInt64(info.currentEndFrame);
    ret["state"] = ttd::TTDSessionStateToString(info.state);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// TD-8: the part of history a backward search examined. It walked back from
/// covered_to and stopped at covered_from - the match, a replay barrier, or
/// the session start. Absent when the search was refused.

/// @brief POST /api/v1/emulator/{id}/ttd/find-last
/// @brief POST /api/v1/emulator/{id}/ttd/port-events
///
/// "When did the program ..." over the session's port journals
/// (ttdportsearch.h): no replay, works on a loaded file.
///
/// Body: { "event": "key" | "ear" | "ay-read" | "ay-write" | "ay-select" |
///         "border" | "beeper" | "in" | "out", "arg": "a" (a key name or an AY
///         register), and any option of ttd::ApplyPortQueryOption: "limit",
///         "newest", "from", "to" ("F" or "F:T"), "port", "port_mask",
///         "value", "value_mask", "match", "trigger", "ay_register";
///         "file": a .ttd path on the emulator's machine - searched without
///         loading it, the current session is not touched }
/// Response: { "event", "direction": "in"|"out", "count", "truncated",
///             "scanned", "hits": [ { "index", "frame", "tinframe", "port",
///             "value", "pc", "ay_register"? } ] }
void EmulatorAPI::portEventsTTD(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "port-events", std::move(options), callback);
}

void EmulatorAPI::findLastTTD(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "find-last", std::move(options), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/step-instruction
void EmulatorAPI::stepInstructionTTD(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback,
                                       const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "step-instruction", std::move(options), callback);
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
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "reverse-step", std::move(options), callback);
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
    // pcs: a JSON array of addresses; the verb takes them as one comma list
    auto json = req->getJsonObject();
    if (!json || !json->isMember("pcs") || !(*json)["pcs"].isArray())
    {
        Json::Value error;
        error["error"]   = "Bad Request";
        error["message"] = "Missing or invalid 'pcs' (expected a JSON array of addresses)";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    std::string pcs;
    for (const Json::Value& pc : (*json)["pcs"])
        pcs += (pcs.empty() ? "" : ",") + (pc.isString() ? pc.asString() : pc.isIntegral() ? pc.asString() : std::string("?"));
    RespondTTD(id, "reverse-continue", {{"pcs", pcs}}, callback);
}

static bool ParseUint16Param(const std::string& str, uint16_t& outVal)
{
    if (str.empty()) return false;
    try
    {
        size_t idx = 0;
        int base = (str.rfind("0x", 0) == 0 || str.rfind("0X", 0) == 0) ? 16 : 10;
        unsigned long val = std::stoul(str, &idx, base);
        if (val > 0xFFFF) return false;
        outVal = static_cast<uint16_t>(val);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

static bool ParseUint64Param(const std::string& str, uint64_t& outVal)
{
    if (str.empty()) return false;
    try
    {
        size_t idx = 0;
        int base = (str.rfind("0x", 0) == 0 || str.rfind("0X", 0) == 0) ? 16 : 10;
        outVal = std::stoull(str, &idx, base);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

/// Builds a 400 response for coverage query parameter validation. Callers add
/// CORS headers and hand it to the callback.
static HttpResponsePtr CoverageBadRequest(const std::string& message)
{
    Json::Value error;
    error["error"] = "Bad Request";
    error["message"] = message;
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(HttpStatusCode::k400BadRequest);
    return resp;
}

void EmulatorAPI::getTTDCoverageProbe(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback,
                                      const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    ttd::TimeTravelManager* mgr = context ? context->pTimeTravelManager : nullptr;

    const std::string frameStr = req->getParameter("frame");
    const std::string kindStr = req->getParameter("kind");
    const std::string addrFromStr = req->getParameter("addr_from");
    const std::string addrToStr = req->getParameter("addr_to");
    const std::string pageStr = req->getParameter("phys_page");

    uint64_t frame = 0;
    if (frameStr.empty())
    {
        auto resp = CoverageBadRequest("Missing required parameter: frame");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    if (!ParseUint64Param(frameStr, frame))
    {
        auto resp = CoverageBadRequest("Invalid frame: '" + frameStr + "' (expected unsigned integer)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ttd::TTDCoverageKind kind = ttd::TTDCoverageKind::Executed;
    if (!kindStr.empty() && !ttd::TTDCoverageKindFromString(kindStr, kind))
    {
        auto resp = CoverageBadRequest("Invalid kind: '" + kindStr + "' (expected executed, written or read)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint16_t addrFrom = 0;
    if (!addrFromStr.empty() && !ParseUint16Param(addrFromStr, addrFrom))
    {
        auto resp = CoverageBadRequest("Invalid addr_from: '" + addrFromStr + "' (expected 16-bit address)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint16_t addrTo = 0xFFFF;
    if (!addrToStr.empty() && !ParseUint16Param(addrToStr, addrTo))
    {
        auto resp = CoverageBadRequest("Invalid addr_to: '" + addrToStr + "' (expected 16-bit address)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (addrFrom > addrTo)
    {
        char rangeBuf[64];
        snprintf(rangeBuf, sizeof(rangeBuf), "addr_from (0x%04X) must not exceed addr_to (0x%04X)", addrFrom, addrTo);
        auto resp = CoverageBadRequest(rangeBuf);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::optional<ttd::PhysPage> physPage;
    uint16_t pageVal = 0;
    if (!pageStr.empty())
    {
        if (!ParseUint16Param(pageStr, pageVal) || pageVal > ttd::kPhysPageMax)
        {
            auto resp = CoverageBadRequest("Invalid phys_page: '" + pageStr + "' (expected 0..255)");
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        physPage = static_cast<ttd::PhysPage>(pageVal);
    }

    Json::Value ret;
    ret["frame"] = Json::UInt64(frame);
    ret["kind"] = ttd::TTDCoverageKindToString(kind);

    char hexBuf[16];
    snprintf(hexBuf, sizeof(hexBuf), "0x%04X", addrFrom);
    ret["addr_from"] = hexBuf;
    snprintf(hexBuf, sizeof(hexBuf), "0x%04X", addrTo);
    ret["addr_to"] = hexBuf;

    if (physPage) ret["phys_page"] = *physPage;

    if (mgr)
    {
        auto res = mgr->QueryCoverageProbe(frame, kind, addrFrom, addrTo, physPage);
        ret["touched"] = res.touched;
        ret["index_available"] = res.indexAvailable;
    }
    else
    {
        ret["touched"] = false;
        ret["index_available"] = false;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

void EmulatorAPI::getTTDCoverageScan(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    ttd::TimeTravelManager* mgr = context ? context->pTimeTravelManager : nullptr;

    const std::string fromStr = req->getParameter("from_frame");
    const std::string toStr = req->getParameter("to_frame");
    const std::string kindStr = req->getParameter("kind");
    const std::string addrFromStr = req->getParameter("addr_from");
    const std::string addrToStr = req->getParameter("addr_to");
    const std::string pageStr = req->getParameter("phys_page");
    const std::string limitStr = req->getParameter("limit");

    uint64_t fromFrame = 0;
    if (!fromStr.empty() && !ParseUint64Param(fromStr, fromFrame))
    {
        auto resp = CoverageBadRequest("Invalid from_frame: '" + fromStr + "' (expected unsigned integer)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint64_t toFrame = mgr ? mgr->ReadSessionInfo().currentEndFrame : 0;
    if (!toStr.empty() && !ParseUint64Param(toStr, toFrame))
    {
        auto resp = CoverageBadRequest("Invalid to_frame: '" + toStr + "' (expected unsigned integer)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ttd::TTDCoverageKind kind = ttd::TTDCoverageKind::Executed;
    if (!kindStr.empty() && !ttd::TTDCoverageKindFromString(kindStr, kind))
    {
        auto resp = CoverageBadRequest("Invalid kind: '" + kindStr + "' (expected executed, written or read)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint16_t addrFrom = 0;
    if (!addrFromStr.empty() && !ParseUint16Param(addrFromStr, addrFrom))
    {
        auto resp = CoverageBadRequest("Invalid addr_from: '" + addrFromStr + "' (expected 16-bit address)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint16_t addrTo = 0xFFFF;
    if (!addrToStr.empty() && !ParseUint16Param(addrToStr, addrTo))
    {
        auto resp = CoverageBadRequest("Invalid addr_to: '" + addrToStr + "' (expected 16-bit address)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (addrFrom > addrTo)
    {
        char rangeBuf[64];
        snprintf(rangeBuf, sizeof(rangeBuf), "addr_from (0x%04X) must not exceed addr_to (0x%04X)", addrFrom, addrTo);
        auto resp = CoverageBadRequest(rangeBuf);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::optional<ttd::PhysPage> physPage;
    uint16_t pageVal = 0;
    if (!pageStr.empty())
    {
        if (!ParseUint16Param(pageStr, pageVal) || pageVal > ttd::kPhysPageMax)
        {
            auto resp = CoverageBadRequest("Invalid phys_page: '" + pageStr + "' (expected 0..255)");
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        physPage = static_cast<ttd::PhysPage>(pageVal);
    }

    size_t limit = 200;
    if (!limitStr.empty())
    {
        uint64_t limitVal = 0;
        if (!ParseUint64Param(limitStr, limitVal) || limitVal == 0)
        {
            auto resp = CoverageBadRequest("Invalid limit: '" + limitStr + "' (expected integer >= 1)");
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        limit = static_cast<size_t>(limitVal);
    }

    Json::Value ret;
    ret["kind"] = ttd::TTDCoverageKindToString(kind);

    char hexBuf[16];
    snprintf(hexBuf, sizeof(hexBuf), "0x%04X", addrFrom);
    ret["addr_from"] = hexBuf;
    snprintf(hexBuf, sizeof(hexBuf), "0x%04X", addrTo);
    ret["addr_to"] = hexBuf;

    if (physPage) ret["phys_page"] = *physPage;

    if (mgr)
    {
        auto res = mgr->QueryCoverageScan(fromFrame, toFrame, kind, addrFrom, addrTo, physPage, limit);
        ret["scanned_frames"] = Json::UInt64(res.scannedFrames);
        ret["matching_frames"] = Json::UInt64(res.matchingFrames);
        ret["first_match"] = Json::UInt64(res.firstMatch);
        ret["last_match"] = Json::UInt64(res.lastMatch);
        ret["truncated"] = res.truncated;
        ret["index_available"] = res.indexAvailable;
        if (res.indexAvailable)
        {
            // Echo the covered window so a clamped request range is visible to callers.
            ret["covered_from"] = Json::UInt64(res.coveredFrom);
            ret["covered_to"] = Json::UInt64(res.coveredTo);
        }

        Json::Value frameArray(Json::arrayValue);
        for (uint64_t f : res.frames)
        {
            frameArray.append(Json::UInt64(f));
        }
        ret["frames"] = frameArray;
    }
    else
    {
        ret["scanned_frames"] = Json::UInt64(0);
        ret["matching_frames"] = Json::UInt64(0);
        ret["first_match"] = Json::UInt64(0);
        ret["last_match"] = Json::UInt64(0);
        ret["truncated"] = false;
        ret["index_available"] = false;
        ret["frames"] = Json::Value(Json::arrayValue);
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

void EmulatorAPI::getTTDCoverageSummary(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    ttd::TimeTravelManager* mgr = context ? context->pTimeTravelManager : nullptr;

    const std::string fromStr = req->getParameter("from_frame");
    const std::string toStr = req->getParameter("to_frame");
    const std::string kindStr = req->getParameter("kind");
    const std::string bucketStr = req->getParameter("bucket_size");
    const std::string limitStr = req->getParameter("limit");

    uint64_t fromFrame = 0;
    if (!fromStr.empty() && !ParseUint64Param(fromStr, fromFrame))
    {
        auto resp = CoverageBadRequest("Invalid from_frame: '" + fromStr + "' (expected unsigned integer)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint64_t toFrame = mgr ? mgr->ReadSessionInfo().currentEndFrame : 0;
    if (!toStr.empty() && !ParseUint64Param(toStr, toFrame))
    {
        auto resp = CoverageBadRequest("Invalid to_frame: '" + toStr + "' (expected unsigned integer)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::optional<ttd::TTDCoverageKind> kind;
    if (!kindStr.empty())
    {
        ttd::TTDCoverageKind parsedKind;
        if (!ttd::TTDCoverageKindFromString(kindStr, parsedKind))
        {
            auto resp = CoverageBadRequest("Invalid kind: '" + kindStr + "' (expected executed, written or read)");
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        kind = parsedKind;
    }

    uint64_t bucketSize = 0;
    if (!bucketStr.empty() && !ParseUint64Param(bucketStr, bucketSize))
    {
        auto resp = CoverageBadRequest("Invalid bucket_size: '" + bucketStr + "' (expected unsigned integer)");
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    size_t limit = 100;
    if (!limitStr.empty())
    {
        uint64_t limitVal = 0;
        if (!ParseUint64Param(limitStr, limitVal) || limitVal == 0)
        {
            auto resp = CoverageBadRequest("Invalid limit: '" + limitStr + "' (expected integer >= 1)");
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        limit = static_cast<size_t>(limitVal);
    }

    Json::Value ret;
    ret["from_frame"] = Json::UInt64(fromFrame);
    ret["to_frame"] = Json::UInt64(toFrame);

    if (mgr)
    {
        auto res = mgr->QueryCoverageSummary(fromFrame, toFrame, kind, bucketSize, limit);
        ret["bucket_size"] = Json::UInt64(res.bucketSize);
        ret["bucket_count"] = Json::UInt64(res.bucketCount);
        ret["index_available"] = res.indexAvailable;
        if (res.indexAvailable)
        {
            // Echo the covered window so a clamped request range is visible to callers.
            ret["covered_from"] = Json::UInt64(res.coveredFrom);
            ret["covered_to"] = Json::UInt64(res.coveredTo);
        }

        Json::Value bucketArray(Json::arrayValue);
        for (const auto& b : res.buckets)
        {
            Json::Value bObj;
            bObj["frame_start"] = Json::UInt64(b.frameStart);
            bObj["frame_end"] = Json::UInt64(b.frameEnd);
            bObj["executed_distinct"] = b.executedDistinct;
            bObj["written_distinct"] = b.writtenDistinct;
            bObj["read_distinct"] = b.readDistinct;
            bObj["has_keyframe"] = b.hasKeyframe;
            bucketArray.append(bObj);
        }
        ret["buckets"] = bucketArray;
    }
    else
    {
        ret["bucket_size"] = Json::UInt64(0);
        ret["bucket_count"] = Json::UInt64(0);
        ret["index_available"] = false;
        ret["buckets"] = Json::Value(Json::arrayValue);
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api

/// @brief GET  /api/v1/emulator/{id}/ttd/journal - the write journal's state and spans
///        POST /api/v1/emulator/{id}/ttd/journal {"enabled": bool} - switch it at any
///        moment, also while recording (a segment starts or ends there, D40)
void EmulatorAPI::journalTTD(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{    std::map<std::string, std::string> options;
    if (req->method() == drogon::Post)
    {
        auto json = req->getJsonObject();
        if (!json || !json->isMember("enabled") || !(*json)["enabled"].isBool())
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "body: {\"enabled\": true | false}";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        options["enabled"] = (*json)["enabled"].asBool() ? "true" : "false";
    }
    RespondTTD(id, "journal", std::move(options), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/journal/build {"from_frame": n, "to_frame": n}
/// Builds the write journal for frames from..to (default: the whole session) by
/// replaying them, about 2-4 ms per frame. Answers when done; GET /ttd/journal
/// shows the progress meanwhile, POST /ttd/journal/build/cancel stops it
void EmulatorAPI::buildJournalTTD(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "journal-build", std::move(options), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/journal/build/cancel - stop a running build
/// after its current frame; what it built is kept
void EmulatorAPI::cancelJournalBuildTTD(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        const std::string& id) const
{    (void)req;
    RespondTTD(id, "journal-build-cancel", {}, callback);
}
