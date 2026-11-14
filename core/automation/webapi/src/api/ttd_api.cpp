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
/// TTD branches (a resume in the past keeps the old future as a branch):
///   GET    /ttd/branches                   — the branch tree, filtered and paged
///   POST   /ttd/branches                   — create one at a moment, optionally run headless with scripted input
///   POST   /ttd/branches/prune             — delete the branches a filter picks (a dry run by default)
///   GET    /ttd/branches/{branch}          — one branch with its children, ancestors and files
///   PATCH  /ttd/branches/{branch}          — rename, note, tags, pin
///   DELETE /ttd/branches/{branch}          — delete it and its descendants (?force=true: pinned ones too)
///   POST   /ttd/branches/{branch}/switch   — continue on it (at frame / tinframe, default its end)
///
/// Phase 4 (reverse search):
///   POST /ttd/dump          — serialize session to .ttd file
///   POST /ttd/find-last     — reverse search: find last access at address
///   POST /ttd/step-instruction — step one instruction back or forward
///
/// Phase 4 (reverse execution):
///   POST /ttd/reverse-step       — step back N instructions or T t-states
///   POST /ttd/reverse-step-over  — step back over a routine whose return comes before, else one instruction
///   POST /ttd/reverse-continue   — run backward until any PC matches
///
/// Every route turns its request into a TTDControl verb (Phase 5, Step 1): the
/// verbs, their checks and their answers are shared with the CLI, Lua and Python
/// (core/src/debugger/ttd/ttdcontrol.h). The status endpoint surfaces every field
/// of TTDSessionInfo so automation clients and the divergence-test harness can
/// poll the recorder without linking against the TTD headers directly.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <json/json.h>

#include <map>
#include <string>

#include "../common/longcallpool.h"
#include "../common/statenode_json.h"
#include "../emulator_api.h"
#include "debugger/ttd/ttdcontrol.h"

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

// ---------------------------------------------------------------------------
// The verbs TTDControl implements for every surface (Phase 5, Step 1): a route
// turns its JSON body into string options and sends the reply back as JSON
// ---------------------------------------------------------------------------
static void RespondTTD(const std::string& id, const std::string& verb, std::map<std::string, std::string> options,
                       std::function<void(const HttpResponsePtr&)>& callback,
                       const std::function<void(ttd::TTDReply&)>& reshape = {})
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
    ttd::TTDReply reply = ttd::TTDControl(emulator->GetContext()).Execute({verb, std::move(options)});
    if (reshape)
        reshape(reply);
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(reply.ToValue()));
    resp->setStatusCode(static_cast<HttpStatusCode>(reply.HttpStatus()));
    addCorsHeaders(resp);
    callback(resp);
}

/// A GET route's query parameters as TTDControl options: only the names the verb
/// takes, so an unrelated parameter (a cache buster) is ignored as before
static std::map<std::string, std::string> OptionsFromQuery(const HttpRequestPtr& req, const std::string& verb)
{
    std::map<std::string, std::string> options;
    for (const std::string& name : ttd::TTDControl::OptionsFor(verb))
    {
        const std::string value = req->getParameter(name);
        if (!value.empty())
            options[name] = value;
    }
    return options;
}

