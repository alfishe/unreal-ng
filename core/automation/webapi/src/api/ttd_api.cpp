/// @file ttd_api.cpp
/// @brief WebAPI TTD (Time-Travel Debug) endpoints.
///
/// Per parent TDD §10.4 and implementation-plan §3.A1 item 7. Only the
/// status endpoint ships in Phase 1 (test observability); the rest of the
/// TTD automation surface (start/stop/seek/step/find_last) lands with
/// Phase 4 alongside the reverse-search engine.
///
/// The status endpoint surfaces every field of TTDSessionInfo so automation
/// clients and the divergence-test harness can poll the recorder without
/// linking against the TTD headers directly.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

#include "../emulator_api.h"
#include "debugger/ttd/ttd_manager.h"

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

    if (ttd::TTDManager* mgr = context->pTTDManager)
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

}  // namespace v1
}  // namespace api
