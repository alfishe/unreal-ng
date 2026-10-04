// WebAPI Opcode Profiler Implementation
// Implements /profiler/opcode endpoints - 2026-01-27

#include <algorithm>
#include <filesystem>
#include <base/featuremanager.h>
#include <common/filehelper.h>
#include <drogon/HttpResponse.h>
#include <emulator/cpu/opcode_profiler.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memoryaccesstracker.h>
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

// Helper to get opcode profiler
static OpcodeProfiler* getOpcodeProfiler(EmulatorContext* context, Z80*& z80,
    std::function<void(const HttpResponsePtr&)>& callback, const std::string& id)
{
    if (!context || !context->pCore)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Core not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return nullptr;
    }
    z80 = context->pCore->GetZ80();
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
        return nullptr;
    }
    return profiler;
}

/// @brief POST /api/v1/emulator/{id}/profiler/opcode/start
void EmulatorAPI::opcodeProfilerStart(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    Z80* z80 = nullptr;
    auto* profiler = getOpcodeProfiler(context, z80, callback, id);
    if (!profiler) return;

    // Enable feature automatically
    if (context->pFeatureManager)
    {
        context->pFeatureManager->setFeature(Features::kOpcodeProfiler, true);
        z80->UpdateFeatureCache();
    }
    profiler->Start();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "opcode";
    ret["action"] = "start";
    ret["message"] = "Opcode profiler session started";
    ret["session_state"] = "capturing";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/opcode/stop
void EmulatorAPI::opcodeProfilerStop(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    Z80* z80 = nullptr;
    auto* profiler = getOpcodeProfiler(context, z80, callback, id);
    if (!profiler) return;

    profiler->Stop();
    auto status = profiler->GetStatus();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "opcode";
    ret["action"] = "stop";
    ret["message"] = "Opcode profiler session stopped";
    ret["session_state"] = "stopped";
    ret["total_executions"] = static_cast<Json::UInt64>(status.totalExecutions);
    ret["trace_size"] = status.traceSize;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/opcode/pause
void EmulatorAPI::opcodeProfilerPause(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    Z80* z80 = nullptr;
    auto* profiler = getOpcodeProfiler(context, z80, callback, id);
    if (!profiler) return;

    profiler->Pause();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "opcode";
    ret["action"] = "pause";
    ret["message"] = "Opcode profiler session paused";
    ret["session_state"] = "paused";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/opcode/resume
void EmulatorAPI::opcodeProfilerResume(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    Z80* z80 = nullptr;
    auto* profiler = getOpcodeProfiler(context, z80, callback, id);
    if (!profiler) return;

    profiler->Resume();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "opcode";
    ret["action"] = "resume";
    ret["message"] = "Opcode profiler session resumed";
    ret["session_state"] = "capturing";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/opcode/clear
void EmulatorAPI::opcodeProfilerClear(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    Z80* z80 = nullptr;
    auto* profiler = getOpcodeProfiler(context, z80, callback, id);
    if (!profiler) return;

    profiler->Clear();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "opcode";
    ret["action"] = "clear";
    ret["message"] = "Opcode profiler data cleared";
    ret["session_state"] = profiler->IsCapturing() ? "capturing" : 
                           (profiler->GetSessionState() == ProfilerSessionState::Paused ? "paused" : "stopped");

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

// ============================================================================
// Memory Profiler Endpoints
// ============================================================================

/// @brief Helper to get session state string
static std::string sessionStateToString(ProfilerSessionState state)
{
    switch (state)
    {
        case ProfilerSessionState::Stopped: return "stopped";
        case ProfilerSessionState::Capturing: return "capturing";
        case ProfilerSessionState::Paused: return "paused";
        default: return "unknown";
    }
}

// Helper to get memory tracker for memory profiler operations
static MemoryAccessTracker* getMemoryTracker(EmulatorContext* context, 
    std::function<void(const HttpResponsePtr&)>& callback, const std::string& id)
{
    if (!context || !context->pMemory)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return nullptr;
    }
    return &context->pMemory->GetAccessTracker();
}

/// @brief POST /api/v1/emulator/{id}/profiler/memory/start
void EmulatorAPI::memoryProfilerStart(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* tracker = getMemoryTracker(context, callback, id);
    if (!tracker) return;

    // Enable feature automatically
    if (context->pFeatureManager)
    {
        context->pFeatureManager->setFeature(Features::kDebugMode, true);
        context->pFeatureManager->setFeature(Features::kMemoryTracking, true);
        tracker->UpdateFeatureCache();
    }
    tracker->StartMemorySession();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "memory";
    ret["action"] = "start";
    ret["message"] = "Memory profiler session started";
    ret["session_state"] = sessionStateToString(tracker->GetMemorySessionState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/memory/stop
void EmulatorAPI::memoryProfilerStop(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* tracker = getMemoryTracker(context, callback, id);
    if (!tracker) return;

    tracker->StopMemorySession();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "memory";
    ret["action"] = "stop";
    ret["message"] = "Memory profiler session stopped";
    ret["session_state"] = sessionStateToString(tracker->GetMemorySessionState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/memory/pause
void EmulatorAPI::memoryProfilerPause(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* tracker = getMemoryTracker(context, callback, id);
    if (!tracker) return;

    tracker->PauseMemorySession();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "memory";
    ret["action"] = "pause";
    ret["message"] = "Memory profiler session paused";
    ret["session_state"] = sessionStateToString(tracker->GetMemorySessionState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/memory/resume
void EmulatorAPI::memoryProfilerResume(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* tracker = getMemoryTracker(context, callback, id);
    if (!tracker) return;

    tracker->ResumeMemorySession();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "memory";
    ret["action"] = "resume";
    ret["message"] = "Memory profiler session resumed";
    ret["session_state"] = sessionStateToString(tracker->GetMemorySessionState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/memory/clear
void EmulatorAPI::memoryProfilerClear(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* tracker = getMemoryTracker(context, callback, id);
    if (!tracker) return;

    tracker->ClearMemoryData();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "memory";
    ret["action"] = "clear";
    ret["message"] = "Memory profiler data cleared";
    ret["session_state"] = sessionStateToString(tracker->GetMemorySessionState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/profiler/memory/status
void EmulatorAPI::getMemoryProfilerStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* memory = context && context->pMemory ? context->pMemory : nullptr;
    auto* tracker = memory ? &memory->GetAccessTracker() : nullptr;

    if (!tracker)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory access tracker not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "memory";
    ret["session_state"] = sessionStateToString(tracker->GetMemorySessionState());
    ret["capturing"] = tracker->IsMemoryCapturing();
    
    bool featureEnabled = context->pFeatureManager ? 
        context->pFeatureManager->isEnabled(Features::kMemoryTracking) : false;
    ret["feature_enabled"] = featureEnabled;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

namespace
{
/// The memory access tracker of `id`, or the error reply already sent
MemoryAccessTracker* ResolveMemoryTracker(const std::string& id,
                                          const std::function<void(const HttpResponsePtr&)>& callback,
                                          const std::function<void(HttpResponsePtr)>& addCors)
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    auto* context = emulator ? emulator->GetContext() : nullptr;
    auto* tracker = context && context->pMemory ? &context->pMemory->GetAccessTracker() : nullptr;
    if (tracker)
        return tracker;

    Json::Value error;
    error["error"] = emulator ? "Internal Error" : "Not Found";
    error["message"] = emulator ? "Memory access tracker not available" : "Emulator with specified ID not found";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(emulator ? HttpStatusCode::k500InternalServerError : HttpStatusCode::k404NotFound);
    addCors(resp);
    callback(resp);
    return nullptr;
}

void ReplyBadRequest(const std::string& message, const std::function<void(const HttpResponsePtr&)>& callback,
                     const std::function<void(HttpResponsePtr)>& addCors)
{
    Json::Value error;
    error["error"] = "Bad Request";
    error["message"] = message;
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(HttpStatusCode::k400BadRequest);
    addCors(resp);
    callback(resp);
}

/// Decimal or 0x-prefixed hex; false on anything else
bool ParseUnsigned(const std::string& text, unsigned long& value)
{
    if (text.empty())
        return false;
    try
    {
        size_t used = 0;
        value = std::stoul(text, &used, 0);
        return used == text.size();
    }
    catch (...)
    {
        return false;
    }
}

const char* PageTypeName(uint8_t type)
{
    switch (type)
    {
        case 0: return "RAM";
        case 1: return "ROM";
        case 2: return "CACHE";
        default: return "MISC";
    }
}
}    // namespace

/// @brief GET /api/v1/emulator/{id}/profiler/memory/pages?limit=N
/// Per physical page totals since the session started, most active first
void EmulatorAPI::getMemoryProfilerPages(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback,
                                         const std::string& id) const
{
    auto addCors = [](HttpResponsePtr resp) { addCorsHeaders(resp); };
    MemoryAccessTracker* tracker = ResolveMemoryTracker(id, callback, addCors);
    if (!tracker)
        return;

    unsigned long limit = 0;
    const std::string limitText = req->getParameter("limit");
    if (!limitText.empty() && !ParseUnsigned(limitText, limit))
    {
        ReplyBadRequest("limit must be a number", callback, addCors);
        return;
    }

    std::vector<PageInfo> pages = tracker->GetActivePageInfos();
    auto total = [tracker](const PageInfo& info) {
        return static_cast<uint64_t>(tracker->GetPageReadAccessCount(info.absPageIndex)) +
               tracker->GetPageWriteAccessCount(info.absPageIndex) +
               tracker->GetPageExecuteAccessCount(info.absPageIndex);
    };
    std::stable_sort(pages.begin(), pages.end(),
                     [&total](const PageInfo& a, const PageInfo& b) { return total(a) > total(b); });
    if (limit > 0 && pages.size() > limit)
        pages.resize(limit);

    Json::Value list(Json::arrayValue);
    for (const PageInfo& info : pages)
    {
        Json::Value page;
        page["page"] = info.absPageIndex;
        page["type"] = PageTypeName(info.pageType);
        page["number"] = info.logicalPageNum;
        page["reads"] = tracker->GetPageReadAccessCount(info.absPageIndex);
        page["writes"] = tracker->GetPageWriteAccessCount(info.absPageIndex);
        page["executes"] = tracker->GetPageExecuteAccessCount(info.absPageIndex);
        page["mapped_bank"] = info.mappedToBank;
        list.append(page);
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["session_state"] = sessionStateToString(tracker->GetMemorySessionState());
    ret["pages"] = list;
    ret["count"] = static_cast<Json::UInt>(list.size());
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/profiler/memory/counters
/// mode=z80 (default): the 64K address space as the CPU saw it; mode=physical with page=N: one
/// 16K physical page (N = absolute page index, as /pages lists it). start/end (inclusive) narrow
/// the window. format=dense (default) returns three arrays indexed by offset from start;
/// format=sparse lists only touched addresses as [address, reads, writes, executes]
void EmulatorAPI::getMemoryProfilerCounters(const HttpRequestPtr& req,
                                            std::function<void(const HttpResponsePtr&)>&& callback,
                                            const std::string& id) const
{
    auto addCors = [](HttpResponsePtr resp) { addCorsHeaders(resp); };
    MemoryAccessTracker* tracker = ResolveMemoryTracker(id, callback, addCors);
    if (!tracker)
        return;

    std::string mode = req->getParameter("mode");
    if (mode.empty())
        mode = "z80";
    const std::string format = req->getParameter("format").empty() ? "dense" : req->getParameter("format");
    if ((mode != "z80" && mode != "physical") || (format != "dense" && format != "sparse"))
    {
        ReplyBadRequest("mode must be z80 or physical, format dense or sparse", callback, addCors);
        return;
    }

    const uint32_t* reads = nullptr;
    const uint32_t* writes = nullptr;
    const uint32_t* executes = nullptr;
    unsigned long size = 0;
    unsigned long page = 0;
    if (mode == "z80")
    {
        reads = tracker->GetZ80ReadCountersPtr();
        writes = tracker->GetZ80WriteCountersPtr();
        executes = tracker->GetZ80ExecuteCountersPtr();
        size = 0x10000;
    }
    else
    {
        if (!ParseUnsigned(req->getParameter("page"), page) || page >= MAX_PAGES)
        {
            ReplyBadRequest("physical mode needs page=N, an absolute page index below " + std::to_string(MAX_PAGES),
                            callback, addCors);
            return;
        }
        const size_t base = static_cast<size_t>(page) * PAGE_SIZE;
        reads = tracker->GetPhysReadCountersPtr();
        writes = tracker->GetPhysWriteCountersPtr();
        executes = tracker->GetPhysExecuteCountersPtr();
        if (reads && writes && executes)
        {
            reads += base;
            writes += base;
            executes += base;
        }
        size = PAGE_SIZE;
    }
    if (!reads || !writes || !executes)
    {
        ReplyBadRequest("no counters yet: start a session with POST .../profiler/memory/start", callback, addCors);
        return;
    }

    unsigned long start = 0;
    unsigned long end = size - 1;
    if ((!req->getParameter("start").empty() && !ParseUnsigned(req->getParameter("start"), start)) ||
        (!req->getParameter("end").empty() && !ParseUnsigned(req->getParameter("end"), end)) || start > end ||
        end >= size)
    {
        ReplyBadRequest("start/end must satisfy start <= end < " + std::to_string(size), callback, addCors);
        return;
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["mode"] = mode;
    ret["format"] = format;
    if (mode == "physical")
        ret["page"] = static_cast<Json::UInt>(page);
    ret["start"] = static_cast<Json::UInt>(start);
    ret["end"] = static_cast<Json::UInt>(end);
    ret["session_state"] = sessionStateToString(tracker->GetMemorySessionState());

    if (format == "dense")
    {
        Json::Value r(Json::arrayValue), w(Json::arrayValue), x(Json::arrayValue);
        for (unsigned long a = start; a <= end; a++)
        {
            r.append(reads[a]);
            w.append(writes[a]);
            x.append(executes[a]);
        }
        ret["counters"]["reads"] = r;
        ret["counters"]["writes"] = w;
        ret["counters"]["executes"] = x;
    }
    else
    {
        Json::Value list(Json::arrayValue);
        for (unsigned long a = start; a <= end; a++)
        {
            if (reads[a] == 0 && writes[a] == 0 && executes[a] == 0)
                continue;
            Json::Value entry(Json::arrayValue);
            entry.append(static_cast<Json::UInt>(a));
            entry.append(reads[a]);
            entry.append(writes[a]);
            entry.append(executes[a]);
            list.append(entry);
        }
        ret["addresses"] = list;
        ret["count"] = static_cast<Json::UInt>(list.size());
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/memory/save
/// Body: {"path": "...", "format": "yaml", "single_file": false}
void EmulatorAPI::memoryProfilerSave(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    auto addCors = [](HttpResponsePtr resp) { addCorsHeaders(resp); };
    MemoryAccessTracker* tracker = ResolveMemoryTracker(id, callback, addCors);
    if (!tracker)
        return;

    auto json = req->getJsonObject();
    if (!json || !json->isMember("path") || !(*json)["path"].isString() || (*json)["path"].asString().empty())
    {
        ReplyBadRequest("body needs \"path\"", callback, addCors);
        return;
    }
    const std::string format = json->isMember("format") ? (*json)["format"].asString() : "yaml";
    if (format != "yaml")
    {
        ReplyBadRequest("format must be yaml", callback, addCors);
        return;
    }
    const bool singleFile = json->isMember("single_file") && (*json)["single_file"].asBool();
    const std::string path = (*json)["path"].asString();

    // Missing parent directories are created, as the screen capture does
    {
        std::error_code ec;
        const std::filesystem::path target = FileHelper::ToFsPath(path);
        const std::filesystem::path folder = singleFile ? target.parent_path() : target;
        if (!folder.empty())
            std::filesystem::create_directories(folder, ec);
    }

    const std::string saved = tracker->SaveAccessData(path, format, singleFile);
    if (saved.empty())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "could not write the memory access data";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["saved"] = true;
    ret["path"] = saved;
    ret["format"] = format;
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// ============================================================================
// Call Trace Profiler Endpoints
// ============================================================================

/// @brief POST /api/v1/emulator/{id}/profiler/calltrace/start
void EmulatorAPI::calltraceProfilerStart(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* tracker = getMemoryTracker(context, callback, id);
    if (!tracker) return;

    // Enable feature automatically
    if (context->pFeatureManager)
    {
        context->pFeatureManager->setFeature(Features::kDebugMode, true);
        context->pFeatureManager->setFeature(Features::kCallTrace, true);
        tracker->UpdateFeatureCache();
    }
    tracker->StartCalltraceSession();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "calltrace";
    ret["action"] = "start";
    ret["message"] = "Call trace profiler session started";
    ret["session_state"] = sessionStateToString(tracker->GetCalltraceSessionState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/calltrace/stop
void EmulatorAPI::calltraceProfilerStop(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* tracker = getMemoryTracker(context, callback, id);
    if (!tracker) return;

    tracker->StopCalltraceSession();

    // Finalize: transfer any pinned (hot) events to the cold buffer so the
    // entries/GetAll endpoints see the complete session. Tight loops keep their
    // events hot while capturing and only the frame tick evicts expired ones —
    // stopping inside that window would otherwise hide them from readers
    tracker->GetCallTraceBuffer()->FlushAllHotToCold();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "calltrace";
    ret["action"] = "stop";
    ret["message"] = "Call trace profiler session stopped";
    ret["session_state"] = sessionStateToString(tracker->GetCalltraceSessionState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/calltrace/pause
void EmulatorAPI::calltraceProfilerPause(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* tracker = getMemoryTracker(context, callback, id);
    if (!tracker) return;

    tracker->PauseCalltraceSession();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "calltrace";
    ret["action"] = "pause";
    ret["message"] = "Call trace profiler session paused";
    ret["session_state"] = sessionStateToString(tracker->GetCalltraceSessionState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/calltrace/resume
void EmulatorAPI::calltraceProfilerResume(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* tracker = getMemoryTracker(context, callback, id);
    if (!tracker) return;

    tracker->ResumeCalltraceSession();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "calltrace";
    ret["action"] = "resume";
    ret["message"] = "Call trace profiler session resumed";
    ret["session_state"] = sessionStateToString(tracker->GetCalltraceSessionState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/calltrace/clear
void EmulatorAPI::calltraceProfilerClear(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* tracker = getMemoryTracker(context, callback, id);
    if (!tracker) return;

    tracker->ClearCalltraceData();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "calltrace";
    ret["action"] = "clear";
    ret["message"] = "Call trace profiler data cleared";
    ret["session_state"] = sessionStateToString(tracker->GetCalltraceSessionState());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}


/// @brief GET /api/v1/emulator/{id}/profiler/calltrace/status
void EmulatorAPI::getCalltraceProfilerStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* memory = context && context->pMemory ? context->pMemory : nullptr;
    auto* tracker = memory ? &memory->GetAccessTracker() : nullptr;

    if (!tracker)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory access tracker not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["profiler"] = "calltrace";
    ret["session_state"] = sessionStateToString(tracker->GetCalltraceSessionState());
    ret["capturing"] = tracker->IsCalltraceCapturing();
    
    bool featureEnabled = context->pFeatureManager ? 
        context->pFeatureManager->isEnabled(Features::kCallTrace) : false;
    ret["feature_enabled"] = featureEnabled;
    
    // Include buffer info if available
    auto* calltraceBuffer = tracker->GetCallTraceBuffer();
    if (calltraceBuffer)
    {
        ret["entry_count"] = static_cast<Json::UInt>(calltraceBuffer->GetCount());
        ret["buffer_capacity"] = static_cast<Json::UInt>(calltraceBuffer->GetCapacity());
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/profiler/calltrace/entries
void EmulatorAPI::getCalltraceProfilerEntries(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* memory = context && context->pMemory ? context->pMemory : nullptr;
    auto* tracker = memory ? &memory->GetAccessTracker() : nullptr;

    if (!tracker)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Memory access tracker not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto* calltraceBuffer = tracker->GetCallTraceBuffer();
    if (!calltraceBuffer)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Call trace buffer not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Get count from query params
    // "limit" is accepted as a synonym: the other profiler reads (opcode counters, MCP) use it
    uint32_t count = 100;
    auto countParam = req->getParameter("count");
    if (countParam.empty())
        countParam = req->getParameter("limit");
    if (!countParam.empty())
    {
        try { count = static_cast<uint32_t>(std::stoul(countParam)); }
        catch (...) {}
    }

    auto entries = calltraceBuffer->GetRecentEntries(count);

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["entry_count"] = static_cast<Json::UInt>(entries.size());
    ret["total_count"] = static_cast<Json::UInt>(calltraceBuffer->GetCount());

    Json::Value entriesJson(Json::arrayValue);
    for (const auto& entry : entries)
    {
        Json::Value e;
        e["type"] = static_cast<int>(entry.type);
        e["from_address"] = entry.m1_pc;
        e["to_address"] = entry.target_addr;
        e["sp"] = entry.sp;
        e["loop_count"] = entry.loop_count;
        entriesJson.append(e);
    }
    ret["entries"] = entriesJson;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// ============================================================================
// Unified Profiler Endpoints
// ============================================================================

// Helper to get all profiler components
static bool getUnifiedProfilerComponents(std::shared_ptr<Emulator> emulator, Z80*& z80, MemoryAccessTracker*& tracker, 
                                          OpcodeProfiler*& opcodeProfiler)
{
    auto* context = emulator->GetContext();
    z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
    auto* memory = context && context->pMemory ? context->pMemory : nullptr;
    tracker = memory ? &memory->GetAccessTracker() : nullptr;
    opcodeProfiler = z80 ? z80->GetOpcodeProfiler() : nullptr;
    return tracker != nullptr || opcodeProfiler != nullptr;
}

/// @brief POST /api/v1/emulator/{id}/profiler/start
void EmulatorAPI::unifiedProfilerStart(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    Z80* z80 = nullptr;
    MemoryAccessTracker* tracker = nullptr;
    OpcodeProfiler* opcodeProfiler = nullptr;
    getUnifiedProfilerComponents(emulator, z80, tracker, opcodeProfiler);

    // Enable all features
    if (context->pFeatureManager)
    {
        context->pFeatureManager->setFeature(Features::kDebugMode, true);
        context->pFeatureManager->setFeature(Features::kMemoryTracking, true);
        context->pFeatureManager->setFeature(Features::kCallTrace, true);
        context->pFeatureManager->setFeature(Features::kOpcodeProfiler, true);
        if (tracker) tracker->UpdateFeatureCache();
        if (z80) z80->UpdateFeatureCache();
    }
    
    if (tracker)
    {
        tracker->StartMemorySession();
        tracker->StartCalltraceSession();
    }
    if (opcodeProfiler) opcodeProfiler->Start();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["action"] = "start";
    ret["message"] = "All profiler sessions started";
    
    Json::Value status;
    if (tracker)
    {
        status["memory"] = sessionStateToString(tracker->GetMemorySessionState());
        status["calltrace"] = sessionStateToString(tracker->GetCalltraceSessionState());
    }
    if (opcodeProfiler)
    {
        status["opcode"] = sessionStateToString(opcodeProfiler->GetSessionState());
    }
    ret["status"] = status;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/stop
void EmulatorAPI::unifiedProfilerStop(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    Z80* z80 = nullptr;
    MemoryAccessTracker* tracker = nullptr;
    OpcodeProfiler* opcodeProfiler = nullptr;
    getUnifiedProfilerComponents(emulator, z80, tracker, opcodeProfiler);

    if (tracker)
    {
        tracker->StopMemorySession();
        tracker->StopCalltraceSession();
    }
    if (opcodeProfiler) opcodeProfiler->Stop();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["action"] = "stop";
    ret["message"] = "All profiler sessions stopped";
    
    Json::Value status;
    if (tracker)
    {
        status["memory"] = sessionStateToString(tracker->GetMemorySessionState());
        status["calltrace"] = sessionStateToString(tracker->GetCalltraceSessionState());
    }
    if (opcodeProfiler)
    {
        status["opcode"] = sessionStateToString(opcodeProfiler->GetSessionState());
    }
    ret["status"] = status;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/pause
void EmulatorAPI::unifiedProfilerPause(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    Z80* z80 = nullptr;
    MemoryAccessTracker* tracker = nullptr;
    OpcodeProfiler* opcodeProfiler = nullptr;
    getUnifiedProfilerComponents(emulator, z80, tracker, opcodeProfiler);

    if (tracker)
    {
        tracker->PauseMemorySession();
        tracker->PauseCalltraceSession();
    }
    if (opcodeProfiler) opcodeProfiler->Pause();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["action"] = "pause";
    ret["message"] = "All profiler sessions paused";
    
    Json::Value status;
    if (tracker)
    {
        status["memory"] = sessionStateToString(tracker->GetMemorySessionState());
        status["calltrace"] = sessionStateToString(tracker->GetCalltraceSessionState());
    }
    if (opcodeProfiler)
    {
        status["opcode"] = sessionStateToString(opcodeProfiler->GetSessionState());
    }
    ret["status"] = status;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/resume
void EmulatorAPI::unifiedProfilerResume(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    Z80* z80 = nullptr;
    MemoryAccessTracker* tracker = nullptr;
    OpcodeProfiler* opcodeProfiler = nullptr;
    getUnifiedProfilerComponents(emulator, z80, tracker, opcodeProfiler);

    if (tracker)
    {
        tracker->ResumeMemorySession();
        tracker->ResumeCalltraceSession();
    }
    if (opcodeProfiler) opcodeProfiler->Resume();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["action"] = "resume";
    ret["message"] = "All profiler sessions resumed";
    
    Json::Value status;
    if (tracker)
    {
        status["memory"] = sessionStateToString(tracker->GetMemorySessionState());
        status["calltrace"] = sessionStateToString(tracker->GetCalltraceSessionState());
    }
    if (opcodeProfiler)
    {
        status["opcode"] = sessionStateToString(opcodeProfiler->GetSessionState());
    }
    ret["status"] = status;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/profiler/clear
void EmulatorAPI::unifiedProfilerClear(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    Z80* z80 = nullptr;
    MemoryAccessTracker* tracker = nullptr;
    OpcodeProfiler* opcodeProfiler = nullptr;
    getUnifiedProfilerComponents(emulator, z80, tracker, opcodeProfiler);

    if (tracker)
    {
        tracker->ClearMemoryData();
        tracker->ClearCalltraceData();
    }
    if (opcodeProfiler) opcodeProfiler->Clear();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["action"] = "clear";
    ret["message"] = "All profiler data cleared";
    
    Json::Value status;
    if (tracker)
    {
        status["memory"] = sessionStateToString(tracker->GetMemorySessionState());
        status["calltrace"] = sessionStateToString(tracker->GetCalltraceSessionState());
    }
    if (opcodeProfiler)
    {
        status["opcode"] = sessionStateToString(opcodeProfiler->GetSessionState());
    }
    ret["status"] = status;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/profiler/status
void EmulatorAPI::getUnifiedProfilerStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    auto* memory = context && context->pMemory ? context->pMemory : nullptr;
    auto* tracker = memory ? &memory->GetAccessTracker() : nullptr;
    auto* opcodeProfiler = z80 ? z80->GetOpcodeProfiler() : nullptr;

    Json::Value ret;
    ret["emulator_id"] = id;

    Json::Value profilers(Json::objectValue);
    
    // Memory profiler status
    if (tracker)
    {
        Json::Value memStatus;
        memStatus["session_state"] = sessionStateToString(tracker->GetMemorySessionState());
        memStatus["capturing"] = tracker->IsMemoryCapturing();
        memStatus["feature_enabled"] = context->pFeatureManager ? 
            context->pFeatureManager->isEnabled(Features::kMemoryTracking) : false;
        profilers["memory"] = memStatus;
        
        // Call trace status
        Json::Value ctStatus;
        ctStatus["session_state"] = sessionStateToString(tracker->GetCalltraceSessionState());
        ctStatus["capturing"] = tracker->IsCalltraceCapturing();
        ctStatus["feature_enabled"] = context->pFeatureManager ? 
            context->pFeatureManager->isEnabled(Features::kCallTrace) : false;
        auto* calltraceBuffer = tracker->GetCallTraceBuffer();
        if (calltraceBuffer)
        {
            ctStatus["entry_count"] = static_cast<Json::UInt>(calltraceBuffer->GetCount());
        }
        profilers["calltrace"] = ctStatus;
    }
    
    // Opcode profiler status
    if (opcodeProfiler)
    {
        auto status = opcodeProfiler->GetStatus();
        Json::Value opStatus;
        opStatus["session_state"] = sessionStateToString(opcodeProfiler->GetSessionState());
        opStatus["capturing"] = status.capturing;
        opStatus["total_executions"] = static_cast<Json::UInt64>(status.totalExecutions);
        opStatus["trace_size"] = status.traceSize;
        opStatus["feature_enabled"] = context->pFeatureManager ? 
            context->pFeatureManager->isEnabled(Features::kOpcodeProfiler) : false;
        profilers["opcode"] = opStatus;
    }

    ret["profilers"] = profilers;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/frame_cost
/// @brief Work-vs-idle t-state accounting per frame. Halted (HALT bus) t-states
///        are counted in Z80::Z80Step and rolled up per frame in
///        Core::AdjustFrameCounters; active cost is the frame budget minus halted.
void EmulatorAPI::getFrameCost(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Unable to access emulator context";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const CONFIG& config = context->config;
    const EmulatorState& state = context->emulatorState;

    const uint64_t frameBudget = static_cast<uint64_t>(config.frame) * state.current_z80_frequency_multiplier;

    // Last completed frame
    const uint64_t lastHalted = state.tstates_halted_last;
    const uint64_t lastActive = frameBudget > lastHalted ? frameBudget - lastHalted : 0;

    Json::Value last;
    last["tstates_total"] = frameBudget;
    last["tstates_halted"] = lastHalted;
    last["tstates_active"] = lastActive;
    last["halted_percent"] = frameBudget ? lastHalted * 100.0 / frameBudget : 0.0;
    last["active_percent"] = frameBudget ? lastActive * 100.0 / frameBudget : 0.0;

    // Session averages over all accounted frames
    const uint64_t frames = state.frame_cost_frames;
    Json::Value average;
    average["frames"] = static_cast<Json::UInt64>(frames);
    average["tstates_total"] = static_cast<Json::UInt64>(state.tstates_frame_total);
    average["tstates_halted"] = static_cast<Json::UInt64>(state.tstates_halted_total);
    average["tstates_active"] = static_cast<Json::UInt64>(state.tstates_frame_total - state.tstates_halted_total);
    average["halted_percent"] =
        state.tstates_frame_total ? state.tstates_halted_total * 100.0 / state.tstates_frame_total : 0.0;
    average["active_percent"] =
        state.tstates_frame_total
            ? (state.tstates_frame_total - state.tstates_halted_total) * 100.0 / state.tstates_frame_total
            : 0.0;

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["frame"] = static_cast<Json::UInt64>(state.frame_counter);
    ret["frame_tstates"] = frameBudget;
    ret["tstates_per_line"] = config.t_line;
    ret["frames_per_second"] = config.intfq;
    ret["last_frame"] = last;
    ret["average"] = average;
    ret["current_frame_halted"] = state.tstates_halted_current;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api