/// A JSON body's scalar members as TTDControl options (null: absent); an array of scalars (tags, watched
/// addresses) as its items joined by commas. False when a member is an object or holds one: the verbs take
/// scalars and lists only
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
        if (v.isArray())
        {
            std::string joined;
            bool scalars = true;
            for (const Json::Value& item : v)
            {
                scalars = scalars && !item.isObject() && !item.isArray() && !item.isNull();
                if (scalars)
                    joined += (joined.empty() ? "" : ",") + (item.isBool() ? (item.asBool() ? "true" : "false")
                                                                            : item.asString());
            }
            if (scalars)
            {
                options[name] = joined;
                continue;
            }
        }
        if (v.isObject() || v.isArray())
        {
            Json::Value error;
            error["error"]   = "Bad Request";
            error["message"] = "'" + name + "' must be a number, a string, true / false or a list of them";
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



/// @brief GET /api/v1/ttd/file-info?path=<file.ttd>
/// A .ttd file's header, sections and recorded machine, read without loading
/// it (no emulator instance involved): provision a matching machine first.
void EmulatorAPI::getTTDFileInfo(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    // A file on disk: no emulator instance involved
    const ttd::TTDReply reply = ttd::TTDControl(nullptr).Execute({"file-info", {{"path", req->getParameter("path")}}});
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(reply.ToValue()));
    resp->setStatusCode(static_cast<HttpStatusCode>(reply.HttpStatus()));
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
/// JSON body: { "frame": <uint64>, "tinframe": <uint32, optional; without it the engine lands at the frame's end (D13)> }
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
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "export-clip", std::move(options), callback);
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
/// Response: { "count": N, "bookmarks": [ { "frame", "tinframe", "label", "note", "tags", "context", "watch" } ] }
/// Time-sorted. Unlike /ttd/markers these entries never halt a seek.
void EmulatorAPI::getTTDBookmarks(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    // ?tag=T (only the bookmarks with that tag), &text=true (one line per bookmark)
    RespondTTD(id, "bookmarks", OptionsFromQuery(req, "bookmarks"), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/bookmarks
///
/// Body: { "label": "umt entry", "frame": F (optional), "tinframe": T (optional), "note": "...", "tags": ["a", "b"]
/// or "a,b", "watch": ["#A130", "#A140:4"] or "#A130,#A140:4", "context": true (default) }
/// When frame is omitted the bookmark is placed at the current position. The context (registers, the
/// instruction, its page, the paging latch) and the watched bytes are taken at the bookmark's moment.
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

/// @brief PUT /api/v1/emulator/{id}/ttd/bookmarks/{label} (only the fields given change)
///
/// Body: { "note": "...", "append": true (add the note as a new line), "tags": [...] (replace), "add_tags": [...],
/// "remove_tags": [...], "rename": "new label" }. 200 with the bookmark, 400 for a broken bound, 404 for an unknown
/// label, 409 for a rename onto an existing label.
void EmulatorAPI::putTTDBookmark(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id, const std::string& label) const
{
    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    options["label"] = label;
    RespondTTD(id, "bookmark-note", std::move(options), callback);
}

/// @brief GET or POST /api/v1/emulator/{id}/ttd/export-bookmarks (beside export-clip: not under bookmarks/{label})
///
/// Query (GET) or body (POST): format = markdown (a WALKTHROUGH section, default) | profile (YAML comment lines for a
/// game profile's header) | json, tag, title, path (written there as well). Response: { "format", "count", "text",
/// "path", "bytes" }. The machine does not move.
void EmulatorAPI::exportTTDBookmarks(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    std::map<std::string, std::string> options = OptionsFromQuery(req, "bookmark-export");
    if (req->method() == drogon::Post && !OptionsFromJson(req, options, callback))
        return;
    RespondTTD(id, "bookmark-export", std::move(options), callback);
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
// TTD branches: a resume in the past keeps the old future as a branch. {branch} names one by its id, its GUID or its
// name; the trunk is id 0. Every route is a WebSocket `call` as well.
// -------------------------------------------------------------------------

/// @brief GET /api/v1/emulator/{id}/ttd/branches
///
/// Query: scope = all (default) | lineage, created_by (prefix), tag, name_prefix, pinned = true | false, limit, offset.
/// Response: { "branches": [ { "id", "guid", "name", "parent", "fork", "end", "length", "created_by", "reason", "run",
/// "tags", "note", "pinned", "children", "active", "on_lineage", ... } ], "total", "count", "active", "viewed",
/// "lineage": [ { "branch", "from" } ], "text" }. A published copy: the machine never parks for it.
void EmulatorAPI::getTTDBranches(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    RespondTTD(id, "branches", OptionsFromQuery(req, "branches"), callback);
}

/// @brief GET /api/v1/emulator/{id}/ttd/branches/{branch}
///
/// One branch (the branch-info verb): { "branch": {...}, "children": [ {id, guid, name, fork, length} ],
/// "ancestors": [ {id, guid, name} ] (from the trunk), "files": [ {name, bytes} ], "file_bytes", "folder", "active",
/// "text" }. 404 for an unknown branch. {branch}: an id or a GUID first, then a name.
void EmulatorAPI::getTTDBranch(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id, const std::string& branch) const
{
    (void)req;
    RespondTTD(id, "branch-info", {{"branch", branch}}, callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/branches
///
/// Body: { "frame": F, "tinframe": T (default: where the machine stands), "branch": <lineage> (default the active
/// one), "name", "reason", "tags": [...], "note", "pinned", "run": "<run id>", "created_by" (default "webapi"),
/// "media": "ask" | "empty" | "keep", "run_frames": N (0..10000: the machine runs the new branch headless and records
/// it), "input": ["0:a", "10:caps+5:4", "20:space:down", "30:space:up"], "read": ["#5C00:16"], "go_back": true }.
/// 200 with { "created", "branch", "parent", "at", "ran_frames", "aborted", "end", "reads", "returned_to", "state",
/// "text" } (media=ask with a medium to decide: { "created": false, "needs_decision", "media" }); 400 for a bad
/// option, 404 for an unknown branch, 409 without a recorded history. A run takes its time: it answers from the
/// long-call pool, the HTTP workers stay free.
void EmulatorAPI::createTTDBranch(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    options.emplace("created_by", "webapi");
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([id, options = std::move(options), answer]() mutable {
        RespondTTD(id, "branch-create", std::move(options), *answer);
    });
}

/// @brief POST /api/v1/emulator/{id}/ttd/branches/prune
///
/// Body: the GET /ttd/branches filter ("created_by", "run", "tag", "name_prefix", "pinned", "min_length",
/// "max_length", "created_after", "created_before", "scope") and "dry_run" (default true). 200 with { "dry_run",
/// "would_delete" | "deleted": [ {id, guid, name, length} ], "kept": [ {id, guid, name, reason} ], "frames",
/// "file_bytes", "memory_bytes_freed", "text" }; 400 without a filter. The active lineage and pinned branches stay.
void EmulatorAPI::pruneTTDBranches(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    RespondTTD(id, "branch-prune", std::move(options), callback);
}

/// @brief PATCH /api/v1/emulator/{id}/ttd/branches/{branch} (only the fields given change)
///
/// Body: { "name": "...", "note": "...", "tags": [...] or "a,b" (replace), "add_tags": [...], "remove_tags": [...],
/// "pinned": true | false }. 200 with { "updated", "branch", "text" }, 400 for nothing to change or a broken bound,
/// 404 for an unknown branch, 409 for a name another branch has.
void EmulatorAPI::patchTTDBranch(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id, const std::string& branch) const
{
    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    options["branch"] = branch;
    RespondTTD(id, "branch-update", std::move(options), callback);
}

/// @brief DELETE /api/v1/emulator/{id}/ttd/branches/{branch}[?force=true]
///
/// Deletes the branch and its descendants: { "deleted", "removed": [ids], "removed_names": [...], "text" }. 404 for
/// an unknown branch; 409 when it is the trunk, on the active lineage, or pinned (force=true deletes pinned ones).
void EmulatorAPI::deleteTTDBranch(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id, const std::string& branch) const
{
    std::map<std::string, std::string> options = OptionsFromQuery(req, "branch-delete");
    options["branch"] = branch;
    RespondTTD(id, "branch-delete", std::move(options), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/branches/{branch}/switch
///
/// Body (optional): { "frame": F, "tinframe": T } - the moment on the branch to arrive at (default: its end).
/// Response: { "switched", "branch", "arrived_at", "state", "check", "text" }. 404 for an unknown branch, 409 when
/// the switch is refused (recording, a moment off the branch).
void EmulatorAPI::switchTTDBranch(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id, const std::string& branch) const
{
    std::map<std::string, std::string> options;
    if (!OptionsFromJson(req, options, callback))
        return;
    options["branch"] = branch;
    RespondTTD(id, "branch-switch", std::move(options), callback);
}

// -------------------------------------------------------------------------
// Phase 4 — Reverse search + dump + instruction step
// -------------------------------------------------------------------------

/// @brief POST /api/v1/emulator/{id}/ttd/dump
void EmulatorAPI::dumpTTD(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "dump", std::move(options), callback);
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
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "load", std::move(options), callback);
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
///         loading it, the current session is not touched; "context": each
///         hit's registers at its instruction, replayed (default on, off while
///         a recording runs) }
/// Response: { "event", "direction": "in"|"out", "count", "truncated",
///             "scanned", "context", "hits": [ { "index", "frame", "tinframe",
///             "port", "value", "pc", "ay_register"?, "rows"?, "keys"?,
///             "registers"?, "stack_top"?, "context_error"? } ] }
void EmulatorAPI::portEventsTTD(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "port-events", std::move(options), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/memory-at
///
/// A memory at a past checkpoint from the engine's store, without seeking (memory-spaces design, step 3).
/// Body: { "space": "ram" | "ramN" | a region name or alias ("sprinter.vram", "vram", "neogs.ram", ...), "frame",
///         "offset" (default 0), "length" (default 256, at most 65536) }; space "cpu": Z80 addresses as the CPU saw
///         them at frame / tinframe
/// Response: { "space" (the engine's region), "offset", "length", "frame", "at_frame" (the checkpoint read, the
///             newest at or before frame), "exact", "hex" }; space "cpu" adds "banks"
void EmulatorAPI::memoryAtTTD(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "memory-at", std::move(options), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/memory-diff
///
/// The bytes of a memory that differ between two checkpoints (memory-spaces design, step 3).
/// Body: { "space", "from_frame", "to_frame", "limit" (ranges, default 100) }
/// Response: { "space", "from_frame", "to_frame", "at_from", "at_to", "changed_bytes", "ranges": [{offset, length}],
///             "truncated" }
void EmulatorAPI::memoryDiffTTD(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "memory-diff", std::move(options), callback);
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

/// @brief POST /api/v1/emulator/{id}/ttd/reverse-step-over
///
/// Body: {} (none). One instruction back, or back past a whole routine when that instruction is its return.
///
/// Response: { "stepped", "over_call", "skipped"?, "call_out_of_reach"?, "blocking_marker"?, "pc"?, "frame",
///             "tinframe", "text" }
void EmulatorAPI::reverseStepOverTTD(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    std::map<std::string, std::string> options;
    if (OptionsFromJson(req, options, callback))
        RespondTTD(id, "reverse-step-over", std::move(options), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/probe-frames
///
/// Body: { "probes": [{ "probe": "exec"|"read"|"write"|"port-in"|"port-out", "address", "end"?, "page"?,
///         "port_mask"?, "if"? }, ...] } (at most 16, a game profile's probe shape), or one probe's fields at the top
///         level; "from_frame"?, "to_frame"?, "limit"? (frames listed per probe, default 1000).
///
/// Response: { "from_frame", "to_frame", "replayed_frames", "pruned_frames", "barrier_frames": [...],
///             "blocked_by_marker"?, "probes": [{ ..., "error"?, "frame_count", "hit_count", "frames": [...],
///             "hits": [...], "truncated" }], "truncated", "next_from_frame"? }. The machine stays where it stood.
void EmulatorAPI::probeFramesTTD(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    // probes: a JSON array of probe objects; the verb takes it as JSON text. The other members are scalars
    std::map<std::string, std::string> options;
    auto json = req->getJsonObject();
    if (json && json->isObject())
    {
        for (const std::string& name : json->getMemberNames())
        {
            const Json::Value& v = (*json)[name];
            if (v.isNull())
                continue;
            if (name == "probes" && v.isArray())
            {
                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                options[name] = Json::writeString(writer, v);
                continue;
            }
            if (v.isObject() || v.isArray())
            {
                Json::Value error;
                error["error"]   = "Bad Request";
                error["message"] = "'" + name + "' must be a number, a string or true / false (probes: an array)";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
            options[name] = v.isBool() ? (v.asBool() ? "true" : "false") : v.asString();
        }
    }
    RespondTTD(id, "probe-frames", std::move(options), callback);
}

/// @brief POST /api/v1/emulator/{id}/ttd/reverse-continue
///
/// Body: { "pcs": [int, ...], "phys_page"?, "all"?, "limit"?, "before_frame"?, "before_tin"?, "before"?,
///         "from_frame"? }  — non-empty list of reverse breakpoints; the rest narrow the walk.
///
/// Response: { "matched": bool, "pc": int, "frame": int, "tinframe": int, "t": int, "phys_page": int|"rom",
///             "blocked_by_marker"?: { ... } }; with all: { "hits": [...], "count", "truncated", "next_before"? }
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
    // The other members (phys_page, all, limit, before_frame, before_tin, before, from_frame) are scalars
    std::map<std::string, std::string> options{{"pcs", pcs}};
    for (const std::string& name : json->getMemberNames())
    {
        const Json::Value& v = (*json)[name];
        if (name == "pcs" || v.isNull())
            continue;
        if (v.isArray())
        {
            std::string joined;
            bool scalars = true;
            for (const Json::Value& item : v)
            {
                scalars = scalars && !item.isObject() && !item.isArray() && !item.isNull();
                if (scalars)
                    joined += (joined.empty() ? "" : ",") + (item.isBool() ? (item.asBool() ? "true" : "false")
                                                                            : item.asString());
            }
            if (scalars)
            {
                options[name] = joined;
                continue;
            }
        }
        if (v.isObject() || v.isArray())
        {
            Json::Value error;
            error["error"]   = "Bad Request";
            error["message"] = "'" + name + "' must be a number, a string, true / false or a list of them";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        options[name] = v.isBool() ? (v.asBool() ? "true" : "false") : v.asString();
    }
    RespondTTD(id, "reverse-continue", std::move(options), callback);
}

void EmulatorAPI::getTTDCoverageProbe(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback,
                                      const std::string& id) const
{
    RespondTTD(id, "coverage-probe", OptionsFromQuery(req, "coverage-probe"), callback);
}

void EmulatorAPI::getTTDCoverageScan(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    RespondTTD(id, "coverage-scan", OptionsFromQuery(req, "coverage-scan"), callback);
}

void EmulatorAPI::getTTDCoverageRare(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    RespondTTD(id, "coverage-rare", OptionsFromQuery(req, "coverage-rare"), callback);
}

void EmulatorAPI::getTTDCoverageSummary(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        const std::string& id) const
{
    RespondTTD(id, "coverage-summary", OptionsFromQuery(req, "coverage-summary"), callback);
}

void EmulatorAPI::getTTDTimeline(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    RespondTTD(id, "timeline", OptionsFromQuery(req, "timeline"), callback);
}

void EmulatorAPI::getTTDCauseChain(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    RespondTTD(id, "cause-chain", OptionsFromQuery(req, "cause-chain"), callback);
}

void EmulatorAPI::getTTDBacktrace(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    RespondTTD(id, "backtrace", OptionsFromQuery(req, "backtrace"), callback);
}

void EmulatorAPI::getTTDFindEvent(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    RespondTTD(id, "find-event", OptionsFromQuery(req, "find-event"), callback);
}

void EmulatorAPI::getTTDExplainFrame(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    RespondTTD(id, "explain-frame", OptionsFromQuery(req, "explain-frame"), callback);
}

/// @brief GET /api/v1/emulator/{id}/ttd/diff
/// What differs between two moments (docs/inprogress/2026-11-03-ttd-diff-moments): registers, latches, the slot
/// mapping, memory ranges with the last write in between, screen, devices, the time, keys and markers in between
void EmulatorAPI::getTTDDiffMoments(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id) const
{
    RespondTTD(id, "diff", OptionsFromQuery(req, "diff"), callback);
}

/// @brief GET /api/v1/emulator/{id}/ttd/replay-check
/// Does the replay reproduce the recording (docs/inprogress/2026-11-05-ttd-replay-check): each frame of the range
/// replayed from its checkpoint and compared with the next checkpoint; what diverged, by part of the machine
void EmulatorAPI::getTTDReplayCheck(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id) const
{
    RespondTTD(id, "replay-check", OptionsFromQuery(req, "replay-check"), callback);
}

/// @brief GET  /api/v1/emulator/{id}/ttd/seek-check - whether seeks check their frame, the counts
///        POST /api/v1/emulator/{id}/ttd/seek-check {"enabled": bool} - switch it (off by default)
void EmulatorAPI::seekCheckTTD(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    std::map<std::string, std::string> options;
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
    RespondTTD(id, "seek-check", std::move(options), callback);
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
