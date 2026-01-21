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
    
    Json::Value ret;
    ret["is_paused"] = emulator->IsPaused();
    ret["breakpoints_count"] = bpm ? static_cast<Json::UInt>(bpm->GetBreakpointsCount()) : 0u;
    
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

} // namespace v1
} // namespace api
