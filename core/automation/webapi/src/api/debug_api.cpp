// WebAPI Debug Commands Implementation
// Debug endpoints for stepping, breakpoints, and inspection
// Created 2026-01-21

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/cpu/z80.h>
#include <emulator/memory/memory.h>
#include <emulator/memory/memoryaccesstracker.h>
#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <debugger/labels/labelmanager.h>
#include <debugger/listing/listingparser.h>
#include <debugger/assembler/z80textassembler.h>
#include <base/featuremanager.h>
#include <common/dumphelper.h>
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

// region Stepping Commands

/// @brief POST /api/v1/emulator/{id}/step
/// @brief Execute single CPU instruction
void EmulatorAPI::step(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                       const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    // Check run-control claim (GDB TDD §3.3 / 1A.7.2)
    auto* ctx = emulator->GetContext();
    if (ctx && ctx->IsRunControlClaimed())
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
        // Execute single instruction
        emulator->RunSingleCPUCycle(false); // Don't skip breakpoints
        
        Z80State* z80 = emulator->GetZ80State();
        
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Executed 1 instruction";
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
void EmulatorAPI::steps(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                        const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    // Check run-control claim (GDB TDD §3.3 / 1A.7.2)
    auto* ctx = emulator->GetContext();
    if (ctx && ctx->IsRunControlClaimed())
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
        
        // Execute N instructions
        emulator->RunNCPUCycles(count, false); // Don't skip breakpoints
        
        Z80State* z80 = emulator->GetZ80State();
        
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Executed " + std::to_string(count) + " instructions";
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
void EmulatorAPI::stepOver(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    // Check run-control claim (GDB TDD §3.3 / 1A.7.2)
    auto* ctx = emulator->GetContext();
    if (ctx && ctx->IsRunControlClaimed())
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
        emulator->StepOver();
        
        Z80State* z80 = emulator->GetZ80State();
        
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Step over completed";
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
void EmulatorAPI::stepOut(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                          const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;

    try
    {
        emulator->StepOut();

        Z80State* z80 = emulator->GetZ80State();

        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Step out completed";
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
/// @brief Request body: {"pc": "0x8000" | 32768, "max_tstates": 70000000}
void EmulatorAPI::skipUntil(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
        const Json::Value& pcValue = (*json)["pc"];
        if (pcValue.isString())
        {
            target32 = static_cast<uint32_t>(std::stoul(pcValue.asString(), nullptr, 0));
        }
        else if (pcValue.isNumeric())
        {
            target32 = pcValue.asUInt();
        }
        else
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "'pc' must be a hex string or integer";

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

        // Safety budget: default 100 frames of emulated time (~2 s), hard cap 200 s
        EmulatorContext* context = emulator->GetContext();
        unsigned maxTStates = json->isMember("max_tstates") ? (*json)["max_tstates"].asUInt() : 0;
        if (maxTStates == 0 && context)
        {
            maxTStates = context->config.frame * 100;
        }
        if (maxTStates == 0)
        {
            maxTStates = 6988800;  // Fallback if config is unavailable
        }
        if (maxTStates > 700000000u)
        {
            maxTStates = 700000000u;
        }

        emulator->RunUntilCondition([target](const Z80State& state) { return state.pc == target; }, maxTStates);

        Z80State* z80 = emulator->GetZ80State();
        const bool hit = z80 && z80->pc == target;

        Json::Value ret;
        ret["status"] = "success";
        ret["hit"] = hit;
        ret["message"] = hit ? "Reached target address" : "T-state budget exhausted before reaching target";
        ret["max_tstates"] = maxTStates;
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
void EmulatorAPI::runTStates(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
void EmulatorAPI::runToScanline(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    try
    {
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
        
        unsigned scanline = (*json)["scanline"].asUInt();
        emulator->RunUntilScanline(scanline);
        
        Z80State* z80 = emulator->GetZ80State();
        
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Ran to scanline " + std::to_string(scanline);
        ret["scanline"] = scanline;
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
void EmulatorAPI::runNScanlines(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    try
    {
        auto json = req->getJsonObject();
        unsigned count = json && json->isMember("count") ? (*json)["count"].asUInt() : 1;
        if (count < 1) count = 1;
        if (count > 1000) count = 1000; // Safety limit
        
        emulator->RunNScanlines(count);
        
        Z80State* z80 = emulator->GetZ80State();
        
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Ran " + std::to_string(count) + " scanlines";
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
void EmulatorAPI::runToPixel(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    try
    {
        emulator->RunUntilNextScreenPixel();
        
        Z80State* z80 = emulator->GetZ80State();
        
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Ran to next screen pixel";
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
void EmulatorAPI::runToInterrupt(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    try
    {
        emulator->RunUntilInterrupt();
        
        Z80State* z80 = emulator->GetZ80State();
        
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Ran to interrupt";
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
void EmulatorAPI::runFrame(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    try
    {
        emulator->RunFrame();
        
        Z80State* z80 = emulator->GetZ80State();
        
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Ran one frame";
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
void EmulatorAPI::runFrames(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
void EmulatorAPI::getDebugMode(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    
    BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
    
    Json::Value ret;
    Json::Value breakpointsArray(Json::arrayValue);
    
    if (bpm)
    {
        const auto& allBps = bpm->GetAllBreakpoints();
        for (const auto& pair : allBps)
        {
            const BreakpointDescriptor* bp = pair.second;
            if (!bp) continue;
            
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
                default:
                    bpObj["type"] = "unknown";
            }
            
            bpObj["address"] = bp->z80address;
            
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
            
            breakpointsArray.append(bpObj);
        }
    }
    
    ret["count"] = breakpointsArray.size();
    ret["breakpoints"] = breakpointsArray;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/breakpoints
/// @brief Add a breakpoint
/// @brief Request body: {"type": "execution"|"read"|"write"|"port_in"|"port_out", "address": 0x8000}
void EmulatorAPI::addBreakpoint(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    
    BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
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
    
    std::string type = (*json)["type"].asString();
    uint16_t address = static_cast<uint16_t>((*json)["address"].asUInt());
    
    uint16_t bpId = 0;
    bool success = false;
    
    if (type == "execution" || type == "exec" || type == "bp")
    {
        bpId = bpm->AddExecutionBreakpoint(address);
        success = true;
    }
    else if (type == "read" || type == "r")
    {
        bpId = bpm->AddMemReadBreakpoint(address);
        success = true;
    }
    else if (type == "write" || type == "w")
    {
        bpId = bpm->AddMemWriteBreakpoint(address);
        success = true;
    }
    else if (type == "port_in" || type == "in")
    {
        bpId = bpm->AddPortInBreakpoint(address);
        success = true;
    }
    else if (type == "port_out" || type == "out")
    {
        bpId = bpm->AddPortOutBreakpoint(address);
        success = true;
    }
    else
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid type. Use: execution, read, write, port_in, port_out";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["id"] = bpId;
    ret["type"] = type;
    ret["address"] = address;
    ret["message"] = "Breakpoint added";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(HttpStatusCode::k201Created);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief DELETE /api/v1/emulator/{id}/breakpoints
/// @brief Clear all breakpoints
void EmulatorAPI::clearBreakpoints(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    
    BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
    if (bpm)
    {
        bpm->ClearBreakpoints();
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
void EmulatorAPI::removeBreakpoint(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    
    uint16_t bpId = static_cast<uint16_t>(std::stoul(bpIdStr));
    BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
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
void EmulatorAPI::enableBreakpoint(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    
    uint16_t bpId = static_cast<uint16_t>(std::stoul(bpIdStr));
    BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
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
void EmulatorAPI::disableBreakpoint(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    
    uint16_t bpId = static_cast<uint16_t>(std::stoul(bpIdStr));
    BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
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
void EmulatorAPI::getBreakpointStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    
    BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
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

// endregion Breakpoints

// region Memory Inspection

/// @brief GET /api/v1/emulator/{id}/registers
/// @brief Get CPU registers
void EmulatorAPI::getRegisters(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    
    Json::Value ret;
    
    // Main registers
    Json::Value main;
    main["af"] = z80->af;
    main["bc"] = z80->bc;
    main["de"] = z80->de;
    main["hl"] = z80->hl;
    ret["main"] = main;
    
    // Alternate registers
    Json::Value alt;
    alt["af_"] = z80->alt.af;
    alt["bc_"] = z80->alt.bc;
    alt["de_"] = z80->alt.de;
    alt["hl_"] = z80->alt.hl;
    ret["alternate"] = alt;
    
    // Index registers
    Json::Value idx;
    idx["ix"] = z80->ix;
    idx["iy"] = z80->iy;
    ret["index"] = idx;
    
    // Special registers
    Json::Value special;
    special["pc"] = z80->pc;
    special["sp"] = z80->sp;
    special["i"] = z80->i;
    special["r"] = (z80->r_hi << 7) | (z80->r_low & 0x7F);
    ret["special"] = special;
    
    // Interrupt state
    Json::Value interrupt;
    interrupt["iff1"] = z80->iff1;
    interrupt["iff2"] = z80->iff2;
    interrupt["im"] = z80->im;
    ret["interrupt"] = interrupt;
    
    // Flags decoded
    uint8_t f = z80->af & 0xFF;
    Json::Value flags;
    flags["s"] = (f & 0x80) ? 1 : 0;
    flags["z"] = (f & 0x40) ? 1 : 0;
    flags["y"] = (f & 0x20) ? 1 : 0;
    flags["h"] = (f & 0x10) ? 1 : 0;
    flags["x"] = (f & 0x08) ? 1 : 0;
    flags["pv"] = (f & 0x04) ? 1 : 0;
    flags["n"] = (f & 0x02) ? 1 : 0;
    flags["c"] = (f & 0x01) ? 1 : 0;
    ret["flags"] = flags;

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

    uint16_t value = static_cast<uint16_t>((*json)["value"].asUInt());

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

    Z80::SetRegisterValue(z80, name, value);

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
    if (len > 4096) len = 4096;
    if (len < 1) len = 1;
    
    // Read memory
    Json::Value ret;
    ret["address"] = addr;
    ret["length"] = len;
    
    Json::Value data(Json::arrayValue);
    for (unsigned i = 0; i < len; i++)
    {
        data.append(mem->MemoryReadFast((addr + i) & 0xFFFF, false));
    }
    ret["data"] = data;
    
    // Also provide hex string for convenience
    std::stringstream hexStr;
    for (unsigned i = 0; i < len; i++)
    {
        hexStr << std::hex << std::uppercase << std::setw(2) << std::setfill('0') 
               << static_cast<int>(mem->MemoryReadFast((addr + i) & 0xFFFF, false));
        if (i < len - 1) hexStr << " ";
    }
    ret["hex"] = hexStr.str();
    
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
    
    unsigned page = std::stoul(pageStr);
    unsigned offset = std::stoul(offsetStr);
    
    // Get length from query param
    unsigned len = 128;
    auto lenParam = req->getParameter("len");
    if (!lenParam.empty()) len = std::stoul(lenParam);
    if (len > 16384) len = 16384;
    if (len < 1) len = 1;
    
    // Clamp to page boundary
    if (offset + len > 16384) len = 16384 - offset;
    
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
            pagePtr = mem->RAMPageAddress(page);
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
            pagePtr = mem->ROMPageHostAddress(page);
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
    
    unsigned page = std::stoul(pageStr);
    unsigned offset = std::stoul(offsetStr);
    
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
            if (page >= MAX_RAM_PAGES) { /* error */ return; }
            pagePtr = mem->RAMPageAddress(page);
            typeName = "ram";
            break;
        case 1:
            if (page >= MAX_ROM_PAGES) { /* error */ return; }
            pagePtr = mem->ROMPageHostAddress(page);
            typeName = "rom";
            break;
        case 2:
            if (page >= MAX_CACHE_PAGES) { /* error */ return; }
            pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
            typeName = "cache";
            break;
        case 3:
            if (page >= MAX_MISC_PAGES) { /* error */ return; }
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
void EmulatorAPI::getMemoryInfo(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

// endregion Memory Inspection

// region Analysis

/// @brief GET /api/v1/emulator/{id}/memcounters
/// @brief Get memory access statistics
void EmulatorAPI::getMemCounters(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
        ret["message"] = "Call trace active";
        
        // TODO: Add actual call trace entries when CallTraceManager exposes API
        ret["entries"] = Json::arrayValue;
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
/// @brief Query params: address (default: PC), count (default: 10, max: 100)
void EmulatorAPI::getDisasm(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto emulator = getEmulatorOrError(id, callback);
    if (!emulator) return;
    
    EmulatorContext* ctx = emulator->GetContext();
    Memory* memory = ctx->pMemory;
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
    
    Z80Disassembler* disasm = dbg->GetDisassembler().get();
    LabelManager* labelMgr = dbg->GetLabelManager();
    
    // Parse query parameters
    std::string addrParam = req->getParameter("address");
    std::string countParam = req->getParameter("count");
    
    uint16_t address = z80->pc;  // Default to PC
    size_t count = 10;           // Default count
    
    if (!addrParam.empty())
    {
        try
        {
            if (addrParam.find("0x") == 0 || addrParam.find("0X") == 0)
                address = static_cast<uint16_t>(std::stoul(addrParam, nullptr, 16));
            else
                address = static_cast<uint16_t>(std::stoul(addrParam));
        }
        catch (...) {}
    }
    
    if (!countParam.empty())
    {
        count = std::stoul(countParam);
        if (count > 100) count = 100;
        if (count < 1) count = 1;
    }
    
    Json::Value ret;
    ret["address"] = address;
    ret["count"] = static_cast<unsigned int>(count);
    ret["instructions"] = Json::arrayValue;
    
    uint16_t currentAddr = address;
    for (size_t i = 0; i < count && currentAddr >= address; ++i)
    {
        // Read up to 4 bytes for instruction
        std::vector<uint8_t> buffer;
        for (int j = 0; j < 4; ++j)
        {
            buffer.push_back(memory->MemoryReadFast(static_cast<uint16_t>(currentAddr + j), false));
        }
        
        uint8_t cmdLen = 0;
        DecodedInstruction decoded;
        std::string mnemonic = disasm->disassembleSingleCommandWithRuntime(buffer, currentAddr, &cmdLen, z80, memory, &decoded);
        
        if (cmdLen == 0) cmdLen = 1;  // Safety: at least advance by 1
        
        Json::Value instr;
        instr["address"] = currentAddr;
        
        // Build hex bytes string
        std::string hexBytes;
        for (uint8_t j = 0; j < cmdLen; ++j)
        {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X", buffer[j]);
            hexBytes += buf;
        }
        instr["bytes"] = hexBytes;
        instr["mnemonic"] = mnemonic;
        instr["size"] = cmdLen;
        
        // Label at the instruction address itself (e.g. jump destination marker)
        if (labelMgr)
        {
            auto label = labelMgr->GetLabelByZ80Address(currentAddr);
            if (label && !label->name.empty())
                instr["label"] = label->name;
        }
        
        // Add target address for jumps/calls. Indirect targets (JP (HL), JP (IX)) are only
        // known at runtime - the field is omitted when the target could not be resolved
        if (decoded.hasJump || decoded.hasRelativeJump)
        {
            uint16_t target = decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr;
            if (!decoded.hasIndirect || decoded.hasRuntime)
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
        
        // Effective memory address for indexed (IX/IY+d) instructions - requires runtime registers
        if (decoded.hasDisplacement && decoded.hasRuntime)
        {
            instr["displacement"] = decoded.displacement;
            instr["effectiveAddress"] = decoded.displacementAddr;
            
            if (labelMgr)
            {
                auto effectiveLabel = labelMgr->GetLabelByZ80Address(decoded.displacementAddr);
                if (effectiveLabel && !effectiveLabel->name.empty())
                    instr["effectiveAddressLabel"] = effectiveLabel->name;
            }
        }
        
        ret["instructions"].append(instr);
        
        currentAddr += cmdLen;
    }
    
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
    
    try {
        page = static_cast<uint8_t>(std::stoul(pageParam));
        if (!offsetParam.empty()) {
            if (offsetParam.find("0x") == 0 || offsetParam.find("0X") == 0)
                offset = static_cast<uint16_t>(std::stoul(offsetParam, nullptr, 16));
            else
                offset = static_cast<uint16_t>(std::stoul(offsetParam));
        }
    } catch (...) {
        Json::Value error;
        error["error"] = "Invalid page or offset parameter";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    if (offset >= PAGE_SIZE) offset = PAGE_SIZE - 1;
    
    if (!countParam.empty()) {
        count = std::stoul(countParam);
        if (count > 100) count = 100;
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
    uint16_t address = static_cast<uint16_t>((*json)["address"].asUInt());
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
void EmulatorAPI::getLabel(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
void EmulatorAPI::removeLabel(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
void EmulatorAPI::clearLabels(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
/// @brief Execute until PC reaches a different listing source line.
/// @brief Body: {max_tstates?: N} — 0/absent = ~2 s of emulated time
void EmulatorAPI::stepLine(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    Z80State* z80 = emulator->GetZ80State();
    if (!z80)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Z80 state not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const ListingLine* startLine = parser->FindLineByAddress(z80->pc);
    int startLineNumber = startLine ? startLine->lineNumber : -1;

    auto json = req->getJsonObject();
    unsigned maxTStates = json && json->isMember("max_tstates") ? (*json)["max_tstates"].asUInt() : 0;
    if (maxTStates == 0)
        maxTStates = ctx->config.frame * 100;  // ~2 s of emulated time

    // Stop on the first instruction whose listing line differs from the starting line.
    // When the current PC is not covered, stop on the first covered line instead.
    emulator->RunUntilCondition(
        [parser, startLineNumber](const Z80State& state) {
            const ListingLine* line = parser->FindLineByAddress(state.pc);
            return line != nullptr && line->lineNumber != startLineNumber;
        },
        maxTStates);

    z80 = emulator->GetZ80State();
    const ListingLine* endLine = z80 ? parser->FindLineByAddress(z80->pc) : nullptr;
    bool lineChanged = endLine != nullptr && endLine->lineNumber != startLineNumber;

    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = lineChanged ? "Stepped to a different source line" : "Stopped without reaching a different source line";
    if (z80)
    {
        ret["pc"] = z80->pc;
        ret["sp"] = z80->sp;
    }
    ret["state"] = stateToString(emulator->GetState());
    ret["line_changed"] = lineChanged;
    if (startLine)
        ret["from_line"] = listingLineToJson(*startLine);
    if (endLine)
        ret["to_line"] = listingLineToJson(*endLine);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/listing/run_to_line
/// @brief Run until PC reaches the first code byte of a listing line (at/after the given number).
/// @brief Body: {line: N, max_tstates?: M} — 0/absent = ~10 s of emulated time
void EmulatorAPI::runToLine(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    int lineNumber = (*json)["line"].asInt();
    const ListingLine* target = parser->FindNextCodeLine(lineNumber);
    if (!target)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "No code line at or after line " + std::to_string(lineNumber) + " in loaded listing";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Z80State* z80 = emulator->GetZ80State();
    if (!z80)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Z80 state not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    unsigned maxTStates = json->isMember("max_tstates") ? (*json)["max_tstates"].asUInt() : 0;
    if (maxTStates == 0)
        maxTStates = ctx->config.frame * 500;  // ~10 s of emulated time

    const uint16_t targetAddress = target->addressStart;
    bool alreadyAt = z80->pc == targetAddress;

    if (!alreadyAt)
    {
        emulator->RunUntilCondition(
            [targetAddress](const Z80State& state) { return state.pc == targetAddress; },
            maxTStates);
    }

    z80 = emulator->GetZ80State();
    bool reached = z80 && z80->pc == targetAddress;

    Json::Value ret;
    ret["status"] = reached ? "success" : "timeout";
    ret["message"] = alreadyAt ? "Already at target line" : (reached ? "Reached target line" : "Safety limit reached before target line");
    if (z80)
    {
        ret["pc"] = z80->pc;
        ret["sp"] = z80->sp;
    }
    ret["state"] = stateToString(emulator->GetState());
    ret["reached"] = reached;
    ret["already_at"] = alreadyAt;
    ret["target_line"] = listingLineToJson(*target);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
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

    // Address accepts JSON number or string (0x / $ / decimal)
    uint16_t address = 0;
    if ((*json)["address"].isString())
    {
        std::string addressStr = (*json)["address"].asString();
        try
        {
            if (addressStr.substr(0, 2) == "0x" || addressStr.substr(0, 2) == "0X")
                address = static_cast<uint16_t>(std::stoul(addressStr.substr(2), nullptr, 16));
            else if (addressStr[0] == '$')
                address = static_cast<uint16_t>(std::stoul(addressStr.substr(1), nullptr, 16));
            else
                address = static_cast<uint16_t>(std::stoul(addressStr, nullptr, 0));
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
    }
    else
    {
        address = static_cast<uint16_t>((*json)["address"].asUInt() & 0xFFFF);
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

} // namespace v1
} // namespace api
