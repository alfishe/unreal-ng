// WebAPI Opcode Profiler Implementation
// Implements /profiler/opcode endpoints - 2026-01-27

#include <base/featuremanager.h>
#include <drogon/HttpResponse.h>
#include <emulator/cpu/opcode_profiler.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

#include "../emulator_api.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief POST /api/v1/emulator/{id}/profiler/opcode/session
/// @brief Control profiler session (start, stop, clear)
/// Body: {"action": "start" | "stop" | "clear"}
void EmulatorAPI::profilerSession(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
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
        return;
    }

    auto* context = emulator->GetContext();
    if (!context || !context->pCore)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Core not available for this emulator";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto* z80 = context->pCore->GetZ80();
    auto* profiler = z80 ? z80->GetOpcodeProfiler() : nullptr;
    
    if (!profiler)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Opcode profiler not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse request body
    auto json = req->getJsonObject();
    if (!json || !json->isMember("action"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'action' field in request body (start, stop, clear)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string action = (*json)["action"].asString();
    Json::Value ret;
    ret["emulator_id"] = id;

    if (action == "start")
    {
        // Enable feature automatically if not already enabled
        if (context->pFeatureManager)
        {
            context->pFeatureManager->setFeature(Features::kOpcodeProfiler, true);
            z80->UpdateFeatureCache();
        }
        
        profiler->Start();
        ret["action"] = "start";
        ret["message"] = "Opcode profiler session started (previous data cleared)";
        ret["capturing"] = true;
    }
    else if (action == "stop")
    {
        profiler->Stop();
        auto status = profiler->GetStatus();
        ret["action"] = "stop";
        ret["message"] = "Opcode profiler session stopped (data preserved)";
        ret["capturing"] = false;
        ret["total_executions"] = static_cast<Json::UInt64>(status.totalExecutions);
        ret["trace_size"] = status.traceSize;
    }
    else if (action == "clear")
    {
        profiler->Clear();
        ret["action"] = "clear";
        ret["message"] = "Opcode profiler data cleared";
        ret["capturing"] = profiler->GetStatus().capturing;
    }
    else
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid action. Must be 'start', 'stop', or 'clear'";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/profiler/opcode/status
/// @brief Get profiler status
void EmulatorAPI::getProfilerStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id) const
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
        return;
    }

    auto* context = emulator->GetContext();
    auto* z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
    auto* profiler = z80 ? z80->GetOpcodeProfiler() : nullptr;

    if (!profiler)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Opcode profiler not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto status = profiler->GetStatus();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["capturing"] = status.capturing;
    ret["total_executions"] = static_cast<Json::UInt64>(status.totalExecutions);
    ret["trace_size"] = status.traceSize;
    ret["trace_capacity"] = status.traceCapacity;
    
    // Feature status
    bool featureEnabled = context->pFeatureManager ? 
        context->pFeatureManager->isEnabled(Features::kOpcodeProfiler) : false;
    ret["feature_enabled"] = featureEnabled;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/profiler/opcode/counters
/// @brief Get opcode execution counters
/// Query params: ?limit=N (default 100)
void EmulatorAPI::getProfilerCounters(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                      const std::string& id) const
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
        return;
    }

    auto* context = emulator->GetContext();
    auto* z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
    auto* profiler = z80 ? z80->GetOpcodeProfiler() : nullptr;

    if (!profiler)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Opcode profiler not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Get limit from query params
    uint32_t limit = 100;
    auto limitParam = req->getParameter("limit");
    if (!limitParam.empty())
    {
        try
        {
            limit = static_cast<uint32_t>(std::stoul(limitParam));
        }
        catch (...)
        {
            // Keep default
        }
    }

    auto topOpcodes = profiler->GetTopOpcodes(limit);
    auto status = profiler->GetStatus();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["total_executions"] = static_cast<Json::UInt64>(status.totalExecutions);
    ret["limit"] = limit;
    ret["count"] = static_cast<Json::UInt>(topOpcodes.size());

    Json::Value counters(Json::arrayValue);
    for (const auto& entry : topOpcodes)
    {
        Json::Value counter;
        counter["prefix"] = entry.prefix;
        counter["opcode"] = entry.opcode;
        counter["count"] = static_cast<Json::UInt64>(entry.count);
        
        // Generate prefix string name
        std::string prefixName;
        switch (entry.prefix)
        {
            case 0x0000: prefixName = "none"; break;
            case 0x00CB: prefixName = "CB"; break;
            case 0x00DD: prefixName = "DD"; break;
            case 0x00ED: prefixName = "ED"; break;
            case 0x00FD: prefixName = "FD"; break;
            case 0xDDCB: prefixName = "DDCB"; break;
            case 0xFDCB: prefixName = "FDCB"; break;
            default: prefixName = "unknown"; break;
        }
        counter["prefix_name"] = prefixName;
        
        counters.append(counter);
    }
    ret["counters"] = counters;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/profiler/opcode/trace
/// @brief Get recent execution trace
/// Query params: ?count=N (default 100)
void EmulatorAPI::getProfilerTrace(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
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
        return;
    }

    auto* context = emulator->GetContext();
    auto* z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
    auto* profiler = z80 ? z80->GetOpcodeProfiler() : nullptr;

    if (!profiler)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Opcode profiler not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Get count from query params
    uint32_t count = 100;
    auto countParam = req->getParameter("count");
    if (!countParam.empty())
    {
        try
        {
            count = static_cast<uint32_t>(std::stoul(countParam));
        }
        catch (...)
        {
            // Keep default
        }
    }

    auto trace = profiler->GetRecentTrace(count);
    auto status = profiler->GetStatus();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["trace_size"] = status.traceSize;
    ret["requested_count"] = count;
    ret["returned_count"] = static_cast<Json::UInt>(trace.size());

    Json::Value entries(Json::arrayValue);
    for (const auto& entry : trace)
    {
        Json::Value traceEntry;
        traceEntry["pc"] = entry.pc;
        traceEntry["prefix"] = entry.prefix;
        traceEntry["opcode"] = entry.opcode;
        traceEntry["flags"] = entry.flags;
        traceEntry["a"] = entry.a;
        traceEntry["frame"] = entry.frame;
        traceEntry["tstate"] = entry.tState;
        
        entries.append(traceEntry);
    }
    ret["trace"] = entries;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
