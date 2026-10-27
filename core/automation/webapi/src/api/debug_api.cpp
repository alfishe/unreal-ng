// WebAPI Debug Commands Implementation
// Debug endpoints for stepping, breakpoints, and inspection
// Created 2026-01-21

#include "../common/binaryresponse.h"
#include "../common/longcallpool.h"
#include "../common/jsonnumber.h"
#include "../common/session_helper.h"
#include "../common/statenode_json.h"
#include "../emulator_api.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <drogon/HttpAppFramework.h>
#include <drogon/HttpResponse.h>
#include <trantor/net/EventLoop.h>
#include <drogon/utils/Utilities.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/execution/runcontrol.h>
#include <emulator/cpu/z80.h>
#include <emulator/memory/memory.h>
#include <emulator/memory/memoryaccesstracker.h>
#include <emulator/memory/memorymap.h>
#include <debugger/debugmanager.h>
#include <debugger/memory/memoryread.h>
#include <debugger/pchistory/pchistory.h>
#include <debugger/ports/portwrite.h>
#include <debugger/snapshot/debugsnapshot.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/breakpoints/breakpointselection.h>
#include <debugger/targets/debugtarget.h>
#include <debugger/keyboard/debugkeyboardmanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <debugger/labels/labelmanager.h>
#include <debugger/listing/listingparser.h>
#include <debugger/assembler/z80textassembler.h>
#include <base/featuremanager.h>
#include <common/dumphelper.h>
#include <common/stringhelper.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);
extern std::string stateToString(EmulatorStateEnum state);

// Shared TD-3 Phase 1 memory window renderer (defined in state_memory_api.cpp)
extern Json::Value RenderMemoryWindowFormat(const uint8_t* data, size_t size, uint32_t address,
                                            const std::string& format);

