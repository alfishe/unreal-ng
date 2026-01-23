// WebAPI Analyzer Management Implementation
// Implements /analyzers endpoints - 2026-01-21

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/emulatorcontext.h>
#include <debugger/debugmanager.h>
#include <debugger/analyzers/analyzermanager.h>
#include <debugger/analyzers/trdos/trdosanalyzer.h>
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

/// @brief GET /api/v1/emulator/{id}/analyzers
/// @brief Get all registered analyzers for an emulator
void EmulatorAPI::getAnalyzers(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pDebugManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Debug manager not available for this emulator";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    AnalyzerManager* analyzerManager = context->pDebugManager->GetAnalyzerManager();
    if (!analyzerManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Analyzer manager not initialized";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    Json::Value analyzers(Json::arrayValue);

    for (const auto& name : analyzerManager->getRegisteredAnalyzers())
    {
        Json::Value analyzer(Json::objectValue);
        analyzer["id"] = name;
        analyzer["enabled"] = analyzerManager->isActive(name);
        analyzers.append(analyzer);
    }

    ret["emulator_id"] = id;
    ret["analyzers"] = analyzers;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/analyzer/{name}
/// @brief Get a specific analyzer status
void EmulatorAPI::getAnalyzer(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id, const std::string& name) const
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
    AnalyzerManager* analyzerManager = context && context->pDebugManager 
        ? context->pDebugManager->GetAnalyzerManager() : nullptr;

    if (!analyzerManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Analyzer manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (!analyzerManager->hasAnalyzer(name))
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown analyzer: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["analyzer_id"] = name;
    ret["enabled"] = analyzerManager->isActive(name);

    // TRDOSAnalyzer specific stats
    if (name == "trdos")
    {
        TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(analyzerManager->getAnalyzer(name));
        if (trdos)
        {
            std::string stateStr;
            switch (trdos->getState())
            {
                case TRDOSAnalyzerState::IDLE: stateStr = "IDLE"; break;
                case TRDOSAnalyzerState::IN_TRDOS: stateStr = "IN_TRDOS"; break;
                case TRDOSAnalyzerState::IN_COMMAND: stateStr = "IN_COMMAND"; break;
                case TRDOSAnalyzerState::IN_SECTOR_OP: stateStr = "IN_SECTOR_OP"; break;
                case TRDOSAnalyzerState::IN_CUSTOM: stateStr = "IN_CUSTOM"; break;
                default: stateStr = "UNKNOWN"; break;
            }
            ret["state"] = stateStr;
            ret["event_count"] = static_cast<Json::UInt64>(trdos->getEventCount());
            ret["total_produced"] = static_cast<Json::UInt64>(trdos->getTotalEventsProduced());
            ret["total_evicted"] = static_cast<Json::UInt64>(trdos->getTotalEventsEvicted());
        }
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT/POST /api/v1/emulator/{id}/analyzer/{name}
/// @brief Enable or disable an analyzer
void EmulatorAPI::setAnalyzer(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id, const std::string& name) const
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
    AnalyzerManager* analyzerManager = context && context->pDebugManager 
        ? context->pDebugManager->GetAnalyzerManager() : nullptr;

    if (!analyzerManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Analyzer manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (!analyzerManager->hasAnalyzer(name))
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown analyzer: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse request body
    auto json = req->getJsonObject();
    if (!json || !json->isMember("enabled"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'enabled' field in request body";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    bool enabled = (*json)["enabled"].asBool();

    bool success;
    if (enabled)
    {
        success = analyzerManager->activate(name);
    }
    else
    {
        success = analyzerManager->deactivate(name);
    }

    if (!success && enabled)
    {
        // Deactivate always succeeds if analyzer exists, but activate can fail
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Failed to activate analyzer";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["analyzer_id"] = name;
    ret["enabled"] = enabled;
    ret["message"] = std::string("Analyzer '") + name + "' " + (enabled ? "enabled" : "disabled");

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/analyzer/{name}/events
/// @brief Get captured events from an analyzer
void EmulatorAPI::getAnalyzerEvents(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id, const std::string& name) const
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
    AnalyzerManager* analyzerManager = context && context->pDebugManager 
        ? context->pDebugManager->GetAnalyzerManager() : nullptr;

    if (!analyzerManager || !analyzerManager->hasAnalyzer(name))
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown analyzer: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse limit parameter
    size_t limit = 100;  // Default
    auto limitParam = req->getParameter("limit");
    if (!limitParam.empty())
    {
        try
        {
            limit = std::stoul(limitParam);
        }
        catch (...)
        {
            // Keep default
        }
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["analyzer_id"] = name;

    // TRDOSAnalyzer specific event handling
    if (name == "trdos")
    {
        TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(analyzerManager->getAnalyzer(name));
        if (trdos)
        {
            auto events = trdos->getEvents();
            Json::Value eventsJson(Json::arrayValue);

            size_t start = (events.size() > limit) ? events.size() - limit : 0;
            for (size_t i = start; i < events.size(); i++)
            {
                Json::Value ev;
                ev["timestamp"] = static_cast<Json::UInt64>(events[i].timestamp);
                ev["type"] = static_cast<int>(events[i].type);
                ev["formatted"] = events[i].format();
                
                // Core fidelity fields
                ev["frame_number"] = static_cast<Json::UInt64>(events[i].frameNumber);
                ev["flags"] = static_cast<int>(events[i].flags);
                
                // Context fields
                Json::Value ctx;
                ctx["pc"] = static_cast<int>(events[i].context.pc);
                if (events[i].context.callerAddress != 0) {
                    ctx["caller"] = static_cast<int>(events[i].context.callerAddress);
                }
                if (events[i].context.originalRAMCaller != 0) {
                    ctx["original_caller"] = static_cast<int>(events[i].context.originalRAMCaller);
                }
                ctx["iff1"] = static_cast<int>(events[i].context.iff1);
                ctx["im"] = static_cast<int>(events[i].context.im);
                ev["context"] = ctx;

                // Legacy/Direct fields
                if (events[i].track != 0xFF)
                {
                    ev["track"] = events[i].track;
                }
                if (events[i].sector != 0xFF)
                {
                    ev["sector"] = events[i].sector;
                }
                if (events[i].bytesTransferred > 0)
                {
                    ev["bytes_transferred"] = events[i].bytesTransferred;
                }
                if (!events[i].filename.empty())
                {
                    ev["filename"] = events[i].filename;
                }
                
                // Command type for COMMAND_START events
                if (events[i].type == TRDOSEventType::COMMAND_START ||
                    events[i].type == TRDOSEventType::COMMAND_COMPLETE)
                {
                    ev["command"] = static_cast<int>(events[i].command);
                }
                
                // Add FDC state info
                ev["fdc_status"] = static_cast<int>(events[i].fdcStatus);
                ev["fdc_cmd_reg"] = static_cast<int>(events[i].fdcCommand);

                eventsJson.append(ev);
            }

            ret["events"] = eventsJson;
            ret["total_events"] = static_cast<Json::UInt64>(events.size());
            ret["showing"] = static_cast<Json::UInt64>(eventsJson.size());
        }
    }
    else
    {
        ret["events"] = Json::Value(Json::arrayValue);
        ret["message"] = "Events not implemented for this analyzer";
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief DELETE /api/v1/emulator/{id}/analyzer/{name}/events
/// @brief Clear events from an analyzer
void EmulatorAPI::clearAnalyzerEvents(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                       const std::string& id, const std::string& name) const
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
    AnalyzerManager* analyzerManager = context && context->pDebugManager 
        ? context->pDebugManager->GetAnalyzerManager() : nullptr;

    if (!analyzerManager || !analyzerManager->hasAnalyzer(name))
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown analyzer: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // TRDOSAnalyzer specific clear
    if (name == "trdos")
    {
        TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(analyzerManager->getAnalyzer(name));
        if (trdos)
        {
            trdos->clear();
        }
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["analyzer_id"] = name;
    ret["message"] = "Events cleared";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/analyzer/{name}/session
/// @brief Control analyzer capture session (activate, deactivate, pause, resume)
void EmulatorAPI::analyzerSession(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id, const std::string& name) const
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
    AnalyzerManager* analyzerManager = context && context->pDebugManager 
        ? context->pDebugManager->GetAnalyzerManager() : nullptr;

    if (!analyzerManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Analyzer manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (!analyzerManager->hasAnalyzer(name))
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown analyzer: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
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
        error["message"] = "Missing 'action' field in request body (expected: activate, deactivate, pause, resume)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string action = (*json)["action"].asString();
    bool success = false;
    std::string message;

    if (action == "activate" || action == "start")
    {
        success = analyzerManager->activate(name);
        
        // Clear buffers on activation for fresh session
        if (success && name == "trdos")
        {
            TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(analyzerManager->getAnalyzer(name));
            if (trdos)
            {
                trdos->clear();
            }
        }
        message = success ? "Session activated" : "Failed to activate session";
    }
    else if (action == "deactivate" || action == "stop")
    {
        success = analyzerManager->deactivate(name);
        message = "Session deactivated";
    }
    else
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid action: " + action + " (expected: activate, deactivate)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (!success && action == "activate")
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = message;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["analyzer_id"] = name;
    ret["action"] = action;
    ret["success"] = success;
    ret["message"] = message;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/analyzer/{name}/raw/fdc
/// @brief Get raw FDC events from an analyzer
void EmulatorAPI::getAnalyzerRawFDC(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id, const std::string& name) const
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
    AnalyzerManager* analyzerManager = context && context->pDebugManager 
        ? context->pDebugManager->GetAnalyzerManager() : nullptr;

    if (!analyzerManager || !analyzerManager->hasAnalyzer(name))
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown analyzer: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse limit parameter
    size_t limit = 100;  // Default
    auto limitParam = req->getParameter("limit");
    if (!limitParam.empty())
    {
        try
        {
            limit = std::stoul(limitParam);
        }
        catch (...)
        {
            // Keep default
        }
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["analyzer_id"] = name;

    // TRDOSAnalyzer specific raw FDC event handling
    if (name == "trdos")
    {
        TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(analyzerManager->getAnalyzer(name));
        if (trdos)
        {
            auto events = trdos->getRawFDCEvents();
            Json::Value eventsJson(Json::arrayValue);

            size_t start = (events.size() > limit) ? events.size() - limit : 0;
            for (size_t i = start; i < events.size(); i++)
            {
                const auto& ev = events[i];
                Json::Value jsonEv;
                
                // Timing
                jsonEv["tstate"] = static_cast<Json::UInt64>(ev.tstate);
                jsonEv["frame_number"] = static_cast<Json::UInt>(ev.frameNumber);
                
                // FDC registers
                jsonEv["command_reg"] = static_cast<unsigned>(ev.commandReg);
                jsonEv["status_reg"] = static_cast<unsigned>(ev.statusReg);
                jsonEv["track_reg"] = static_cast<unsigned>(ev.trackReg);
                jsonEv["sector_reg"] = static_cast<unsigned>(ev.sectorReg);
                jsonEv["data_reg"] = static_cast<unsigned>(ev.dataReg);
                jsonEv["system_reg"] = static_cast<unsigned>(ev.systemReg);
                
                // Z80 context
                jsonEv["pc"] = ev.pc;
                jsonEv["sp"] = ev.sp;
                
                // Main registers
                jsonEv["af"] = static_cast<unsigned>((ev.a << 8) | ev.f);
                jsonEv["bc"] = static_cast<unsigned>((ev.b << 8) | ev.c);
                jsonEv["de"] = static_cast<unsigned>((ev.d << 8) | ev.e);
                jsonEv["hl"] = static_cast<unsigned>((ev.h << 8) | ev.l);
                
                // Interrupt/Mode registers
                jsonEv["iff1"] = static_cast<unsigned>(ev.iff1);
                jsonEv["iff2"] = static_cast<unsigned>(ev.iff2);
                jsonEv["im"] = static_cast<unsigned>(ev.im);
                
                // Stack snapshot
                Json::Value stack(Json::arrayValue);
                for (size_t j = 0; j < 16; ++j)
                {
                    stack.append(static_cast<unsigned>(ev.stack[j]));
                }
                jsonEv["stack"] = stack;

                eventsJson.append(jsonEv);
            }

            ret["events"] = eventsJson;
            ret["total_events"] = static_cast<Json::UInt64>(events.size());
            ret["showing"] = static_cast<Json::UInt64>(eventsJson.size());
        }
    }
    else
    {
        ret["events"] = Json::Value(Json::arrayValue);
        ret["message"] = "Raw FDC events not supported for this analyzer";
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/analyzer/{name}/raw/breakpoints
/// @brief Get raw breakpoint events from an analyzer
void EmulatorAPI::getAnalyzerRawBreakpoints(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                            const std::string& id, const std::string& name) const
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
    AnalyzerManager* analyzerManager = context && context->pDebugManager 
        ? context->pDebugManager->GetAnalyzerManager() : nullptr;

    if (!analyzerManager || !analyzerManager->hasAnalyzer(name))
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown analyzer: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse limit parameter
    size_t limit = 100;  // Default
    auto limitParam = req->getParameter("limit");
    if (!limitParam.empty())
    {
        try
        {
            limit = std::stoul(limitParam);
        }
        catch (...)
        {
            // Keep default
        }
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["analyzer_id"] = name;

    // TRDOSAnalyzer specific raw breakpoint event handling
    if (name == "trdos")
    {
        TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(analyzerManager->getAnalyzer(name));
        if (trdos)
        {
            auto events = trdos->getRawBreakpointEvents();
            Json::Value eventsJson(Json::arrayValue);

            size_t start = (events.size() > limit) ? events.size() - limit : 0;
            for (size_t i = start; i < events.size(); i++)
            {
                const auto& ev = events[i];
                Json::Value jsonEv;
                
                // Timing
                jsonEv["tstate"] = static_cast<Json::UInt64>(ev.tstate);
                jsonEv["frame_number"] = static_cast<Json::UInt>(ev.frameNumber);
                
                // Breakpoint info
                jsonEv["address"] = ev.address;
                
                // Z80 context
                jsonEv["pc"] = ev.pc;
                jsonEv["sp"] = ev.sp;
                
                // Main registers
                jsonEv["af"] = ev.af;
                jsonEv["bc"] = ev.bc;
                jsonEv["de"] = ev.de;
                jsonEv["hl"] = ev.hl;
                
                // Alternate registers
                jsonEv["af_"] = ev.af_;
                jsonEv["bc_"] = ev.bc_;
                jsonEv["de_"] = ev.de_;
                jsonEv["hl_"] = ev.hl_;
                
                // Index registers
                jsonEv["ix"] = ev.ix;
                jsonEv["iy"] = ev.iy;
                
                // Special registers
                jsonEv["i"] = static_cast<unsigned>(ev.i);
                jsonEv["r"] = ev.r;
                
                jsonEv["iff1"] = static_cast<unsigned>(ev.iff1);
                jsonEv["iff2"] = static_cast<unsigned>(ev.iff2);
                jsonEv["im"] = static_cast<unsigned>(ev.im);
                
                // Stack snapshot
                Json::Value stack(Json::arrayValue);
                for (size_t j = 0; j < 16; ++j)
                {
                    stack.append(static_cast<unsigned>(ev.stack[j]));
                }
                jsonEv["stack"] = stack;

                eventsJson.append(jsonEv);
            }

            ret["events"] = eventsJson;
            ret["total_events"] = static_cast<Json::UInt64>(events.size());
            ret["showing"] = static_cast<Json::UInt64>(eventsJson.size());
        }
    }
    else
    {
        ret["events"] = Json::Value(Json::arrayValue);
        ret["message"] = "Raw breakpoint events not supported for this analyzer";
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