/// Helper: Get emulator or return 404
static std::shared_ptr<Emulator> getEmulatorOrError(
    const std::string& id,
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

/// 409 revoked_by_user when the direct run this call made was cut short because the user took control back from the
/// automation session (execution arbiter design §6a): the client must stop, not retry. True when it answered
static bool ReplyIfControlTakenBack(const std::shared_ptr<Emulator>& emulator,
                                    std::function<void(const HttpResponsePtr&)>& callback)
{
    if (!emulator->LastDirectRunAborted())
        return false;
    auto resp = HttpResponse::newHttpJsonResponse(webapi_session::RevokedBody());
    resp->setStatusCode(HttpStatusCode::k409Conflict);
    addCorsHeaders(resp);
    callback(resp);
    return true;
}

/// 409 "Run-control held by <surface>" when another surface (GDB) holds run control: the one rule for every call
/// that advances the CPU (debugger additions tdd §5, F5). True when it answered
static bool RunControlHeldReply(const HttpRequestPtr& req, const std::shared_ptr<Emulator>& emulator,
                                std::function<void(const HttpResponsePtr&)>& callback)
{
    EmulatorContext* ctx = emulator->GetContext();
    // The session holding control (its X-Unreal-Session header) steps; everybody else is refused
    if (!ctx || !ctx->IsRunControlClaimed() || webapi_session::HoldsControl(req, ctx))
        return false;
    Json::Value error;
    error["error"] = "Run-control held";
    error["message"] = "Run-control held by " + ctx->GetRunControlState().surfaceLabel + ". Use that surface to step.";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(HttpStatusCode::k409Conflict);
    addCorsHeaders(resp);
    callback(resp);
    return true;
}

/// A RunControl reply as the route answers it: the body; revoked_by_user as every revoked reply; an error with its
/// phrase and message
static void ReplyRun(const execution::RunReply& reply, std::function<void(const HttpResponsePtr&)>& callback)
{
    Json::Value body = reply.status == execution::RunReply::Status::Revoked ? webapi_session::RevokedBody()
                                                                            : StateNodeToJson(reply.body);
    if (!reply.Ok() && reply.status != execution::RunReply::Status::Revoked)
    {
        body["error"] = reply.error;
        body["message"] = reply.message;
    }
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(static_cast<HttpStatusCode>(reply.HttpStatus()));
    addCorsHeaders(resp);
    callback(resp);
}

// region Stepping Commands

namespace
{
/// The end of a step as the protocol's PauseEvent describes it (docs/inprogress/2026-09-28-debugger-model/
/// protocol.md §3.16): reason "step", or "breakpoint" with which one, where and on what access
/// A breakpoint's page as the debugger protocol carries it: {kind: ram | rom | cache, page}; null when it
/// matches the address in any page
Json::Value BreakpointPageJson(const BreakpointDescriptor& bp)
{
    if (bp.spacePage != 0xFFFF)
    {
        // A page of the Sprinter's video RAM (vramN): offsets of that 16 KB page
        Json::Value page;
        page["kind"] = "vram";
        page["page"] = bp.spacePage - 0x110;
        return page;
    }
    if (bp.matchType != BRK_MATCH_BANK_ADDR)
        return Json::Value(Json::nullValue);
    Json::Value page;
    page["kind"] = BreakpointManager::PageKindName(bp.pageType);
    page["page"] = bp.page;
    return page;
}

/// One breakpoint as every breakpoint reply shows it (the protocol's Breakpoint fields)
Json::Value BreakpointJson(const BreakpointDescriptor& bp)
{
    Json::Value j;
    j["id"] = bp.breakpointID;
    j["address"] = bp.z80address;
    if (bp.isRange)
        j["address_end"] = bp.z80addressEnd;
    if (bp.matchType == BRK_MATCH_BANK_ADDR || bp.spacePage != 0xFFFF)
    {
        j["page"] = BreakpointPageJson(bp);
        j["slot_only"] = bp.slotOnly;
    }
    if (bp.type == BRK_IO && bp.portMask != 0xFFFF)
        j["port_mask"] = bp.portMask;
    if (bp.type == BRK_BEAM)
        j["hc"] = bp.beamHc;
    if (bp.type == BRK_KEYBOARD)
    {
        j["key"] = bp.z80address == BRK_KEY_ANY
                       ? std::string("any")
                       : DebugKeyboardManager::GetKeyDisplayName(static_cast<ZXKeysEnum>(bp.z80address));
        j["on"] = (bp.keyType & BRK_KEY_PRESS) && (bp.keyType & BRK_KEY_RELEASE) ? "both"
                  : (bp.keyType & BRK_KEY_RELEASE)                               ? "release"
                                                                                 : "press";
    }
    if (bp.type == BRK_CHIPREG)
    {
        j["chip"] = SoundChipKindName(static_cast<SoundChipKind>(bp.chip));
        if (bp.chipUnit != kSoundChipAnyUnit)
            j["unit"] = bp.chipUnit;
        if (bp.nrMask)
        {
            j["value"] = bp.nrValue & bp.nrMask;
            j["value_mask"] = bp.nrMask;
        }
    }
    if (bp.type == BRK_EVENT)
    {
        j["event"] = GSCardEventName(static_cast<GSCardEvent>(bp.z80address));
        if (bp.nrMask)
        {
            j["value"] = bp.nrValue & bp.nrMask;
            j["value_mask"] = bp.nrMask;
        }
    }
    if (bp.type == BRK_NEXTREG)
    {
        j["doors"] = BreakpointManager::NextRegDoorsName(bp.nrDoors);
        if (bp.nrMask)
        {
            j["value"] = bp.nrValue & bp.nrMask;
            j["value_mask"] = bp.nrMask;
        }
    }
    j["hit_mode"] = BreakpointManager::HitModeName(bp.hitMode);
    if (bp.hitMode != BRK_HIT_ALWAYS)
        j["hit_target"] = static_cast<Json::UInt>(bp.hitTarget);
    j["hit_count"] = static_cast<Json::UInt>(bp.hitCount);
    if (bp.condition)
    {
        // Counted on every match; hit_count counts the matches whose condition held
        j["condition"] = bp.condition->Canonical();
        j["match_count"] = static_cast<Json::UInt>(bp.matchCount);
        if (bp.conditionFailed)
            j["condition_error"] = bp.conditionError;
    }
    j["active"] = bp.active;
    j["note"] = bp.note;
    j["group"] = bp.group;
    return j;
}

/// The page a request names, as {kind, page} or as the text form "ram32"; the text form for messages
std::string BreakpointPageText(const Json::Value& value)
{
    if (value.isObject())
        return value.get("kind", "").asString() + (value.isMember("page") ? value["page"].asString() : std::string());
    return value.asString();
}

Json::Value StepStopJson(const Emulator& emulator)
{
    const Emulator::BreakpointStop& stop = emulator.LastDirectStop();
    Json::Value json;
    json["cpu"] = DebugCpuName(stop.cpu);
    json["reason"] = stop.hit ? "breakpoint" : "step";
    if (stop.hit)
    {
        json["breakpoint_id"] = stop.breakpointId;
        json["address"] = stop.address;
        json["access"] = BreakpointHitKindName(stop.kind);
        if (stop.cpu != DebugCpuId::Main)
            json["cpu_pc"] = stop.cpuPc;
    }
    return json;
}
}  // namespace

/// @brief POST /api/v1/emulator/{id}/step
/// @brief Execute single CPU instruction
void EmulatorAPI::step(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                       const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    try
    {
        // RunControl (core/src/emulator/execution/runcontrol.h): the claim rule and the reply, shared with the actions
        ReplyRun(execution::RunControl(emulator).StepIn(webapi_session::RequesterOf(req, emulator->GetContext())), callback);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Step failed";
        error["message"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/steps
/// @brief Execute N CPU instructions
/// @brief Request body: {"count": N}
void EmulatorAPI::stepsNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                        const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    // Check run-control claim (GDB TDD §3.3 / 1A.7.2)
    auto* ctx = emulator->GetContext();
    if (ctx && ctx->IsRunControlClaimed() && !webapi_session::HoldsControl(req, ctx))
    {
        auto state = ctx->GetRunControlState();
        Json::Value error;
        error["error"] = "Run-control held";
        error["message"] = "Run-control held by " + state.surfaceLabel + ". Use that surface to step.";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k409Conflict);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    try
    {
        auto json = req->getJsonObject();
        unsigned count = json && json->isMember("count") ? (*json)["count"].asUInt() : 1;
        if (count < 1) count = 1;
        if (count > 100000) count = 100000; // Safety limit
        
        // Execute N instructions; a breakpoint ends the run early (the reply's "stop" says which)
        const unsigned executed = emulator->RunNCPUCycles(count, false);
        if (ReplyIfControlTakenBack(emulator, callback))
            return;
        
        Z80State* z80 = emulator->GetZ80State();
        
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Executed " + std::to_string(executed) + " instructions";
        ret["count"] = count;
        ret["executed"] = executed;
        ret["stop"] = StepStopJson(*emulator);
        if (z80)
        {
            ret["pc"] = z80->pc;
            ret["sp"] = z80->sp;
        }
        ret["state"] = stateToString(emulator->GetState());
        
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Steps failed";
        error["message"] = e.what();
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/stepover
/// @brief Step over call instructions
void EmulatorAPI::stepOverNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                       const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    try
    {
        // RunControl (core/src/emulator/execution/runcontrol.h): the claim rule and the reply, shared with the actions
        ReplyRun(execution::RunControl(emulator).StepOver(webapi_session::RequesterOf(req, emulator->GetContext())), callback);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Step over failed";
        error["message"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/stepout
/// @brief Step out of the current subroutine (SP-tracking: runs until a RET-family
/// @brief instruction at/above the entry stack level, then executes it)
void EmulatorAPI::stepOutNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                       const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    try
    {
        // RunControl (core/src/emulator/execution/runcontrol.h): the claim rule and the reply, shared with the actions
        ReplyRun(execution::RunControl(emulator).StepOut(webapi_session::RequesterOf(req, emulator->GetContext())), callback);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Step out failed";
        error["message"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/skip_until
/// @brief Fast-forward execution until PC reaches the target address (or the
/// @brief t-state budget is exhausted). Breakpoints are skipped for the walk —
/// @brief same no-trap rule as step out. Frames rendered during the skip are
/// @brief not captured by the recording subsystem (raw CPU stepping path).
/// @brief Request body: {"pc": "0x8000" | 32768, "max_tstates": 70000000, "breakpoints": false}
void EmulatorAPI::skipUntilNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    try
    {
        auto json = req->getJsonObject();
        if (!json || !json->isMember("pc"))
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "Request body must contain 'pc' field (hex string or integer)";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        // Target address: accept "0x8000" / "8000" strings and plain integers
        uint32_t target32 = 0;
        if (!ParseJsonUInt((*json)["pc"], 0xFFFFFFFFu, target32))
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "'pc' must be a hex string or integer (a number, or a string: decimal, \"0x..\", \"#..\" or \"$..\")";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        if (target32 > 0xFFFF)
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "'pc' is out of the 16-bit address range";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        const uint16_t target = static_cast<uint16_t>(target32);

        // RunControl (core/src/emulator/execution/runcontrol.h): budget 0 = 100 frames, at most 700000000 t-states;
        // breakpoints skipped (the no-trap rule of step out) unless "breakpoints": true (run to a cursor)
        const uint64_t maxTStates = json->isMember("max_tstates") ? (*json)["max_tstates"].asUInt64() : 0;
        const bool breakpoints = json->isMember("breakpoints") && (*json)["breakpoints"].asBool();
        ReplyRun(execution::RunControl(emulator).RunUntilPc(webapi_session::RequesterOf(req, emulator->GetContext()),
                                                            target, maxTStates, breakpoints),
                 callback);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Skip until failed";
        error["message"] = e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/run_tstates
/// @brief Run for N t-states
/// @brief Request body: {"tstates": N}
void EmulatorAPI::runTStatesNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    try
    {
        auto json = req->getJsonObject();
        unsigned tstates = json && json->isMember("tstates") ? (*json)["tstates"].asUInt() : 1;
        if (tstates < 1) tstates = 1;
        if (tstates > 10000000) tstates = 10000000; // Safety limit
        
        emulator->RunTStates(tstates);
        if (ReplyIfControlTakenBack(emulator, callback))
            return;
        
        Z80State* z80 = emulator->GetZ80State();
        
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Ran " + std::to_string(tstates) + " t-states";
        ret["tstates"] = tstates;
        if (z80)
        {
            ret["pc"] = z80->pc;
            ret["sp"] = z80->sp;
        }
        ret["state"] = stateToString(emulator->GetState());
        
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "RunTStates failed";
        error["message"] = e.what();
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/run_to_scanline
/// @brief Run until target scanline
/// @brief Request body: {"scanline": N}
void EmulatorAPI::runToScanlineNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                       const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    auto json = req->getJsonObject();
    if (!json || !json->isMember("scanline"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Request body must contain 'scanline' field";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    const unsigned scanline = (*json)["scanline"].asUInt();
    try
    {
        // RunControl (core/src/emulator/execution/runcontrol.h): the claim rule and the reply, shared with the actions
        ReplyRun(execution::RunControl(emulator).RunToScanline(webapi_session::RequesterOf(req, emulator->GetContext()), scanline), callback);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "RunToScanline failed";
        error["message"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/run_scanlines
/// @brief Run N scanlines from current position
/// @brief Request body: {"count": N}
void EmulatorAPI::runNScanlinesNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                       const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    auto json = req->getJsonObject();
    const unsigned count = json && json->isMember("count") ? (*json)["count"].asUInt() : 1;
    try
    {
        // RunControl (core/src/emulator/execution/runcontrol.h): the claim rule and the reply, shared with the actions
        ReplyRun(execution::RunControl(emulator).RunNScanlines(webapi_session::RequesterOf(req, emulator->GetContext()), count), callback);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "RunNScanlines failed";
        error["message"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/run_to_pixel
/// @brief Run until next screen pixel (skip vblank/borders)
void EmulatorAPI::runToPixelNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    try
    {
        ReplyRun(execution::RunControl(emulator).RunToPixel(webapi_session::RequesterOf(req, emulator->GetContext())),
                 callback);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "RunToPixel failed";
        error["message"] = e.what();
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/run_to_interrupt
/// @brief Run until Z80 accepts maskable interrupt
void EmulatorAPI::runToInterruptNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                       const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    try
    {
        // RunControl (core/src/emulator/execution/runcontrol.h): the claim rule and the reply, shared with the actions
        ReplyRun(execution::RunControl(emulator).RunToInterrupt(webapi_session::RequesterOf(req, emulator->GetContext())), callback);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "RunToInterrupt failed";
        error["message"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/run_frame
/// @brief Run one complete video frame
void EmulatorAPI::runFrameNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                       const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    try
    {
        // RunControl (core/src/emulator/execution/runcontrol.h): the claim rule and the reply, shared with the actions
        ReplyRun(execution::RunControl(emulator).FrameStep(webapi_session::RequesterOf(req, emulator->GetContext())), callback);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "RunFrame failed";
        error["message"] = e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/run_frames
/// @brief Run N complete video frames
/// @brief Request body: {"count": N} (alias "frames" also accepted; other keys are rejected with 400)
void EmulatorAPI::runFramesNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    try
    {
        auto json = req->getJsonObject();
        unsigned count = 1;
        if (json)
        {
            // Accept both "count" (documented) and "frames" (common guess) keys
            if (json->isMember("count"))
            {
                count = (*json)["count"].asUInt();
            }
            else if (json->isMember("frames"))
            {
                count = (*json)["frames"].asUInt();
            }
            else if (!json->getMemberNames().empty())
            {
                // Body present but neither key found - reject instead of silently running 1 frame
                // (silent default caused "blank screen" confusion: users passed a mistyped key,
                // only 1 frame ran, and the loaded program had not redrawn the screen yet)
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Missing 'count' parameter. Expected body: {\"count\": N} (alias: \"frames\")";

                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
        }
        if (count < 1) count = 1;
        if (count > 10000) count = 10000; // Safety limit
        
        emulator->RunNFrames(count);
        if (ReplyIfControlTakenBack(emulator, callback))
            return;
        
        Z80State* z80 = emulator->GetZ80State();
        
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Ran " + std::to_string(count) + " frames";
        ret["count"] = count;
        if (z80)
        {
            ret["pc"] = z80->pc;
            ret["sp"] = z80->sp;
        }
        ret["state"] = stateToString(emulator->GetState());
        
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "RunFrames failed";
        error["message"] = e.what();
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

// endregion Stepping Commands

// region Debug Mode

/// @brief GET /api/v1/emulator/{id}/debugmode
/// @brief Get debug mode status
void EmulatorAPI::getDebugMode(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    FeatureManager* fm = emulator->GetFeatureManager();
    
    Json::Value ret;
    ret["enabled"] = fm ? fm->isEnabled("debugmode") : false;
    ret["breakpoints"] = fm ? fm->isEnabled("breakpoints") : false;
    ret["memorytracking"] = fm ? fm->isEnabled("memorytracking") : false;
    ret["calltrace"] = fm ? fm->isEnabled("calltrace") : false;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT /api/v1/emulator/{id}/debugmode
/// @brief Set debug mode status
/// @brief Request body: {"enabled": true/false}
void EmulatorAPI::setDebugMode(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    auto json = req->getJsonObject();
    bool enabled = json && json->isMember("enabled") ? (*json)["enabled"].asBool() : false;
    
    FeatureManager* fm = emulator->GetFeatureManager();
    if (fm)
    {
        fm->setFeature("debugmode", enabled);
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["enabled"] = enabled;
    ret["message"] = enabled ? "Debug mode enabled" : "Debug mode disabled";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// endregion Debug Mode

// region Breakpoints

namespace
{
void ReplyJson(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body, HttpStatusCode status)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    addCorsHeaders(resp);
    callback(resp);
}

void ReplyError(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode status, const char* error,
                const std::string& message)
{
    Json::Value body;
    body["error"] = error;
    body["message"] = message;
    ReplyJson(callback, body, status);
}

/// The selection of the bulk routes: "select" (all, an id, a kind, group:<name>), or "id" / "kind" / "group" alone;
/// none: all. False after a 400 reply
bool SelectionFromJson(const Json::Value* body, const std::string& query, BreakpointSelection& selection,
                       std::function<void(const HttpResponsePtr&)>& callback)
{
    std::string error;
    bool ok = true;
    if (!query.empty())
        ok = BreakpointSelection::Parse(query, selection, error);
    else if (body != nullptr && body->isMember("select"))
        ok = BreakpointSelection::Parse((*body)["select"].asString(), selection, error);
    else if (body != nullptr && body->isMember("id"))
        ok = BreakpointSelection::Parse((*body)["id"].asString(), selection, error) &&
             (selection.GetKind() == BreakpointSelection::Kind::Id || (error = "'id': a breakpoint id", false));
    else if (body != nullptr && body->isMember("group"))
        selection = BreakpointSelection::Group((*body)["group"].asString());
    else if (body != nullptr && body->isMember("kind"))
        ok = BreakpointSelection::Parse((*body)["kind"].asString(), selection, error);
    else
        selection = BreakpointSelection::All();
    if (!ok)
        ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", error);
    return ok;
}

Json::Value SelectionJson(const BreakpointSelection& selection, const BreakpointManager& manager)
{
    Json::Value ret;
    ret["select"] = selection.Name();
    Json::Value ids(Json::arrayValue);
    for (const uint16_t bpId : selection.Select(manager))
        ids.append(bpId);
    ret["ids"] = ids;
    ret["selected"] = ids.size();
    const std::optional<bool> active = selection.IsActive(manager);
    ret["active"] = active ? Json::Value(*active) : Json::Value(Json::nullValue);
    return ret;
}

/// The breakpoints of the CPU a route names: ?cpu= or the body's "cpu" (main, gs; none: main). Null after the reply
BreakpointManager* ManagerOrError(const std::shared_ptr<Emulator>& emulator, const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>& callback)
{
    std::string name = req->getParameter("cpu");
    if (name.empty())
        if (auto json = req->getJsonObject(); json && (*json)["cpu"].isString())
            name = (*json)["cpu"].asString();
    DebugCpuId cpu = DebugCpuId::Main;
    if (!DebugManager::ParseCpuName(name, cpu))
    {
        ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "'cpu': main, gs or ms");
        return nullptr;
    }
    EmulatorContext* ctx = emulator->GetContext();
    BreakpointManager* bpm = ctx && ctx->pDebugManager ? ctx->pDebugManager->GetBreakpointsManager(cpu) : nullptr;
    if (bpm == nullptr)
        ReplyError(callback, HttpStatusCode::k500InternalServerError, "Internal Error", "Breakpoint manager not available");
    return bpm;
}
}  // namespace

/// @brief GET /api/v1/emulator/{id}/breakpoints
/// @brief List all breakpoints
void EmulatorAPI::getBreakpoints(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    if (!bpm)
        return;
    
    Json::Value ret;
    Json::Value breakpointsArray(Json::arrayValue);
    
    if (bpm)
    {
        // The user's breakpoints; ?all=true adds the ones analyzers, debugger adapters and steps own (with owner)
        const bool all = req->getParameter("all") == "true";
        const auto& allBps = bpm->GetAllBreakpoints();
        for (const auto& pair : allBps)
        {
            const BreakpointDescriptor* bp = pair.second.get();
            if (!bp) continue;
            if (!all && !BreakpointManager::IsUserBreakpoint(*bp)) continue;
            
            Json::Value bpObj;
            bpObj["id"] = bp->breakpointID;
            
            // Determine type string
            switch (bp->type)
            {
                case BRK_MEMORY:
                    bpObj["type"] = "memory";
                    break;
                case BRK_IO:
                    bpObj["type"] = "port";
                    break;
                case BRK_KEYBOARD:
                    bpObj["type"] = "keyboard";
                    break;
                case BRK_NEXTREG:
                    bpObj["type"] = "nextreg";
                    break;
                case BRK_BEAM:
                    bpObj["type"] = "beam";
                    break;
                case BRK_COPPER:
                    bpObj["type"] = "copper";
                    break;
                case BRK_MAGIC:
                    bpObj["type"] = "magic";
                    break;
                case BRK_EVENT:
                    bpObj["type"] = "event";
                    break;
                case BRK_CHIPREG:
                    bpObj["type"] = "chipreg";
                    break;
                default:
                    bpObj["type"] = "unknown";
            }
            
            // address, address_end, page + slot_only, port_mask, hit_mode / hit_target / hit_count
            for (const auto& field : BreakpointJson(*bp).getMemberNames())
                if (field != "id" && field != "active" && field != "note" && field != "group")
                    bpObj[field] = BreakpointJson(*bp)[field];
            
            // Type-specific access flags
            switch (bp->type)
            {
                case BRK_MEMORY:
                    bpObj["execute"] = (bp->memoryType & BRK_MEM_EXECUTE) != 0;
                    bpObj["read"] = (bp->memoryType & BRK_MEM_READ) != 0;
                    bpObj["write"] = (bp->memoryType & BRK_MEM_WRITE) != 0;
                    break;
                case BRK_IO:
                    bpObj["in"] = (bp->ioType & BRK_IO_IN) != 0;
                    bpObj["out"] = (bp->ioType & BRK_IO_OUT) != 0;
                    break;
                case BRK_NEXTREG:
                    bpObj["read"] = (bp->ioType & BRK_IO_IN) != 0;
                    bpObj["write"] = (bp->ioType & BRK_IO_OUT) != 0;
                    break;
                case BRK_KEYBOARD:
                    bpObj["press"] = (bp->keyType & BRK_KEY_PRESS) != 0;
                    bpObj["release"] = (bp->keyType & BRK_KEY_RELEASE) != 0;
                    break;
                default:
                    break;
            }
            
            bpObj["active"] = bp->active;
            bpObj["note"] = bp->note;
            bpObj["group"] = bp->group;
            if (all)
            {
                bpObj["owner"] = bp->owner;
                bpObj["hidden"] = bp->hidden;
            }
            
            breakpointsArray.append(bpObj);
        }
    }
    
    ret["cpu"] = bpm && bpm->Target() ? bpm->Target()->Name() : "main";
    ret["count"] = breakpointsArray.size();
    ret["breakpoints"] = breakpointsArray;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/breakpoints
/// @brief Add a breakpoint: {type, address, address_end?, page?, slot_only?, port_mask?, hits? | hit_mode? +
/// hit_target?, note?, group?} (the debugger protocol's Breakpoint fields; BreakpointManager::AddBreakpoint(spec))
void EmulatorAPI::addBreakpoint(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto badRequest = [&callback](const std::string& message) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = message;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    };

    auto* ctx = emulator->GetContext();
    // "cpu": main (default) or gs: whose breakpoint it is
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    if (!bpm)
        return;

    auto json = req->getJsonObject();
    if (!json)
    {
        badRequest("Request body required");
        return;
    }
    const Json::Value& body = *json;

    // The debugger protocol's Breakpoint fields (protocol.md §3.6, §6.3)
    BreakpointSpec spec;
    const std::string type = body.get("type", "").asString();
    if (type == "execution" || type == "exec" || type == "bp")
        spec.access = BRK_MEM_EXECUTE;
    else if (type == "read" || type == "r")
        spec.access = BRK_MEM_READ;
    else if (type == "write" || type == "w")
        spec.access = BRK_MEM_WRITE;
    else if (type == "rw" || type == "access")
        spec.access = BRK_MEM_READ | BRK_MEM_WRITE;
    else if (type == "port_in" || type == "in")
        spec.type = BRK_IO, spec.access = BRK_IO_IN;
    else if (type == "port_out" || type == "out")
        spec.type = BRK_IO, spec.access = BRK_IO_OUT;
    else if (type == "port" || type == "io")
        spec.type = BRK_IO, spec.access = BRK_IO_IN | BRK_IO_OUT;
    else if (type == "nextreg_write")
        spec.type = BRK_NEXTREG, spec.access = BRK_IO_OUT;
    else if (type == "nextreg_read")
        spec.type = BRK_NEXTREG, spec.access = BRK_IO_IN;
    else if (type == "nextreg")
        spec.type = BRK_NEXTREG, spec.access = BRK_IO_IN | BRK_IO_OUT;
    else if (type == "beam")
        spec.type = BRK_BEAM, spec.access = BRK_IO_OUT;
    else if (type == "copper")
        spec.type = BRK_COPPER, spec.access = BRK_IO_OUT;
    else if (type == "magic_ed_ff" || type == "magic_dd_01")
        spec.type = BRK_MAGIC, spec.access = BRK_IO_OUT;
    else if (type == "keyboard" || type == "key")
        spec.type = BRK_KEYBOARD, spec.access = BRK_KEY_PRESS;
    else if (type == "event")
        spec.type = BRK_EVENT, spec.access = 0;
    else if (type == "chipreg")
    {
        // A sound chip's register write ("chip": ay, fm, saa, opl4, opl4wave, dac; "address" the register,
        // "address_end" a range, "unit" one chip of the kind; chipregister.h)
        SoundChipKind chip = SoundChipKind::Count;
        if (!body["chip"].isString() || !ParseSoundChipKind(body["chip"].asString().c_str(), chip))
        {
            badRequest("'chip': ay, fm, saa, opl4, opl4wave or dac");
            return;
        }
        spec.type = BRK_CHIPREG, spec.access = 0;
        spec.chip = static_cast<uint8_t>(chip);
        uint32_t unit = 0;
        if (body.isMember("unit") && !body["unit"].isNull())
        {
            if (!ParseJsonUInt(body["unit"], 0xFE, unit))
            {
                badRequest("'unit' must be 0..254 (the chip's number on its device)");
                return;
            }
            spec.chipUnit = static_cast<uint8_t>(unit);
        }
    }
    else
    {
        badRequest("Invalid type. Use: execution, read, write, rw, port_in, port_out, port, nextreg_write, nextreg_read, nextreg, beam, copper, magic_ed_ff, magic_dd_01, keyboard, event, chipreg");
        return;
    }

    // A memory address is a number or a label's name (its CPU address); a port or a line is a number
    LabelManager* labels = ctx->pDebugManager->GetLabelManager();
    auto parseAddress = [&](const Json::Value& field, uint32_t& out) {
        if (ParseJsonUInt(field, 0xFFFF, out))
            return true;
        if (spec.type != BRK_MEMORY || !field.isString() || !labels)
            return false;
        const auto label = labels->GetLabelByName(field.asString());
        if (!label)
            return false;
        out = label->address;
        return true;
    };

    uint32_t value = 0;
    if (spec.type == BRK_MAGIC)
    {
        // The opcode is the type (ED FF / DD 01); no address
        value = type == "magic_ed_ff" ? BRK_MAGIC_ED_FF : BRK_MAGIC_DD_01;
    }
    else if (spec.type == BRK_EVENT)
    {
        // A sound card's protocol event or a machine event by name ("event": "card_command", "host_data", "cpu_int",
        // ...; gsdebughooks.h); the card's side on the gs CPU, the host's and the machine's on the main one; "value" /
        // "value_mask" filter its byte
        GSCardEvent kind = GSCardEvent::Count;
        if (!body["event"].isString() || !ParseGSCardEvent(body["event"].asString().c_str(), kind))
        {
            badRequest("'event': int, nmi, reset, page, card_command, card_data, card_reply, dac_fetch, clock_change, "
                       "dma_start, dma_done, gscfg0 (cpu gs), host_command, host_data, host_reply, cpu_int, cpu_reti, cpu_nmi, cpu_retn, machine_reset, divmmc_map, divmmc_unmap, esxdos_call, zxndma_start, zxndma_done, tsdma_start, tsdma_done (cpu main)");
            return;
        }
        value = static_cast<uint32_t>(kind);
    }
    else if (spec.type == BRK_KEYBOARD)
    {
        // A key of the matrix by name ("key": "space", "caps", "a"; "any" or none: any key), "on": press | release |
        // both: the first keyboard read that sees the key change stops
        const std::string key = body.get("key", "any").asString();
        value = BRK_KEY_ANY;
        if (key != "any")
        {
            const ZXKeysEnum code = DebugKeyboardManager::ResolveKeyName(key);
            if (code == ZXKEY_NONE)
            {
                badRequest("'key': a key name (GET /keyboard/keys lists them) or any");
                return;
            }
            value = static_cast<uint32_t>(code);
        }
        const std::string on = body.get("on", "press").asString();
        if (on != "press" && on != "release" && on != "both")
        {
            badRequest("'on': press, release or both");
            return;
        }
        spec.access = on == "press" ? BRK_KEY_PRESS : on == "release" ? BRK_KEY_RELEASE : (BRK_KEY_PRESS | BRK_KEY_RELEASE);
    }
    else if (!parseAddress(body["address"], value))
    {
        badRequest(spec.type == BRK_MEMORY
                       ? "'address' must be 0..65535 (a number, a string: decimal, \"0x..\", \"#..\" or \"$..\", or a label's name)"
                       : "'address' must be 0..65535 (a number, or a string: decimal, \"0x..\", \"#..\" or \"$..\")");
        return;
    }
    spec.address = static_cast<uint16_t>(value);
    if (body.isMember("address_end") && !body["address_end"].isNull())
    {
        if (!parseAddress(body["address_end"], value))
        {
            badRequest("'address_end' must be 0..65535 (or, for memory, a label's name)");
            return;
        }
        spec.hasEnd = true;
        spec.addressEnd = static_cast<uint16_t>(value);
    }
    // "page": {"kind":"ram","page":32} (protocol) or "ram32": a physical breakpoint, through any slot that
    // shows the page; "slot_only": true keeps it to the slot of the address
    if (body.isMember("page") && !body["page"].isNull())
    {
        std::string pageError;
        if (!BreakpointManager::ParsePageInto(BreakpointPageText(body["page"]), spec, pageError))
        {
            badRequest(pageError);
            return;
        }
    }
    spec.slotOnly = body.get("slot_only", false).asBool();
    if (body.isMember("port_mask") && !body["port_mask"].isNull())
    {
        if (!ParseJsonUInt(body["port_mask"], 0xFFFF, value))
        {
            badRequest("'port_mask' must be 0..65535");
            return;
        }
        spec.portMask = static_cast<uint16_t>(value);
    }
    // Beam: 'address' is the line (the copper's counter, NR #64 applied), 'hc' the pixel clock in the row (default 0)
    if (body.isMember("hc") && !body["hc"].isNull())
    {
        if (!ParseJsonUInt(body["hc"], BRK_BEAM_MAX_HC, value))
        {
            badRequest("'hc' must be 0..511");
            return;
        }
        spec.beamHc = static_cast<uint16_t>(value);
    }
    // NextREG: the doors a write comes through, the value written (value alone: exactly; with value_mask: masked)
    if (body.isMember("doors") && !body["doors"].isNull())
    {
        std::string doorsText;
        if (body["doors"].isArray())
            for (const auto& door : body["doors"])
                doorsText += door.asString() + ",";
        else
            doorsText = body["doors"].asString();
        std::string doorsError;
        if (!BreakpointManager::ParseNextRegDoors(doorsText, spec.nrDoors, doorsError))
        {
            badRequest(doorsError);
            return;
        }
    }
    if (body.isMember("value") && !body["value"].isNull())
    {
        if (!ParseJsonUInt(body["value"], 0xFF, value))
        {
            badRequest("'value' must be 0..255");
            return;
        }
        spec.nrValue = static_cast<uint8_t>(value);
        spec.nrMask = 0xFF;
    }
    if (body.isMember("value_mask") && !body["value_mask"].isNull())
    {
        if (!ParseJsonUInt(body["value_mask"], 0xFF, value))
        {
            badRequest("'value_mask' must be 0..255");
            return;
        }
        spec.nrMask = static_cast<uint8_t>(value);
    }
    // Hits: hit_mode + hit_target (protocol), or the text form "hits": "5" | ">=5" | "%5"
    if (body.isMember("hits"))
    {
        std::string hitError;
        if (!BreakpointManager::ParseHitSpec(body["hits"].asString(), spec.hitMode, spec.hitTarget, hitError))
        {
            badRequest(hitError);
            return;
        }
    }
    if (body.isMember("hit_target"))
    {
        if (!ParseJsonUInt(body["hit_target"], 0xFFFFFFFFu, value))
        {
            badRequest("'hit_target' must be a positive number");
            return;
        }
        spec.hitTarget = value;
        spec.hitMode = BRK_HIT_EQUAL;
    }
    if (body.isMember("hit_mode") && !BreakpointManager::ParseHitModeName(body["hit_mode"].asString(), spec.hitMode))
    {
        badRequest("'hit_mode' must be always, equal, at_least or multiple");
        return;
    }
    // Condition: an expression in the Unreal language (condition_dialect "c": C precedence, decimal numbers)
    if (body.isMember("condition"))
    {
        if (!body["condition"].isString())
        {
            badRequest("'condition' must be a string");
            return;
        }
        spec.condition = body["condition"].asString();
    }
    if (body.isMember("condition_dialect") &&
        !BreakpointConditionProgram::ParseDialect(body["condition_dialect"].asString(), spec.conditionDialect))
    {
        badRequest("'condition_dialect' must be unreal or c");
        return;
    }
    if (body.isMember("note") && body["note"].isString())
        spec.note = body["note"].asString();
    if (body.isMember("group") && body["group"].isString())
        spec.group = body["group"].asString();

    std::string error;
    const uint16_t bpId = bpm->AddBreakpoint(spec, error);
    if (bpId == BRK_INVALID)
    {
        badRequest(error);
        return;
    }

    // A copy: another client may remove it meanwhile
    const BreakpointSnapshot added = bpm->GetBreakpointById(bpId);
    Json::Value ret = added ? BreakpointJson(*added) : Json::Value(Json::objectValue);
    ret["id"] = bpId;
    ret["cpu"] = bpm->Target() ? bpm->Target()->Name() : "main";
    ret["status"] = "success";
    ret["type"] = type;
    ret["message"] = "Breakpoint added";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(HttpStatusCode::k201Created);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/breakpoints/hits/reset - hit counters back to 0 (body {"id": N}: one;
/// no body or no id: all)
void EmulatorAPI::resetBreakpointHits(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                      const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    if (!bpm)
        return;
    auto json = req->getJsonObject();
    Json::Value ret;
    if (bpm && json && json->isMember("id"))
    {
        uint32_t bpId = 0;
        if (!ParseJsonUInt((*json)["id"], 0xFFFF, bpId) || !bpm->ResetHitCount(static_cast<uint16_t>(bpId)))
        {
            Json::Value error;
            error["error"] = "Not Found";
            error["message"] = "No breakpoint " + (*json)["id"].asString();
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k404NotFound);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        ret["reset"] = bpId;
    }
    else if (bpm)
    {
        bpm->ResetAllHitCounts();
        ret["reset"] = "all";
    }
    ret["status"] = "success";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief DELETE /api/v1/emulator/{id}/breakpoints
/// @brief Clear all breakpoints
void EmulatorAPI::clearBreakpoints(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    if (!bpm)
        return;
    if (bpm)
    {
        // The user's breakpoints: the BASIC typer's control points and the debugger adapters' stay with their owners
        bpm->ClearUserBreakpoints();
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "All breakpoints cleared";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief DELETE /api/v1/emulator/{id}/breakpoints/{bp_id}
/// @brief Remove specific breakpoint
void EmulatorAPI::removeBreakpoint(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id, const std::string& bpIdStr) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // A number, never an exception from a word ("DELETE /breakpoints/abc" threw std::invalid_argument)
    uint32_t parsedId = BRK_INVALID;
    if (!ParseJsonUInt(Json::Value(bpIdStr), 0xFFFF, parsedId))
        parsedId = BRK_INVALID;
    const uint16_t bpId = static_cast<uint16_t>(parsedId);
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    if (!bpm)
        return;
    bool removed = bpm ? bpm->RemoveBreakpointByID(bpId) : false;
    
    Json::Value ret;
    if (removed)
    {
        ret["status"] = "success";
        ret["message"] = "Breakpoint removed";
    }
    else
    {
        ret["status"] = "error";
        ret["message"] = "Breakpoint not found";
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(removed ? HttpStatusCode::k200OK : HttpStatusCode::k404NotFound);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT /api/v1/emulator/{id}/breakpoints/{bp_id}/enable
/// @brief Enable breakpoint
void EmulatorAPI::enableBreakpoint(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id, const std::string& bpIdStr) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // A number, never an exception from a word ("DELETE /breakpoints/abc" threw std::invalid_argument)
    uint32_t parsedId = BRK_INVALID;
    if (!ParseJsonUInt(Json::Value(bpIdStr), 0xFFFF, parsedId))
        parsedId = BRK_INVALID;
    const uint16_t bpId = static_cast<uint16_t>(parsedId);
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    if (!bpm)
        return;
    bool success = bpm ? bpm->ActivateBreakpoint(bpId) : false;
    
    Json::Value ret;
    ret["status"] = success ? "success" : "error";
    ret["message"] = success ? "Breakpoint enabled" : "Breakpoint not found";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k404NotFound);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT /api/v1/emulator/{id}/breakpoints/{bp_id}/disable
/// @brief Disable breakpoint
void EmulatorAPI::disableBreakpoint(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id, const std::string& bpIdStr) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // A number, never an exception from a word ("DELETE /breakpoints/abc" threw std::invalid_argument)
    uint32_t parsedId = BRK_INVALID;
    if (!ParseJsonUInt(Json::Value(bpIdStr), 0xFFFF, parsedId))
        parsedId = BRK_INVALID;
    const uint16_t bpId = static_cast<uint16_t>(parsedId);
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    if (!bpm)
        return;
    bool success = bpm ? bpm->DeactivateBreakpoint(bpId) : false;
    
    Json::Value ret;
    ret["status"] = success ? "success" : "error";
    ret["message"] = success ? "Breakpoint disabled" : "Breakpoint not found";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k404NotFound);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/breakpoints/status
/// @brief Get breakpoint status including last triggered breakpoint
void EmulatorAPI::getBreakpointStatus(const HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback,
                                       const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    if (!bpm)
        return;
    if (!bpm)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Breakpoint manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["is_paused"] = emulator->IsPaused();
    ret["breakpoints_count"] = static_cast<Json::UInt>(bpm->GetBreakpointsCount());

    // Last triggered breakpoint info (using centralized method)
    auto bpInfo = bpm->GetLastTriggeredBreakpointInfo();
    if (bpInfo.valid)
    {
        ret["last_triggered_id"] = bpInfo.id;
        ret["last_triggered_type"] = bpInfo.type;
        ret["last_triggered_address"] = bpInfo.address;
        ret["last_triggered_access"] = bpInfo.access;
        ret["last_triggered_active"] = bpInfo.active;
        ret["last_triggered_note"] = bpInfo.note;
        if (!bpInfo.pageKind.empty())
        {
            ret["last_triggered_page"]["kind"] = bpInfo.pageKind;
            ret["last_triggered_page"]["page"] = bpInfo.pageNumber;
        }
        ret["last_triggered_hit_count"] = static_cast<Json::UInt>(bpInfo.hitCount);
        ret["last_triggered_info"] = bpm->FormatBreakpointInfo(bpInfo.id);
        ret["paused_by_breakpoint"] = emulator->IsPaused();
    }
    else
    {
        ret["last_triggered_id"] = Json::nullValue;
        ret["last_triggered_type"] = Json::nullValue;
        ret["last_triggered_address"] = Json::nullValue;
        ret["last_triggered_access"] = Json::nullValue;
        ret["last_triggered_info"] = "";
        ret["paused_by_breakpoint"] = false;
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}


/// @brief GET /api/v1/emulator/{id}/breakpoints/active?select= - whether a selection is on (null: nothing selected)
void EmulatorAPI::getBreakpointsActive(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                       const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    BreakpointSelection selection;
    const std::string query = req->getParameter("select");
    if (!bpm || !SelectionFromJson(nullptr, query.empty() ? std::string("all") : query, selection, callback))
        return;
    ReplyJson(callback, SelectionJson(selection, *bpm), HttpStatusCode::k200OK);
}

/// @brief PUT /api/v1/emulator/{id}/breakpoints/active - {select, active: true | false | "toggle"}: switch one
/// breakpoint, all, a kind or a group on or off
void EmulatorAPI::setBreakpointsActive(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                       const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    auto json = req->getJsonObject();
    BreakpointSelection selection;
    if (!bpm || !SelectionFromJson(json.get(), {}, selection, callback))
        return;
    const Json::Value active = json ? (*json)["active"] : Json::Value();
    BreakpointSelection::Outcome outcome;
    if (active.isBool())
        outcome = selection.SetActive(*bpm, active.asBool());
    else if (active.isString() && active.asString() == "toggle")
    {
        bool on = false;
        outcome = selection.Toggle(*bpm, on);
    }
    else
    {
        ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "'active': true, false or \"toggle\"");
        return;
    }
    if (outcome.selected == 0 && selection.GetKind() == BreakpointSelection::Kind::Id)
    {
        ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "No breakpoint " + selection.Name());
        return;
    }
    Json::Value ret = SelectionJson(selection, *bpm);
    ret["changed"] = static_cast<Json::UInt64>(outcome.changed);
    ret["status"] = "success";
    ReplyJson(callback, ret, HttpStatusCode::k200OK);
}

/// @brief POST /api/v1/emulator/{id}/breakpoints/remove - {select}: remove one breakpoint, all, a kind or a group
void EmulatorAPI::removeBreakpoints(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    auto json = req->getJsonObject();
    BreakpointSelection selection;
    if (!bpm || !SelectionFromJson(json.get(), {}, selection, callback))
        return;
    const BreakpointSelection::Outcome outcome = selection.Remove(*bpm);
    if (outcome.changed == 0 && selection.GetKind() == BreakpointSelection::Kind::Id)
    {
        ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "No breakpoint " + selection.Name());
        return;
    }
    Json::Value ret;
    ret["select"] = selection.Name();
    ret["removed"] = static_cast<Json::UInt64>(outcome.changed);
    ret["status"] = "success";
    ReplyJson(callback, ret, HttpStatusCode::k200OK);
}

/// @brief PUT /api/v1/emulator/{id}/breakpoints/{bp_id}/group - {group}: move a breakpoint to a group ("" or
/// "default": back to the default group)
void EmulatorAPI::setBreakpointGroup(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id, const std::string& bpIdStr) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    if (!bpm)
        return;
    auto json = req->getJsonObject();
    uint32_t bpId = 0;
    if (!ParseJsonUInt(Json::Value(bpIdStr), 0xFFFE, bpId))
    {
        ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "The breakpoint id is a number");
        return;
    }
    if (!json || !(*json)["group"].isString())
    {
        ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "Body: {\"group\": \"name\"}");
        return;
    }
    const std::string group = (*json)["group"].asString();
    const bool ok = group.empty() || group == "default" ? bpm->RemoveBreakpointFromGroup(static_cast<uint16_t>(bpId))
                                                        : bpm->SetBreakpointGroup(static_cast<uint16_t>(bpId), group);
    if (!ok)
    {
        ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "No breakpoint " + bpIdStr);
        return;
    }
    const BreakpointSnapshot bp = bpm->GetBreakpointById(static_cast<uint16_t>(bpId));
    Json::Value ret = bp ? BreakpointJson(*bp) : Json::Value(Json::objectValue);
    ret["status"] = "success";
    ReplyJson(callback, ret, HttpStatusCode::k200OK);
}

/// @brief PUT /api/v1/emulator/{id}/breakpoints/{bp_id}/note - {note}: the breakpoint's annotation ("" clears it)
void EmulatorAPI::setBreakpointNote(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id, const std::string& bpIdStr) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    if (!bpm)
        return;
    auto json = req->getJsonObject();
    uint32_t bpId = 0;
    if (!ParseJsonUInt(Json::Value(bpIdStr), 0xFFFE, bpId))
    {
        ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "The breakpoint id is a number");
        return;
    }
    if (!json || !(*json)["note"].isString())
    {
        ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "Body: {\"note\": \"text\"}");
        return;
    }
    if (!bpm->SetBreakpointNote(static_cast<uint16_t>(bpId), (*json)["note"].asString()))
    {
        ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "No breakpoint " + bpIdStr);
        return;
    }
    const BreakpointSnapshot bp = bpm->GetBreakpointById(static_cast<uint16_t>(bpId));
    Json::Value ret = bp ? BreakpointJson(*bp) : Json::Value(Json::objectValue);
    ret["status"] = "success";
    ReplyJson(callback, ret, HttpStatusCode::k200OK);
}

/// @brief PUT /api/v1/emulator/{id}/breakpoints/{bp_id}/condition - {condition, dialect?}: the breakpoint's condition
/// ("" clears it). A text that does not compile for the breakpoint's trigger is refused with the reason and leaves the
/// old condition in place
void EmulatorAPI::setBreakpointCondition(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id,
                                         const std::string& bpIdStr) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    BreakpointManager* bpm = ManagerOrError(emulator, req, callback);
    if (!bpm)
        return;
    auto json = req->getJsonObject();
    uint32_t bpId = 0;
    if (!ParseJsonUInt(Json::Value(bpIdStr), 0xFFFE, bpId))
    {
        ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "The breakpoint id is a number");
        return;
    }
    if (!json || !(*json)["condition"].isString())
    {
        ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "Body: {\"condition\": \"expression\"}");
        return;
    }
    BreakpointConditionDialect dialect = BreakpointConditionDialect::Unreal;
    if (json->isMember("dialect") && !BreakpointConditionProgram::ParseDialect((*json)["dialect"].asString(), dialect))
    {
        ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "'dialect' must be unreal or c");
        return;
    }
    if (!bpm->GetBreakpointById(static_cast<uint16_t>(bpId)))
    {
        ReplyError(callback, HttpStatusCode::k404NotFound, "Not Found", "No breakpoint " + bpIdStr);
        return;
    }
    std::string error;
    if (!bpm->SetBreakpointCondition(static_cast<uint16_t>(bpId), (*json)["condition"].asString(), error, dialect))
    {
        ReplyError(callback, HttpStatusCode::k400BadRequest, "Bad Request", error);
        return;
    }
    const BreakpointSnapshot bp = bpm->GetBreakpointById(static_cast<uint16_t>(bpId));
    Json::Value ret = bp ? BreakpointJson(*bp) : Json::Value(Json::objectValue);
    ret["status"] = "success";
    ReplyJson(callback, ret, HttpStatusCode::k200OK);
}

// endregion Breakpoints

// region Memory Inspection

/// @brief GET /api/v1/emulator/{id}/registers
/// @brief Get CPU registers
void EmulatorAPI::getRegisters(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    Z80State* z80 = emulator->GetZ80State();
    if (!z80)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "CPU state not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // One builder for GET /registers and the snapshot's regs (core DebugSnapshot)
    const Json::Value ret = StateNodeToJson(DebugSnapshot::Registers(emulator->GetContext()));

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT /api/v1/emulator/{id}/registers/{name}
/// @brief Set a CPU register value
/// @brief Request body: {"value": 0x1234} or {"value": 4660}
void EmulatorAPI::setRegister(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id, const std::string& name) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    Z80State* z80 = emulator->GetZ80State();
    if (!z80)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "CPU state not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse request body
    auto json = req->getJsonObject();
    if (!json || !json->isMember("value"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'value' field in request body";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint32_t value = 0;
    if (!ParseJsonUInt((*json)["value"], 0xFFFF, value))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "'value' must be 0..65535 (a number, or a string: decimal, \"0x..\", \"#..\" or \"$..\")";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Use centralized register API
    const Z80::RegisterInfo* regInfo = Z80::FindRegister(name);
    if (!regInfo)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Unknown register: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string refused;
    if (!emulator->SetRegisterFromTool("webapi register", name, static_cast<uint16_t>(value), &refused))   // a tool edit
    {
        if (!refused.empty())   // the recorded past
        {
            Json::Value error;
            error["error"] = "Conflict";
            error["message"] = refused;
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k409Conflict);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = std::string(regInfo->name) + " cannot hold " + std::to_string(value) + " (at most " +
                           std::to_string(regInfo->maxValue) + ")";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Read back to confirm
    uint16_t readBack;
    bool is16bit;
    Z80::GetRegisterValue(z80, name, readBack, is16bit);

    Json::Value ret;
    ret["status"] = "success";
    ret["register"] = regInfo->name;
    ret["value"] = readBack;
    ret["is16bit"] = regInfo->is16bit;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/memory/{addr}
/// @brief Read memory at address
/// @brief Query param: len (default 128, max 4096)
void EmulatorAPI::getMemory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id, const std::string& addrStr) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    Memory* mem = emulator->GetMemory();
    if (!mem)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Parse address (supports 0x prefix, $ prefix, or decimal)
    uint16_t addr = 0;
    try
    {
        if (addrStr.substr(0, 2) == "0x" || addrStr.substr(0, 2) == "0X")
        {
            addr = static_cast<uint16_t>(std::stoul(addrStr.substr(2), nullptr, 16));
        }
        else if (addrStr[0] == '$')
        {
            addr = static_cast<uint16_t>(std::stoul(addrStr.substr(1), nullptr, 16));
        }
        else
        {
            addr = static_cast<uint16_t>(std::stoul(addrStr));
        }
    }
    catch (...)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid address format";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Get length from query param
    unsigned len = 128;
    auto lenParam = req->getParameter("len");
    if (!lenParam.empty())
    {
        len = std::stoul(lenParam);
    }
    // format=binary: the raw bytes, the whole 64K at once (the JSON formats keep their 4096 cap)
    if (req->getParameter("format") == "binary")
    {
        const MemoryRead::Result read =
            MemoryRead::Bytes(emulator->GetContext(), "cpu", addr, std::clamp<unsigned>(len, 1, MemoryRead::kMaxLength));
        if (!read.error.empty())
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = read.error;
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        callback(BinaryMemoryResponse(read.space, read.address, read.bytes));
        return;
    }
    if (len > 4096) len = 4096;
    if (len < 1) len = 1;

    // TD-3 Phase 1 compact read format: hexdump (default, ~80% token cut) |
    // full (legacy data array + hex) | sparse (fill-run segments);
    // filter=sparse is accepted as an alias
    std::string format = "hexdump";
    const std::string formatParam = req->getParameter("format");
    const std::string filterParam = req->getParameter("filter");
    if (!formatParam.empty()) format = formatParam;
    else if (filterParam == "sparse") format = "sparse";
    if (format != "hexdump" && format != "full" && format != "sparse")
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid format parameter (expected 'hexdump', 'full', 'sparse' or 'binary')";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Read memory. Direct (non-mutating) access: debugger-side reads must
    // never drive the ProfROM quadrant state machine the way CPU reads do
    // (MemoryReadFast/MemoryReadDebug strobe #0000-#0003 while the Service
    // ROM is paged) - inspecting memory must not change machine state
    std::vector<uint8_t> buffer(len);
    for (unsigned i = 0; i < len; i++)
    {
        buffer[i] = mem->DirectReadFromZ80Memory((addr + i) & 0xFFFF);
    }

    Json::Value ret;
    ret["address"] = addr;
    ret["length"] = len;
    const Json::Value payload = RenderMemoryWindowFormat(buffer.data(), buffer.size(), addr, format);
    for (const std::string& name : payload.getMemberNames())
        ret[name] = payload[name];

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT /api/v1/emulator/{id}/memory/{addr}
/// @brief Write memory at address
/// @brief Request body: {"data": [0x00, 0x01, ...]} or {"hex": "00 01 02 ..."}
void EmulatorAPI::putMemory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id, const std::string& addrStr) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    Memory* mem = emulator->GetMemory();
    if (!mem)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Parse address
    uint16_t addr = 0;
    try
    {
        if (addrStr.substr(0, 2) == "0x" || addrStr.substr(0, 2) == "0X")
            addr = static_cast<uint16_t>(std::stoul(addrStr.substr(2), nullptr, 16));
        else if (addrStr[0] == '$')
            addr = static_cast<uint16_t>(std::stoul(addrStr.substr(1), nullptr, 16));
        else
            addr = static_cast<uint16_t>(std::stoul(addrStr));
    }
    catch (...)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid address format";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto json = req->getJsonObject();
    if (!json)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Request body required";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::vector<uint8_t> bytes;
    
    // Parse data from array or hex string
    if (json->isMember("data") && (*json)["data"].isArray())
    {
        for (const auto& val : (*json)["data"])
        {
            bytes.push_back(static_cast<uint8_t>(val.asUInt()));
        }
    }
    else if (json->isMember("hex"))
    {
        std::string hexData = (*json)["hex"].asString();
        std::istringstream iss(hexData);
        std::string token;
        while (iss >> token)
        {
            bytes.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
        }
    }
    else
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Provide 'data' array or 'hex' string";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Write memory
    for (size_t i = 0; i < bytes.size(); i++)
    {
        mem->DirectWriteToZ80Memory((addr + i) & 0xFFFF, bytes[i]);
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["address"] = addr;
    ret["length"] = static_cast<unsigned>(bytes.size());
    ret["message"] = "Wrote " + std::to_string(bytes.size()) + " bytes";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/memory/{type}/{page}/{offset}
/// @brief Read from physical page
/// @brief Query param: len (default 128, max 16384)
void EmulatorAPI::getMemoryPage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id, const std::string& typeStr, 
                                const std::string& pageStr, const std::string& offsetStr) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    Memory* mem = emulator->GetMemory();
    if (!mem)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Parse type
    int pageType = -1;
    if (typeStr == "ram") pageType = 0;
    else if (typeStr == "rom") pageType = 1;
    else if (typeStr == "cache") pageType = 2;
    else if (typeStr == "misc") pageType = 3;
    
    if (pageType < 0)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid page type. Use: ram, rom, cache, misc";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Strict index validation: a plain std::stoul() here accepted "-1"
    // (wrapping to a huge value), so pagePtr[offset] read memory outside
    // the page - leaking host process bytes
    uint64_t page = 0;
    uint64_t offset = 0;
    if (!StringHelper::TryParseUInt64(pageStr, page))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid page number '" + pageStr + "' (expected unsigned decimal)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    if (!StringHelper::TryParseUInt64(offsetStr, offset) || offset >= PAGE_SIZE)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid offset '" + offsetStr + "' (expected 0-" + std::to_string(PAGE_SIZE - 1) + ")";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Get length from query param
    unsigned len = 128;
    auto lenParam = req->getParameter("len");
    if (!lenParam.empty())
    {
        uint64_t lenValue = 0;
        if (!StringHelper::TryParseUInt64(lenParam, lenValue) || lenValue < 1)
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "Invalid length '" + lenParam + "' (expected unsigned decimal >= 1)";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        len = lenValue > PAGE_SIZE ? PAGE_SIZE : static_cast<unsigned>(lenValue);
    }

    // Clamp to page boundary (offset <= PAGE_SIZE - 1 here, no underflow)
    if (offset + len > PAGE_SIZE) len = PAGE_SIZE - offset;
    
    // Get page pointer
    uint8_t* pagePtr = nullptr;
    const char* typeName = nullptr;
    
    switch (pageType)
    {
        case 0:  // RAM
            if (page >= MAX_RAM_PAGES)
            {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Invalid RAM page";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
            pagePtr = mem->RAMPageAddress(static_cast<uint16_t>(page));
            typeName = "ram";
            break;
        case 1:  // ROM
            if (page >= MAX_ROM_PAGES)
            {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Invalid ROM page";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
            pagePtr = mem->ROMPageHostAddress(static_cast<uint8_t>(page));
            typeName = "rom";
            break;
        case 2:  // Cache
            if (page >= MAX_CACHE_PAGES)
            {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Invalid cache page";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
            pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
            typeName = "cache";
            break;
        case 3:  // Misc
            if (page >= MAX_MISC_PAGES)
            {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Invalid misc page";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
            pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
            typeName = "misc";
            break;
    }
    
    if (!pagePtr)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Page not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // format=binary: the same bytes, raw (the JSON answer stays the default)
    if (req->getParameter("format") == "binary")
    {
        callback(BinaryMemoryResponse(std::string(typeName) + std::to_string(page), static_cast<uint32_t>(offset),
                                      std::vector<uint8_t>(pagePtr + offset, pagePtr + offset + len)));
        return;
    }

    // Read memory
    Json::Value ret;
    ret["type"] = typeName;
    ret["page"] = page;
    ret["offset"] = offset;
    ret["length"] = len;
    
    Json::Value data(Json::arrayValue);
    for (unsigned i = 0; i < len; i++)
    {
        data.append(pagePtr[offset + i]);
    }
    ret["data"] = data;
    
    // Hex string
    std::stringstream hexStr;
    for (unsigned i = 0; i < len; i++)
    {
        hexStr << std::hex << std::uppercase << std::setw(2) << std::setfill('0') 
               << static_cast<int>(pagePtr[offset + i]);
        if (i < len - 1) hexStr << " ";
    }
    ret["hex"] = hexStr.str();
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT /api/v1/emulator/{id}/memory/{type}/{page}/{offset}
/// @brief Write to physical page
/// @brief Request body: {"data": [0x00, 0x01, ...], "force": true} or {"hex": "00 01", "force": true}
void EmulatorAPI::putMemoryPage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id, const std::string& typeStr,
                                const std::string& pageStr, const std::string& offsetStr) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    Memory* mem = emulator->GetMemory();
    if (!mem)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Parse type
    int pageType = -1;
    if (typeStr == "ram") pageType = 0;
    else if (typeStr == "rom") pageType = 1;
    else if (typeStr == "cache") pageType = 2;
    else if (typeStr == "misc") pageType = 3;
    
    if (pageType < 0)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid page type. Use: ram, rom, cache, misc";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Strict index validation (see getMemoryPage): std::stoul() wrapped
    // "-1" into a huge value, and the checks below then misbehaved
    uint64_t page = 0;
    uint64_t offset = 0;
    if (!StringHelper::TryParseUInt64(pageStr, page))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid page number '" + pageStr + "' (expected unsigned decimal)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    if (!StringHelper::TryParseUInt64(offsetStr, offset) || offset >= PAGE_SIZE)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid offset '" + offsetStr + "' (expected 0-" + std::to_string(PAGE_SIZE - 1) + ")";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Request body required";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    bool forceFlag = json->isMember("force") && (*json)["force"].asBool();
    
    // ROM requires force
    if (pageType == 1 && !forceFlag)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "ROM write requires 'force': true";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::vector<uint8_t> bytes;
    
    // Parse data
    if (json->isMember("data") && (*json)["data"].isArray())
    {
        for (const auto& val : (*json)["data"])
        {
            bytes.push_back(static_cast<uint8_t>(val.asUInt()));
        }
    }
    else if (json->isMember("hex"))
    {
        std::string hexData = (*json)["hex"].asString();
        std::istringstream iss(hexData);
        std::string token;
        while (iss >> token)
        {
            bytes.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
        }
    }
    else
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Provide 'data' array or 'hex' string";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Get page pointer
    uint8_t* pagePtr = nullptr;
    const char* typeName = nullptr;
    
    switch (pageType)
    {
        case 0:
            if (page >= MAX_RAM_PAGES)
            {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Invalid RAM page (expected 0-" + std::to_string(MAX_RAM_PAGES - 1) + ")";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
            pagePtr = mem->RAMPageAddress(static_cast<uint16_t>(page));
            typeName = "ram";
            break;
        case 1:
            if (page >= MAX_ROM_PAGES)
            {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Invalid ROM page (expected 0-" + std::to_string(MAX_ROM_PAGES - 1) + ")";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
            pagePtr = mem->ROMPageHostAddress(static_cast<uint8_t>(page));
            typeName = "rom";
            break;
        case 2:
            if (page >= MAX_CACHE_PAGES)
            {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Invalid cache page (expected 0-" + std::to_string(MAX_CACHE_PAGES - 1) + ")";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
            pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
            typeName = "cache";
            break;
        case 3:
            if (page >= MAX_MISC_PAGES)
            {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Invalid misc page (expected 0-" + std::to_string(MAX_MISC_PAGES - 1) + ")";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
            pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
            typeName = "misc";
            break;
    }
    
    if (!pagePtr)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Page not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Check bounds
    if (offset + bytes.size() > PAGE_SIZE)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Write would exceed page boundary";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Write
    for (size_t i = 0; i < bytes.size(); i++)
    {
        pagePtr[offset + i] = bytes[i];
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["type"] = typeName;
    ret["page"] = page;
    ret["offset"] = offset;
    ret["length"] = static_cast<unsigned>(bytes.size());
    ret["message"] = "Wrote " + std::to_string(bytes.size()) + " bytes";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/memory/info
/// @brief Get memory configuration
void EmulatorAPI::getMemoryInfo(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    Memory* mem = emulator->GetMemory();
    if (!mem)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    Json::Value ret;
    
    // Page counts
    Json::Value pages;
    pages["ram"]["count"] = MAX_RAM_PAGES;
    pages["ram"]["size_kb"] = MAX_RAM_PAGES * 16;
    pages["rom"]["count"] = MAX_ROM_PAGES;
    pages["rom"]["size_kb"] = MAX_ROM_PAGES * 16;
    pages["cache"]["count"] = MAX_CACHE_PAGES;
    pages["cache"]["size_kb"] = MAX_CACHE_PAGES * 16;
    pages["misc"]["count"] = MAX_MISC_PAGES;
    pages["misc"]["size_kb"] = MAX_MISC_PAGES * 16;
    ret["pages"] = pages;
    
    // Current bank mapping
    Json::Value banks;
    for (int bank = 0; bank < 4; bank++)
    {
        Json::Value bankInfo;
        bankInfo["start_address"] = bank * 0x4000;
        bankInfo["end_address"] = (bank + 1) * 0x4000 - 1;
        bankInfo["mapping"] = mem->GetCurrentBankName(bank);
        banks["bank" + std::to_string(bank)] = bankInfo;
    }
    ret["z80_banks"] = banks;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/memory/map
/// @brief Sparse non-zero memory overview (TD-3 Phase 1): merged block map of
/// @brief the CPU address space (view=address, default) or the physical RAM
/// @brief pages (view=ram). Query params: min_run (default 64), max_blocks
/// @brief (default 48). Rendered from the single-source core helper so every
/// @brief automation surface reports identical blocks.
void EmulatorAPI::getMemoryMap(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    Memory* mem = emulator->GetMemory();
    if (!ctx || !mem)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // view=address (default, 64K CPU space) | view=ram (physical RAM pages)
    MemoryMapView view = MemoryMapView::AddressSpace;
    const std::string viewParam = req->getParameter("view");
    if (viewParam == "ram" || viewParam == "pages")
    {
        view = MemoryMapView::RamPages;
    }
    else if (!viewParam.empty() && viewParam != "address" && viewParam != "cpu")
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid view parameter (expected 'address' or 'ram')";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint32_t minRun = kMemoryMapDefaultMinRun;
    uint32_t maxBlocks = kMemoryMapDefaultMaxBlocks;
    try
    {
        const std::string minRunParam = req->getParameter("min_run");
        if (!minRunParam.empty())
            minRun = std::stoul(minRunParam);
        const std::string maxBlocksParam = req->getParameter("max_blocks");
        if (!maxBlocksParam.empty())
            maxBlocks = std::stoul(maxBlocksParam);
    }
    catch (...)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid min_run / max_blocks parameter";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    // Clamp instead of rejecting: agents experiment with granularity values
    if (minRun < 1) minRun = 1;
    if (minRun > PAGE_SIZE) minRun = PAGE_SIZE;
    if (maxBlocks < 1) maxBlocks = 1;
    if (maxBlocks > 4096) maxBlocks = 4096;

    const MemoryMapReport report = BuildMemoryMap(*mem, ctx->config, view, minRun, maxBlocks);

    Json::Value ret;
    ret["model"] = report.model;
    ret["view"] = report.ramView ? "ram" : "address";
    ret["total_size"] = report.totalSize;
    ret["non_zero_bytes"] = report.nonZeroBytes;
    ret["min_run"] = report.minRun;
    ret["block_count"] = static_cast<unsigned>(report.blocks.size());
    ret["truncated"] = report.truncated;

    Json::Value blocks(Json::arrayValue);
    for (const MemoryMapBlock& block : report.blocks)
    {
        char address[12];
        std::snprintf(address, sizeof(address), "0x%04X", block.address);

        Json::Value item;
        item["address"] = address;
        item["size"] = block.size;
        item["type"] = block.typeName;
        item["bank"] = block.bank == 0xFF ? -1 : static_cast<int>(block.bank);
        item["page"] = block.page;
        item["rom"] = block.isRom;
        item["status"] = block.IsZeroFill() ? "zeros" : "data";
        item["non_zero"] = block.nonZero;
        if (!block.IsZeroFill())
        {
            // FNV-1a 64 fingerprint (not sha256): cheap, stable, lets an agent
            // diff a region across seeks without dumping its bytes
            char hash[20];
            std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(block.hash));
            item["hash"] = hash;
        }
        blocks.append(item);
    }
    ret["blocks"] = blocks;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// endregion Memory Inspection

// region Analysis

/// @brief GET /api/v1/emulator/{id}/memcounters
/// @brief Get memory access statistics
void EmulatorAPI::getMemCounters(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    auto* ctx = emulator->GetContext();
    if (!ctx)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Emulator context not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Use memory->GetAccessTracker() API
    Memory* memory = ctx->pMemory;
    if (!memory)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    MemoryAccessTracker& tracker = memory->GetAccessTracker();
    
    // Get counters by summing Z80 banks
    uint64_t totalReads = 0;
    uint64_t totalWrites = 0;
    uint64_t totalExecutes = 0;
    
    Json::Value banks(Json::arrayValue);
    for (int bank = 0; bank < 4; bank++)
    {
        uint64_t reads = tracker.GetZ80BankReadAccessCount(bank);
        uint64_t writes = tracker.GetZ80BankWriteAccessCount(bank);
        uint64_t executes = tracker.GetZ80BankExecuteAccessCount(bank);
        
        totalReads += reads;
        totalWrites += writes;
        totalExecutes += executes;
        
        Json::Value bankInfo;
        bankInfo["bank"] = bank;
        bankInfo["reads"] = static_cast<Json::UInt64>(reads);
        bankInfo["writes"] = static_cast<Json::UInt64>(writes);
        bankInfo["executes"] = static_cast<Json::UInt64>(executes);
        bankInfo["total"] = static_cast<Json::UInt64>(reads + writes + executes);
        banks.append(bankInfo);
    }
    
    Json::Value ret;
    ret["total_reads"] = static_cast<Json::UInt64>(totalReads);
    ret["total_writes"] = static_cast<Json::UInt64>(totalWrites);
    ret["total_executes"] = static_cast<Json::UInt64>(totalExecutes);
    ret["total_accesses"] = static_cast<Json::UInt64>(totalReads + totalWrites + totalExecutes);
    ret["banks"] = banks;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/calltrace
/// @brief Get call trace history
/// @brief Query param: limit (default 50, max 1000)
void EmulatorAPI::getCallTrace(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    FeatureManager* fm = emulator->GetFeatureManager();
    bool calltraceEnabled = fm ? fm->isEnabled("calltrace") : false;
    
    auto* ctx = emulator->GetContext();
    
    Json::Value ret;
    ret["calltrace_enabled"] = calltraceEnabled;
    
    if (calltraceEnabled && ctx && ctx->pDebugManager)
    {
        // Get limit from query param
        unsigned limit = 50;
        auto limitParam = req->getParameter("limit");
        if (!limitParam.empty())
        {
            limit = std::stoul(limitParam);
        }
        if (limit > 1000) limit = 1000;
        
        ret["limit"] = limit;

        // The same buffer GET /profiler/calltrace/entries reads; it fills while a calltrace
        // profiler session is capturing (POST /profiler/calltrace/start)
        Json::Value entriesJson(Json::arrayValue);
        auto* memory = ctx->pMemory;
        auto* calltraceBuffer = memory ? memory->GetAccessTracker().GetCallTraceBuffer() : nullptr;
        if (calltraceBuffer)
        {
            for (const auto& entry : calltraceBuffer->GetRecentEntries(limit))
            {
                Json::Value e;
                e["type"] = static_cast<int>(entry.type);
                e["from_address"] = entry.m1_pc;
                e["to_address"] = entry.target_addr;
                e["sp"] = entry.sp;
                e["loop_count"] = entry.loop_count;
                entriesJson.append(e);
            }
            ret["total_count"] = static_cast<Json::UInt>(calltraceBuffer->GetCount());
        }
        ret["message"] = "Call trace active; entries are filled while a calltrace profiler session is capturing";
        ret["entries"] = entriesJson;
    }
    else
    {
        ret["message"] = "Call trace disabled. Enable with 'feature calltrace on'";
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// endregion Analysis

// region Disassembly

/// @brief GET /api/v1/emulator/{id}/disasm
/// @brief Disassemble Z80 code
/// @brief Query params: address (default: PC; decimal, 0x / $ / # prefixed or h suffixed hex), count (default: 10, max: 256);
/// @brief a bad address or count is a 400
/// @brief GET /api/v1/emulator/{id}/debug/snapshot - one coherent picture for a debugger front end
/// @brief Query: disasm (lines from PC, default 0, max 100), stack (words from SP, default 8, max 128),
/// @brief memory (<space>:<addr>:<len>, repeatable or comma-separated, at most 8 windows)
void EmulatorAPI::getDebugSnapshot(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto reply = [&callback](HttpStatusCode code, const std::string& message) {
        Json::Value error;
        error["error"] = code == HttpStatusCode::k503ServiceUnavailable ? "Service Unavailable" : "Bad Request";
        error["message"] = message;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(code);
        addCorsHeaders(resp);
        callback(resp);
    };
    DebugSnapshot::Options options;
    auto number = [&](const char* key, unsigned& out) {
        const std::string text = req->getParameter(key);
        if (text.empty())
            return true;
        uint64_t value = 0;
        if (!StringHelper::TryParseUInt64(text, value) || value > 0xFFFF)
            return false;
        out = static_cast<unsigned>(value);
        return true;
    };
    if (!number("disasm", options.disasm) || !number("stack", options.stack) || !number("pchist", options.pchist))
        return reply(HttpStatusCode::k400BadRequest, "disasm, stack and pchist are unsigned numbers");
    // memory may repeat (memory=a&memory=b) and take a comma list: read the raw query, not the parameter map
    const std::string& query = req->query();
    size_t at = 0;
    while (at <= query.size())
    {
        const size_t end = std::min(query.find('&', at), query.size());
        const std::string pair = query.substr(at, end - at);
        if (pair.rfind("memory=", 0) == 0)
        {
            const std::string value = drogon::utils::urlDecode(pair.substr(7));
            size_t from = 0;
            while (from <= value.size())
            {
                const size_t comma = std::min(value.find(',', from), value.size());
                if (comma > from)
                    options.memory.push_back(value.substr(from, comma - from));
                from = comma + 1;
            }
        }
        at = end + 1;
    }
    options.disasm = std::min(options.disasm, 100u);   // the snapshot keeps its own 100-line cap

    const DebugSnapshot::Result result = DebugSnapshot::Build(emulator.get(), options);
    if (!result.error.empty())
        return reply(result.busy ? HttpStatusCode::k503ServiceUnavailable : HttpStatusCode::k400BadRequest, result.error);
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(result.snapshot));
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/ports/out {"port": "0x13AF", "value": "0x20"}
/// A debugger's port write through the machine's decoder (PortWrite): the side effects of a CPU OUT, no breakpoint,
/// no device waits, a tool edit for TTD. Port and value: a JSON number or text (0x13AF, #13AF, 13AFh, decimal)
void EmulatorAPI::postPortOut(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto reply = [&callback](HttpStatusCode code, const std::string& message) {
        Json::Value error;
        error["error"] = code == HttpStatusCode::k503ServiceUnavailable ? "Service Unavailable" : "Bad Request";
        error["message"] = message;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(code);
        addCorsHeaders(resp);
        callback(resp);
    };
    const auto json = req->getJsonObject();
    if (!json || !json->isMember("port") || !json->isMember("value"))
        return reply(HttpStatusCode::k400BadRequest, "body must be JSON with 'port' and 'value'");
    // A JSON number goes in as decimal text: one parser for every surface
    auto text = [](const Json::Value& value) {
        if (value.isIntegral() && !value.isBool())
            return value.isInt64() && value.asInt64() < 0 ? std::string("-") : std::to_string(value.asUInt64());
        return value.isString() ? value.asString() : std::string();
    };
    uint16_t port = 0;
    uint8_t value = 0;
    std::string error;
    if (!PortWrite::Parse(text((*json)["port"]), text((*json)["value"]), port, value, error))
        return reply(HttpStatusCode::k400BadRequest, error);

    const PortWrite::Result result = PortWrite::Write(emulator.get(), port, value, "webapi");
    if (!result.ok)
        return reply(result.busy ? HttpStatusCode::k503ServiceUnavailable : HttpStatusCode::k400BadRequest,
                     result.error);
    char portHex[8];
    char valueHex[8];
    std::snprintf(portHex, sizeof(portHex), "0x%04X", port);
    std::snprintf(valueHex, sizeof(valueHex), "0x%02X", value);
    Json::Value body;
    body["port"] = portHex;
    body["value"] = valueHex;
    body["moment"] = result.moment;
    auto resp = HttpResponse::newHttpJsonResponse(body);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/debug/wait?since=N&timeout_ms=M - long-poll (debugger additions tdd §6): the
/// answer comes when the snapshot's seq moves past `since` (default: the current seq) or after timeout_ms (default
/// 10000, at most 60000): {seq, changed, state, pause}. No thread waits: a 10 ms timer on this worker's event loop
/// checks seq, so many clients can wait at once
void EmulatorAPI::getDebugWait(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto number = [&](const char* key, uint64_t fallback, uint64_t max, uint64_t& out) {
        const std::string text = req->getParameter(key);
        out = fallback;
        return text.empty() || (StringHelper::TryParseUInt64(text, out) && out <= max);
    };
    uint64_t since = 0;
    uint64_t timeoutMs = 0;
    if (!number("since", emulator->DebugSeq(), UINT64_MAX, since) || !number("timeout_ms", 10000, 60000, timeoutMs))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "since is an unsigned number, timeout_ms 0..60000";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    auto answer = [since](const std::shared_ptr<Emulator>& target, std::function<void(const HttpResponsePtr&)>& reply) {
        auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(DebugSnapshot::WaitAnswer(target.get(), since)));
        addCorsHeaders(resp);
        reply(resp);
    };
    if (emulator->DebugSeq() != since || timeoutMs == 0)
        return answer(emulator, callback);

    struct Waiter
    {
        std::function<void(const HttpResponsePtr&)> callback;
        std::weak_ptr<Emulator> emulator;
        uint64_t since = 0;
        std::chrono::steady_clock::time_point deadline;
        trantor::TimerId timer = 0;
        bool done = false;
    };
    auto waiter = std::make_shared<Waiter>();
    waiter->callback = std::move(callback);
    waiter->emulator = emulator;
    waiter->since = since;
    waiter->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    trantor::EventLoop* loop = trantor::EventLoop::getEventLoopOfCurrentThread();
    if (!loop)
        loop = drogon::app().getLoop();
    waiter->timer = loop->runEvery(0.01, [waiter, loop, answer]() {
        if (waiter->done)
            return;
        std::shared_ptr<Emulator> target = waiter->emulator.lock();
        if (target && target->DebugSeq() == waiter->since && std::chrono::steady_clock::now() < waiter->deadline)
            return;
        waiter->done = true;
        loop->invalidateTimer(waiter->timer);
        if (target)
            return answer(target, waiter->callback);
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "the emulator was removed while waiting";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        waiter->callback(resp);
    });
}

/// @brief GET /api/v1/emulator/{id}/debug/pchist?depth=32 - the PC history, newest first (debugger additions tdd §7):
/// {armed, started_now, total, capacity, entries [{address, kind, page}]}. The first read arms it (entries from then
/// on); recording costs the emulation nothing until then
void EmulatorAPI::getPcHistory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    uint64_t depth = 32;
    const std::string text = req->getParameter("depth");
    auto reply = [&callback](HttpStatusCode code, const std::string& message) {
        Json::Value error;
        error["error"] = code == HttpStatusCode::k503ServiceUnavailable ? "Service Unavailable" : "Bad Request";
        error["message"] = message;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(code);
        addCorsHeaders(resp);
        callback(resp);
    };
    if (!text.empty() && (!StringHelper::TryParseUInt64(text, depth) || depth > PcHistory::kCapacity))
        return reply(HttpStatusCode::k400BadRequest, "depth is 0.." + std::to_string(PcHistory::kCapacity));
    const PcHistory::Result result = PcHistory::Report(emulator.get(), static_cast<size_t>(depth));
    if (!result.error.empty())
        return reply(result.busy ? HttpStatusCode::k503ServiceUnavailable : HttpStatusCode::k400BadRequest, result.error);
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(result.report));
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/debug/pchist {"enabled": true | false} - start (empty) or stop the PC history
void EmulatorAPI::postPcHistory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    const auto json = req->getJsonObject();
    Json::Value body;
    HttpStatusCode code = HttpStatusCode::k200OK;
    if (!json || !json->isMember("enabled") || !(*json)["enabled"].isBool())
    {
        code = HttpStatusCode::k400BadRequest;
        body["error"] = "Bad Request";
        body["message"] = "body must be {\"enabled\": true | false}";
    }
    else
    {
        const bool on = (*json)["enabled"].asBool();
        const std::string error = PcHistory::SetArmed(emulator.get(), on);
        if (error.empty())
            body["armed"] = on;
        else
        {
            code = HttpStatusCode::k503ServiceUnavailable;
            body["error"] = "Service Unavailable";
            body["message"] = error;
        }
    }
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

void EmulatorAPI::getDisasm(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    EmulatorContext* ctx = emulator->GetContext();
    Z80* z80 = ctx->pCore->GetZ80();
    DebugManager* dbg = ctx->pDebugManager;
    
    if (!dbg || !dbg->GetDisassembler())
    {
        Json::Value error;
        error["error"] = "Disassembler not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    
    // Parse query parameters strictly: a bad address used to fall back to PC silently and a bad count threw.
    // Address: decimal, 0x / $ / # prefixed or h suffixed hex
    std::string addrParam = req->getParameter("address");
    std::string countParam = req->getParameter("count");

    uint16_t address = z80->pc;  // Default to PC
    size_t count = 10;           // Default count
    auto badRequest = [&callback](const std::string& message) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = message;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    };

    if (!addrParam.empty())
    {
        std::string digits = addrParam;
        int base = 10;
        if (digits.size() > 2 && (digits.compare(0, 2, "0x") == 0 || digits.compare(0, 2, "0X") == 0))
        {
            digits = digits.substr(2);
            base = 16;
        }
        else if (digits.size() > 1 && (digits[0] == '$' || digits[0] == '#'))
        {
            digits = digits.substr(1);
            base = 16;
        }
        else if (digits.size() > 1 && (digits.back() == 'h' || digits.back() == 'H'))
        {
            digits.pop_back();
            base = 16;
        }
        uint64_t value = 0;
        if (!StringHelper::TryParseUInt64(digits, value, base) || value > 0xFFFF)
            return badRequest("address must be 0-65535: decimal, 0x / $ / # prefixed or h suffixed hex");
        address = static_cast<uint16_t>(value);
    }

    if (!countParam.empty())
    {
        uint64_t value = 0;
        if (!StringHelper::TryParseUInt64(countParam, value) || value < 1 || value > DebugSnapshot::kMaxDisasmLines)
            return badRequest("count must be 1-" + std::to_string(DebugSnapshot::kMaxDisasmLines));
        count = static_cast<size_t>(value);
    }

    // One builder for GET /disasm and the snapshot's disasm (core DebugSnapshot)
    const Json::Value ret = StateNodeToJson(DebugSnapshot::Disasm(ctx, address, count));

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// endregion Disassembly

/// @brief GET /api/v1/emulator/{id}/disasm/page
/// @brief Disassemble from physical RAM/ROM page (bypasses Z80 paging)
/// @brief Query params: type (ram|rom), page (0-255), offset (0-16383), count (default: 10, max: 100)
void EmulatorAPI::getDisasmPage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    EmulatorContext* ctx = emulator->GetContext();
    Memory* memory = ctx->pMemory;
    DebugManager* dbg = ctx->pDebugManager;
    
    if (!dbg || !dbg->GetDisassembler())
    {
        Json::Value error;
        error["error"] = "Disassembler not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    Z80Disassembler* disasm = dbg->GetDisassembler().get();
    LabelManager* labelMgr = dbg->GetLabelManager();
    
    // Parse query parameters
    std::string typeParam = req->getParameter("type");
    std::string pageParam = req->getParameter("page");
    std::string offsetParam = req->getParameter("offset");
    std::string countParam = req->getParameter("count");
    
    bool isROM = (typeParam == "rom");
    if (typeParam != "rom" && typeParam != "ram")
    {
        Json::Value error;
        error["error"] = "Invalid type parameter. Use 'ram' or 'rom'";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    uint8_t page = 0;
    uint16_t offset = 0;
    size_t count = 10;

    // Strict parse: std::stoul silently truncated page "300" to 44 and "-1"
    // to 255, and count was parsed outside the try block at all
    uint64_t pageValue = 0;
    uint64_t offsetValue = 0;
    const uint64_t maxDisasmPage = isROM ? MAX_ROM_PAGES - 1 : MAX_RAM_PAGES - 1;
    bool indexOk = StringHelper::TryParseUInt64(pageParam, pageValue) && pageValue <= maxDisasmPage;
    if (indexOk && !offsetParam.empty())
    {
        if (offsetParam.find("0x") == 0 || offsetParam.find("0X") == 0)
            indexOk = StringHelper::TryParseUInt64(offsetParam, offsetValue, 16);
        else
            indexOk = StringHelper::TryParseUInt64(offsetParam, offsetValue);
        indexOk = indexOk && offsetValue < PAGE_SIZE;
    }
    if (!indexOk)
    {
        Json::Value error;
        error["error"] = "Invalid page or offset parameter";
        error["message"] = "Page must be 0-" + std::to_string(maxDisasmPage) + ", offset 0-" +
                           std::to_string(PAGE_SIZE - 1) + " (decimal or 0x-prefixed hex)";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    page = static_cast<uint8_t>(pageValue);
    offset = static_cast<uint16_t>(offsetValue);

    if (!countParam.empty()) {
        uint64_t countValue = 0;
        if (!StringHelper::TryParseUInt64(countParam, countValue))
        {
            Json::Value error;
            error["error"] = "Invalid count parameter";
            error["message"] = "Count must be an unsigned decimal number";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        count = countValue > 100 ? 100 : static_cast<size_t>(countValue);
        if (count < 1) count = 1;
    }
    
    // Get physical memory base for the page
    uint8_t* pageBase = isROM ? memory->ROMPageHostAddress(page) : memory->RAMPageAddress(page);
    if (!pageBase)
    {
        Json::Value error;
        error["error"] = "Invalid page number";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    Json::Value ret;
    ret["type"] = typeParam;
    ret["page"] = page;
    ret["offset"] = offset;
    ret["count"] = static_cast<unsigned int>(count);
    ret["instructions"] = Json::arrayValue;
    
    uint16_t currentOffset = offset;
    for (size_t i = 0; i < count && currentOffset < PAGE_SIZE; ++i)
    {
        // Read up to 4 bytes from physical page (Z80 instructions are max 4 bytes)
        // Pre-size to avoid GCC 16 false-positive -Wstringop-overflow warning
        std::vector<uint8_t> buffer(4, 0);
        for (int j = 0; j < 4 && (currentOffset + j) < PAGE_SIZE; ++j)
        {
            buffer[j] = pageBase[currentOffset + j];
        }
        
        uint8_t cmdLen = 0;
        DecodedInstruction decoded;
        std::string mnemonic = disasm->disassembleSingleCommand(buffer, currentOffset, &cmdLen, &decoded);
        if (cmdLen == 0) cmdLen = 1;
        
        Json::Value instr;
        instr["offset"] = currentOffset;
        
        std::string hexBytes;
        for (uint8_t j = 0; j < cmdLen; ++j) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X", buffer[j]);
            hexBytes += buf;
        }
        instr["bytes"] = hexBytes;
        instr["mnemonic"] = mnemonic;
        instr["size"] = cmdLen;
        
        // Label at the instruction offset itself (e.g. jump destination marker)
        if (labelMgr)
        {
            auto label = labelMgr->GetLabelByZ80Address(currentOffset);
            if (label && !label->name.empty())
                instr["label"] = label->name;
        }
        
        // Target address for jumps/calls. Static view has no runtime registers, so indirect
        // targets (JP (HL), JP (IX)) can not be resolved and the field is omitted
        if (decoded.hasJump || decoded.hasRelativeJump) {
            uint16_t target = decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr;
            if (!decoded.hasIndirect)
            {
                instr["target"] = target;
                
                if (labelMgr)
                {
                    auto targetLabel = labelMgr->GetLabelByZ80Address(target);
                    if (targetLabel && !targetLabel->name.empty())
                        instr["targetLabel"] = targetLabel->name;
                }
            }
        }
        
        ret["instructions"].append(instr);
        currentOffset += cmdLen;
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// region Labels/Symbols

/// @brief GET /api/v1/emulator/{id}/labels
/// @brief List labels with optional filtering via query params: module, bank, type, from, to, active
void EmulatorAPI::getLabels(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    if (!labelMgr)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Label manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Build filter from query params
    LabelManager::LabelFilter filter;
    auto params = req->getParameters();

    if (params.count("module"))
        filter.module = params.at("module");
    if (params.count("type"))
        filter.type = params.at("type");
    if (params.count("bank"))
        filter.bank = static_cast<uint16_t>(std::stoul(params.at("bank")));
    if (params.count("from"))
        filter.addressFrom = static_cast<uint16_t>(std::stoul(params.at("from"), nullptr, 0));
    if (params.count("to"))
        filter.addressTo = static_cast<uint16_t>(std::stoul(params.at("to"), nullptr, 0));
    if (params.count("active") && params.at("active") == "true")
        filter.activeOnly = true;

    auto labels = labelMgr->GetLabels(filter);

    Json::Value ret;
    Json::Value labelsArray(Json::arrayValue);

    for (const auto& label : labels)
    {
        Json::Value obj;
        obj["name"] = label->name;
        obj["address"] = label->address;
        if (label->bank != UINT16_MAX)
        {
            obj["bank"] = label->bank;
            obj["bankType"] = label->isROM() ? "rom" : "ram";
        }
        if (!label->type.empty())
            obj["type"] = label->type;
        if (!label->module.empty())
            obj["module"] = label->module;
        if (!label->comment.empty())
            obj["comment"] = label->comment;
        obj["active"] = label->active;
        labelsArray.append(obj);
    }

    ret["count"] = static_cast<unsigned>(labels.size());
    ret["total"] = static_cast<unsigned>(labelMgr->GetLabelCount());
    ret["labels"] = labelsArray;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/labels
/// @brief Add a label. Body: {name, address, bank?, bankType?, type?, module?, comment?}
void EmulatorAPI::addLabel(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    if (!labelMgr)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Label manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("name") || !json->isMember("address"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Required: name, address";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string name = (*json)["name"].asString();
    uint32_t address = 0;
    if (!ParseJsonUInt((*json)["address"], 0xFFFF, address))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "'address' must be 0..65535 (a number, or a string: decimal, \"0x..\", \"#..\" or \"$..\")";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    uint16_t bank = json->isMember("bank") ? static_cast<uint16_t>((*json)["bank"].asUInt()) : UINT16_MAX;
    uint16_t bankOffset = UINT16_MAX;
    std::string type = json->isMember("type") ? (*json)["type"].asString() : "";
    std::string module = json->isMember("module") ? (*json)["module"].asString() : "";
    std::string comment = json->isMember("comment") ? (*json)["comment"].asString() : "";

    if (labelMgr->AddLabel(name, address, bank, bankOffset, type, module, comment))
    {
        Json::Value ret;
        ret["status"] = "success";
        ret["name"] = name;
        ret["address"] = address;
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    else
    {
        Json::Value error;
        error["error"] = "Conflict";
        error["message"] = "Label already exists or invalid parameters";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k409Conflict);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief GET /api/v1/emulator/{id}/labels/{name}
void EmulatorAPI::getLabel(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id, const std::string& name) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    auto label = labelMgr ? labelMgr->GetLabelByName(name) : nullptr;

    if (!label)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Label not found: " + name;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["name"] = label->name;
    ret["address"] = label->address;
    if (label->bank != UINT16_MAX)
    {
        ret["bank"] = label->bank;
        ret["bankType"] = label->isROM() ? "rom" : "ram";
    }
    if (!label->type.empty())
        ret["type"] = label->type;
    if (!label->module.empty())
        ret["module"] = label->module;
    if (!label->comment.empty())
        ret["comment"] = label->comment;
    ret["active"] = label->active;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief DELETE /api/v1/emulator/{id}/labels/{name}
void EmulatorAPI::removeLabel(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id, const std::string& name) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    if (labelMgr && labelMgr->RemoveLabel(name))
    {
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Label removed: " + name;
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    else
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Label not found: " + name;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief PUT /api/v1/emulator/{id}/labels/{name}
/// @brief Update label properties (active, comment, type, module)
void EmulatorAPI::updateLabel(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id, const std::string& name) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    auto label = labelMgr ? labelMgr->GetLabelByName(name) : nullptr;

    if (!label)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Label not found: " + name;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (json)
    {
        if (json->isMember("active"))
            label->active = (*json)["active"].asBool();
        if (json->isMember("comment"))
            label->comment = (*json)["comment"].asString();
        if (json->isMember("type"))
            label->type = (*json)["type"].asString();
        if (json->isMember("module"))
            label->module = (*json)["module"].asString();
    }

    Json::Value ret;
    ret["status"] = "success";
    ret["name"] = label->name;
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief DELETE /api/v1/emulator/{id}/labels
void EmulatorAPI::clearLabels(const HttpRequestPtr& /*req*/, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    if (labelMgr)
        labelMgr->ClearAllLabels();

    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "All labels cleared";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/symbols/load
/// @brief Load symbols from file. Body: {path: "symbols.sld"}
void EmulatorAPI::loadSymbols(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    if (!labelMgr)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Label manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("path"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Required: path";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string path = (*json)["path"].asString();
    if (labelMgr->LoadLabels(path))
    {
        Json::Value ret;
        ret["status"] = "success";
        ret["count"] = static_cast<unsigned>(labelMgr->GetLabelCount());
        ret["path"] = path;
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    else
    {
        Json::Value error;
        error["error"] = "Failed";
        error["message"] = "Failed to load symbols from: " + path;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/symbols/save
/// @brief Save symbols to file. Body: {path: "symbols.sld"}
void EmulatorAPI::saveSymbols(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    if (!labelMgr)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Label manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("path"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Required: path";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string path = (*json)["path"].asString();
    if (labelMgr->SaveLabels(path))
    {
        Json::Value ret;
        ret["status"] = "success";
        ret["count"] = static_cast<unsigned>(labelMgr->GetLabelCount());
        ret["path"] = path;
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    else
    {
        Json::Value error;
        error["error"] = "Failed";
        error["message"] = "Failed to save symbols to: " + path;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// Helper: serialize a Label to JSON (same field shape as GET /labels/{name})
static Json::Value labelToJson(const Label& label)
{
    Json::Value json;
    json["name"] = label.name;
    json["address"] = label.address;
    if (label.bank != UINT16_MAX)
    {
        json["bank"] = label.bank;
        json["bankType"] = label.isROM() ? "rom" : "ram";
    }
    if (!label.type.empty())
        json["type"] = label.type;
    if (!label.module.empty())
        json["module"] = label.module;
    if (!label.comment.empty())
        json["comment"] = label.comment;
    json["active"] = label.active;
    return json;
}

/// @brief GET /api/v1/emulator/{id}/labels/resolve?name=LABEL or ?address=0x8000
/// @brief Resolve a label by name (exact) or by Z80 address (exact + aliases + nearest context).
/// @brief Address queries always return 200: found=false plus nearest_below/nearest_above context
void EmulatorAPI::resolveLabel(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
    if (!labelMgr)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Label manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string name = req->getParameter("name");
    std::string addressStr = req->getParameter("address");

    // Exactly one of name / address is required
    if (name.empty() == addressStr.empty())
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Provide exactly one query parameter: name or address";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Resolve by name — exact match only
    if (!name.empty())
    {
        auto label = labelMgr->GetLabelByName(name);
        if (!label)
        {
            Json::Value error;
            error["error"] = "Not Found";
            error["message"] = "Label not found: " + name;
            if (labelMgr->GetLabelCount() == 0)
                error["hint"] = "No labels loaded - POST /api/v1/emulator/{id}/symbols/load first";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k404NotFound);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        Json::Value ret;
        ret["query"] = "name";
        ret["name"] = name;
        ret["found"] = true;
        ret["label"] = labelToJson(*label);
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Resolve by address (supports 0x prefix, $ prefix, or decimal)
    uint16_t address = 0;
    try
    {
        if (addressStr.substr(0, 2) == "0x" || addressStr.substr(0, 2) == "0X")
        {
            address = static_cast<uint16_t>(std::stoul(addressStr.substr(2), nullptr, 16));
        }
        else if (addressStr[0] == '$')
        {
            address = static_cast<uint16_t>(std::stoul(addressStr.substr(1), nullptr, 16));
        }
        else
        {
            address = static_cast<uint16_t>(std::stoul(addressStr));
        }
    }
    catch (...)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid address format";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["query"] = "address";
    ret["address"] = address;
    char hexStr[8];
    snprintf(hexStr, sizeof(hexStr), "0x%04X", address);
    ret["address_hex"] = hexStr;

    auto exact = labelMgr->GetLabelByZ80Address(address);
    ret["found"] = exact != nullptr;
    if (exact)
        ret["label"] = labelToJson(*exact);

    // Aliases — all labels sharing the address
    auto atAddress = labelMgr->GetAllLabelsAtAddress(address);
    if (!atAddress.empty())
    {
        Json::Value aliases(Json::arrayValue);
        for (const auto& l : atAddress)
            aliases.append(labelToJson(*l));
        ret["all_at_address"] = aliases;
    }

    // Nearest labels around the address — context for disassembly annotation
    const Label* bestBelow = nullptr;
    const Label* bestAbove = nullptr;
    for (const auto& l : labelMgr->GetAllLabels())
    {
        if (l->address < address && (!bestBelow || l->address > bestBelow->address))
            bestBelow = l.get();
        else if (l->address > address && (!bestAbove || l->address < bestAbove->address))
            bestAbove = l.get();
    }
    if (bestBelow)
    {
        Json::Value below;
        below["name"] = bestBelow->name;
        below["address"] = bestBelow->address;
        below["distance"] = address - bestBelow->address;
        ret["nearest_below"] = below;
    }
    if (bestAbove)
    {
        Json::Value above;
        above["name"] = bestAbove->name;
        above["address"] = bestAbove->address;
        above["distance"] = bestAbove->address - address;
        ret["nearest_above"] = above;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// Helper: serialize a ListingLine to JSON
static Json::Value listingLineToJson(const ListingLine& line)
{
    Json::Value json;
    json["line"] = line.lineNumber;
    json["has_code"] = line.hasCode;
    if (line.hasCode)
    {
        json["address"] = line.addressStart;
        json["address_end"] = line.addressEnd;
        json["size"] = static_cast<Json::UInt64>(line.bytes.size());
    }
    json["source"] = line.source;
    return json;
}

// endregion Labels/Symbols

// region Source Listing

/// @brief POST /api/v1/emulator/{id}/listing/load
/// @brief Load a sjasmplus .lst listing. Body: {path: "game.lst", clear?: true}
void EmulatorAPI::loadListing(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ListingParser* parser = ctx->pDebugManager->GetListingParser();
    if (!parser)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Listing parser not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("path"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Required: path";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string path = (*json)["path"].asString();
    if (parser->LoadListing(path))
    {
        Json::Value ret;
        ret["status"] = "success";
        ret["path"] = path;
        ret["lines"] = static_cast<Json::UInt64>(parser->GetLineCount());
        ret["code_lines"] = static_cast<Json::UInt64>(parser->GetCodeLineCount());
        ret["total_bytes"] = static_cast<Json::UInt64>(parser->GetTotalBytes());
        ret["min_address"] = parser->GetMinAddress();
        ret["max_address"] = parser->GetMaxAddress();
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    else
    {
        Json::Value error;
        error["error"] = "Failed";
        error["message"] = "Failed to load listing from: " + path;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief GET /api/v1/emulator/{id}/listing/source_at?address=0x8000&context=5
/// @brief Source line covering an address, with N lines of context above/below
void EmulatorAPI::listingSourceAt(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto* ctx = emulator->GetContext();
    if (!ctx || !ctx->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ListingParser* parser = ctx->pDebugManager->GetListingParser();
    if (!parser)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Listing parser not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (!parser->IsLoaded())
    {
        Json::Value error;
        error["error"] = "Conflict";
        error["message"] = "No listing loaded";
        error["hint"] = "POST /api/v1/emulator/{id}/listing/load first";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k409Conflict);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse address (supports 0x prefix, $ prefix, or decimal)
    std::string addressStr = req->getParameter("address");
    if (addressStr.empty())
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Required: address query parameter";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    uint16_t address = 0;
    try
    {
        if (addressStr.substr(0, 2) == "0x" || addressStr.substr(0, 2) == "0X")
        {
            address = static_cast<uint16_t>(std::stoul(addressStr.substr(2), nullptr, 16));
        }
        else if (addressStr[0] == '$')
        {
            address = static_cast<uint16_t>(std::stoul(addressStr.substr(1), nullptr, 16));
        }
        else
        {
            address = static_cast<uint16_t>(std::stoul(addressStr));
        }
    }
    catch (...)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid address format";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const ListingLine* line = parser->FindLineByAddress(address);
    if (!line)
    {
        char rangeHint[64];
        snprintf(rangeHint, sizeof(rangeHint), "Covered code range: 0x%04X-0x%04X", parser->GetMinAddress(), parser->GetMaxAddress());

        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Address not covered by loaded listing";
        error["hint"] = rangeHint;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Context lines around the hit
    int context = 5;
    std::string contextParam = req->getParameter("context");
    if (!contextParam.empty())
    {
        try { context = std::stoi(contextParam); } catch (...) { context = 5; }
    }
    if (context < 0) context = 0;
    if (context > 50) context = 50;

    int from = line->lineNumber - context;
    int to = line->lineNumber + context;

    Json::Value contextLines(Json::arrayValue);
    for (const auto& l : parser->GetLines())
    {
        if (l.lineNumber >= from && l.lineNumber <= to)
            contextLines.append(listingLineToJson(l));
    }

    Json::Value ret;
    ret["address"] = address;
    char hexStr[8];
    snprintf(hexStr, sizeof(hexStr), "0x%04X", address);
    ret["address_hex"] = hexStr;
    ret["found"] = true;
    ret["line"] = listingLineToJson(*line);
    ret["context"] = contextLines;
    ret["context_from"] = from;
    ret["context_to"] = to;
    ret["source_path"] = parser->GetSourcePath();

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/listing/step_line
/// @brief Execute until PC reaches a different listing source line (RunControl::StepLine).
/// @brief Body: {max_tstates?: N} — 0/absent = ~2 s of emulated time. On the long-call pool: never an I/O thread
void EmulatorAPI::stepLine(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { stepLineNow(req, std::move(*answer), id); });
}

void EmulatorAPI::stepLineNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    auto json = req->getJsonObject();
    const uint64_t maxTStates = json && json->isMember("max_tstates") ? (*json)["max_tstates"].asUInt64() : 0;
    ReplyRun(execution::RunControl(emulator).StepLine(webapi_session::RequesterOf(req, emulator->GetContext()),
                                                      maxTStates),
             callback);
}

/// @brief POST /api/v1/emulator/{id}/listing/run_to_line
/// @brief Run until PC reaches the first code byte of a listing line (at/after the given number; RunControl::RunToLine).
/// @brief Body: {line: N, max_tstates?: M} — 0/absent = ~10 s of emulated time. On the long-call pool
void EmulatorAPI::runToLine(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto json = req->getJsonObject();
    if (!json || !json->isMember("line"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Required: line";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { runToLineNow(req, std::move(*answer), id); });
}

void EmulatorAPI::runToLineNow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator)
        return;
    auto json = req->getJsonObject();
    const int line = json ? (*json)["line"].asInt() : 0;
    const uint64_t maxTStates = json && json->isMember("max_tstates") ? (*json)["max_tstates"].asUInt64() : 0;
    ReplyRun(execution::RunControl(emulator).RunToLine(webapi_session::RequesterOf(req, emulator->GetContext()), line,
                                                       maxTStates),
             callback);
}

// endregion Source Listing

// region Assembler

/// @brief POST /api/v1/emulator/{id}/assemble
/// @brief Assemble Z80 source text. Body: {code: "...", address: 0x8000 | "0x8000", write?: false}
void EmulatorAPI::assembleCode(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    auto json = req->getJsonObject();
    if (!json || !json->isMember("code") || !json->isMember("address"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Required: code, address";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string code = (*json)["code"].asString();

    // Address accepts a JSON number or a string (decimal, 0x.., #.., $..)
    uint32_t address = 0;
    if (!ParseJsonUInt((*json)["address"], 0xFFFF, address))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "'address' must be 0..65535 (a number, or a string: decimal, \"0x..\", \"#..\" or \"$..\")";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    bool write = json->isMember("write") && (*json)["write"].asBool();

    Z80TextAssembler assembler;
    AsmResult result = assembler.Assemble(code, address);

    if (!result.ok)
    {
        Json::Value error;
        error["error"] = "Assembly failed";
        error["message"] = result.error.message;
        error["line"] = result.error.line;
        error["source_line"] = result.error.sourceLine;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Optional: write the emitted bytes into emulator RAM
    if (write)
    {
        Memory* mem = emulator->GetMemory();
        if (!mem)
        {
            Json::Value error;
            error["error"] = "Internal Error";
            error["message"] = "Memory not available";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k500InternalServerError);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        uint32_t addr = result.startAddress;
        for (uint8_t b : result.bytes)
            mem->MemoryWriteFast(static_cast<uint16_t>((addr++) & 0xFFFF), b);
    }

    Json::Value ret;
    ret["status"] = "success";
    ret["address"] = result.startAddress;
    ret["end_address"] = result.endAddress;
    ret["size"] = static_cast<Json::UInt64>(result.bytes.size());

    Json::Value bytesArr(Json::arrayValue);
    for (uint8_t b : result.bytes)
        bytesArr.append(b);
    ret["bytes"] = bytesArr;

    Json::Value listing(Json::arrayValue);
    for (const auto& line : result.lines)
    {
        Json::Value entry;
        entry["address"] = line.address;
        if (!line.label.empty())
            entry["label"] = line.label;
        if (!line.source.empty())
            entry["source"] = line.source;
        if (!line.bytes.empty())
        {
            Json::Value lineBytes(Json::arrayValue);
            for (uint8_t b : line.bytes)
                lineBytes.append(b);
            entry["bytes"] = lineBytes;
        }
        listing.append(entry);
    }
    ret["listing"] = listing;

    Json::Value symbols(Json::objectValue);
    for (const auto& sym : result.symbols)
        symbols[sym.first] = sym.second;
    ret["symbols"] = symbols;

    if (write)
        ret["written"] = true;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// endregion Assembler

// region <Long run-control calls: off the HTTP worker (tdd §5, F4), one claim rule (F5)>

void EmulatorAPI::steps(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { stepsNow(req, std::move(*answer), id); });
}

void EmulatorAPI::stepOver(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { stepOverNow(req, std::move(*answer), id); });
}

void EmulatorAPI::stepOut(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { stepOutNow(req, std::move(*answer), id); });
}

void EmulatorAPI::skipUntil(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { skipUntilNow(req, std::move(*answer), id); });
}

void EmulatorAPI::runTStates(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { runTStatesNow(req, std::move(*answer), id); });
}

void EmulatorAPI::runToScanline(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { runToScanlineNow(req, std::move(*answer), id); });
}

void EmulatorAPI::runNScanlines(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { runNScanlinesNow(req, std::move(*answer), id); });
}

void EmulatorAPI::runToPixel(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { runToPixelNow(req, std::move(*answer), id); });
}

void EmulatorAPI::runToInterrupt(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { runToInterruptNow(req, std::move(*answer), id); });
}

void EmulatorAPI::runFrame(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { runFrameNow(req, std::move(*answer), id); });
}

void EmulatorAPI::runFrames(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                     const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator || RunControlHeldReply(req, emulator, callback))
        return;
    auto answer = std::make_shared<std::function<void(const HttpResponsePtr&)>>(std::move(callback));
    LongCallPool::Instance().Run([this, req, answer, id]() { runFramesNow(req, std::move(*answer), id); });
}

// endregion </Long run-control calls>

} // namespace v1
} // namespace api
