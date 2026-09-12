// WebAPI Analyzer Management Implementation
// Implements /analyzers endpoints - 2026-01-21

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/emulatorcontext.h>
#include <debugger/debugmanager.h>
#include <debugger/analyzers/analyzermanager.h>
#include <debugger/analyzers/audiocapture/audiocaptureanalyzer.h>
#include <debugger/analyzers/aylog/ayloganalyzer.h>
#include <debugger/analyzers/coverage/coverageanalyzer.h>
#include <debugger/analyzers/trdos/trdosanalyzer.h>
#include <emulator/sound/soundmanager.h>
#include <json/json.h>

#include <cmath>
#include <cctype>
#include <ctime>
#include <atomic>
#include <filesystem>
#include <string>

#include "../emulator_api.h"
#include <emulator/io/porttracker.h>
#include <debugger/analyzers/memory-region/memoryregionanalyzer.h>
#include <base/featuremanager.h>

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
                    // Low-level disk service code (C register at $3D13)
                    ev["service"] = static_cast<int>(events[i].service);
                    // User command (BASIC token at CH_ADD from $3D1A)
                    ev["user_command"] = static_cast<int>(events[i].userCommand);
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
                
                // Breakpoint info with label and page context
                jsonEv["address"] = ev.address;
                if (!ev.address_label.empty())
                {
                    jsonEv["address_label"] = ev.address_label;
                }
                jsonEv["page_type"] = ev.page_type;
                jsonEv["page_index"] = static_cast<unsigned>(ev.page_index);
                jsonEv["page_offset"] = ev.page_offset;
                
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

/// region <Coverage analyzer endpoints>

namespace
{
/// Resolve the built-in coverage analyzer for an emulator (nullptr on any missing link)
CoverageAnalyzer* findCoverageAnalyzer(Emulator* emulator, AnalyzerManager** outManager = nullptr)
{
    if (!emulator)
        return nullptr;

    auto* context = emulator->GetContext();
    if (!context || !context->pDebugManager)
        return nullptr;

    AnalyzerManager* analyzerManager = context->pDebugManager->GetAnalyzerManager();
    if (!analyzerManager)
        return nullptr;

    if (outManager)
        *outManager = analyzerManager;

    return analyzerManager->getAnalyzer<CoverageAnalyzer>("coverage");
}

/// Parse an address query parameter (decimal or 0x-prefixed hex)
bool parseAddressParam(const std::string& value, uint16_t& out)
{
    try
    {
        unsigned long parsed;
        if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0)
            parsed = std::stoul(value, nullptr, 16);
        else
            parsed = std::stoul(value);

        if (parsed > 0xFFFF)
            return false;

        out = static_cast<uint16_t>(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

/// Pause the emulator around subscription mutations (race-free activation)
struct ScopedPause
{
    Emulator* emulator;
    bool wasRunning;

    explicit ScopedPause(Emulator* emu)
        : emulator(emu), wasRunning(emu && emu->IsRunning() && !emu->IsPaused())
    {
        if (wasRunning)
        {
            emulator->Pause(false);
            emulator->WaitForPauseConfirmation(1000);
        }
    }

    ~ScopedPause()
    {
        if (wasRunning)
            emulator->Resume(false);
    }
};
}  // namespace

/// @brief POST /api/v1/emulator/{id}/coverage/start
/// @brief Activate the coverage analyzer. Optional body: {"clear": true} (default true)
void EmulatorAPI::startCoverage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    AnalyzerManager* analyzerManager = nullptr;
    CoverageAnalyzer* coverage = findCoverageAnalyzer(emulator.get(), &analyzerManager);
    if (!coverage || !analyzerManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Coverage analyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Optional body: {"clear": true} — clear recorded data on start (default)
    bool doClear = true;
    auto json = req->getJsonObject();
    if (json && json->isMember("clear"))
    {
        doClear = (*json)["clear"].asBool();
    }

    // Mutate analyzer subscriptions only while the emulation thread is parked
    ScopedPause pause(emulator.get());

    if (doClear)
    {
        coverage->clear();
    }

    bool activated = analyzerManager->activate("coverage");

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["success"] = activated;
    ret["active"] = analyzerManager->isActive("coverage");
    ret["recording"] = coverage->isRecording();
    ret["cleared"] = doClear;
    ret["message"] = activated ? "Coverage session started" : "Failed to activate coverage analyzer";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/coverage/stop
/// @brief Deactivate the coverage analyzer. Recorded data is retained for reading
void EmulatorAPI::stopCoverage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    AnalyzerManager* analyzerManager = nullptr;
    CoverageAnalyzer* coverage = findCoverageAnalyzer(emulator.get(), &analyzerManager);
    if (!coverage || !analyzerManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Coverage analyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Mutate analyzer subscriptions only while the emulation thread is parked
    ScopedPause pause(emulator.get());

    bool deactivated = analyzerManager->deactivate("coverage");

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["success"] = deactivated;
    ret["active"] = analyzerManager->isActive("coverage");
    ret["recording"] = coverage->isRecording();
    ret["executed_count"] = static_cast<Json::UInt64>(coverage->getExecutedCount());
    ret["instructions"] = static_cast<Json::UInt64>(coverage->getInstructionCount());
    ret["message"] = deactivated ? "Coverage session stopped (data retained)" : "Coverage session was not active";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/coverage/clear
/// @brief Reset recorded coverage data (best effort: pause the emulator first for a clean reset)
void EmulatorAPI::clearCoverage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    CoverageAnalyzer* coverage = findCoverageAnalyzer(emulator.get());
    if (!coverage)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Coverage analyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ScopedPause pause(emulator.get());
    coverage->clear();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["success"] = true;
    ret["executed_count"] = 0;
    ret["message"] = "Coverage data cleared";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/coverage?max_ranges=512
/// @brief Read the recorded executed-address coverage
void EmulatorAPI::getCoverage(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    AnalyzerManager* analyzerManager = nullptr;
    CoverageAnalyzer* coverage = findCoverageAnalyzer(emulator.get(), &analyzerManager);
    if (!coverage || !analyzerManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Coverage analyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Optional ?max_ranges= bound on the returned range list (0 = unlimited)
    size_t maxRanges = 512;
    auto params = req->getParameters();
    auto it = params.find("max_ranges");
    if (it != params.end())
    {
        try
        {
            maxRanges = static_cast<size_t>(std::stoul(it->second));
        }
        catch (...)
        {
            maxRanges = 512;
        }
    }

    const size_t executedCount = coverage->getExecutedCount();
    const bool active = analyzerManager->isActive("coverage");

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["active"] = active;
    ret["recording"] = coverage->isRecording();
    ret["executed_count"] = static_cast<Json::UInt64>(executedCount);
    ret["total_addresses"] = 65536;
    ret["coverage_percent"] = executedCount * 100.0 / 65536.0;
    ret["instructions"] = static_cast<Json::UInt64>(coverage->getInstructionCount());

    auto ranges = coverage->getExecutedRanges(maxRanges);
    Json::Value rangesJson(Json::arrayValue);
    for (const auto& range : ranges)
    {
        Json::Value item;
        item["start"] = StringHelper::Format("0x%04X", range.first);
        item["end"] = StringHelper::Format("0x%04X", range.second);
        item["size"] = static_cast<Json::UInt64>(range.second - range.first + 1);
        rangesJson.append(item);
    }
    ret["ranges"] = rangesJson;
    ret["range_count"] = static_cast<Json::UInt64>(ranges.size());
    ret["truncated"] = (maxRanges != 0 && ranges.size() >= maxRanges);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/coverage/gaps?start=0x4000&end=0xFFFF&max_gaps=256
/// @brief Gap analysis: address ranges never executed within [start, end]
void EmulatorAPI::getCoverageGaps(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    AnalyzerManager* analyzerManager = nullptr;
    CoverageAnalyzer* coverage = findCoverageAnalyzer(emulator.get(), &analyzerManager);
    if (!coverage || !analyzerManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Coverage analyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Window bounds: ?start= and ?end= (hex or decimal); gaps inside RAM by default
    uint16_t start = 0x4000;
    uint16_t end = 0xFFFF;
    size_t maxGaps = 256;

    auto params = req->getParameters();
    auto startIt = params.find("start");
    if (startIt != params.end() && !parseAddressParam(startIt->second, start))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid 'start' parameter (expected hex or decimal address)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto endIt = params.find("end");
    if (endIt != params.end() && !parseAddressParam(endIt->second, end))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid 'end' parameter (expected hex or decimal address)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (start > end)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "'start' must not exceed 'end'";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto gapsIt = params.find("max_gaps");
    if (gapsIt != params.end())
    {
        try
        {
            maxGaps = static_cast<size_t>(std::stoul(gapsIt->second));
        }
        catch (...)
        {
            maxGaps = 256;
        }
    }

    const size_t executedInWindow = coverage->getExecutedCountInRange(start, end);
    const size_t windowSize = static_cast<size_t>(end) - start + 1;

    auto gaps = coverage->getGaps(start, end, maxGaps);
    Json::Value gapsJson(Json::arrayValue);
    for (const auto& gap : gaps)
    {
        Json::Value item;
        item["start"] = StringHelper::Format("0x%04X", gap.first);
        item["end"] = StringHelper::Format("0x%04X", gap.second);
        item["size"] = static_cast<Json::UInt64>(gap.second - gap.first + 1);
        gapsJson.append(item);
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["active"] = analyzerManager->isActive("coverage");
    Json::Value window;
    window["start"] = StringHelper::Format("0x%04X", start);
    window["end"] = StringHelper::Format("0x%04X", end);
    window["size"] = static_cast<Json::UInt64>(windowSize);
    ret["window"] = window;
    ret["executed_in_window"] = static_cast<Json::UInt64>(executedInWindow);
    ret["coverage_percent"] = executedInWindow * 100.0 / windowSize;
    ret["gaps"] = gapsJson;
    ret["gap_count"] = static_cast<Json::UInt64>(gaps.size());
    ret["truncated"] = (maxGaps != 0 && gaps.size() >= maxGaps);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// endregion </Coverage analyzer endpoints>

/// region <AY log endpoints (MCP automation)>

namespace
{
/// Resolve the built-in AY log analyzer for an emulator (nullptr on any missing link)
AYLogAnalyzer* findAYLogAnalyzer(Emulator* emulator, AnalyzerManager** outManager = nullptr)
{
    if (!emulator)
        return nullptr;

    auto* context = emulator->GetContext();
    if (!context || !context->pDebugManager)
        return nullptr;

    AnalyzerManager* analyzerManager = context->pDebugManager->GetAnalyzerManager();
    if (!analyzerManager)
        return nullptr;

    if (outManager)
        *outManager = analyzerManager;

    return analyzerManager->getAnalyzer<AYLogAnalyzer>("aylog");
}

/// Resolve the built-in audio capture analyzer (nullptr on any missing link)
AudioCaptureAnalyzer* findAudioCaptureAnalyzer(Emulator* emulator, AnalyzerManager** outManager = nullptr)
{
    if (!emulator)
        return nullptr;

    auto* context = emulator->GetContext();
    if (!context || !context->pDebugManager)
        return nullptr;

    AnalyzerManager* analyzerManager = context->pDebugManager->GetAnalyzerManager();
    if (!analyzerManager)
        return nullptr;

    if (outManager)
        *outManager = analyzerManager;

    return analyzerManager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture");
}

/// Core audio rate for an emulator (44100 fallback when the sound stack is absent)
size_t audioCoreRate(Emulator* emulator)
{
    auto* context = emulator ? emulator->GetContext() : nullptr;
    return context && context->pSoundManager ? context->pSoundManager->getCoreRate() : 44100;
}

/// Write an interleaved int16 stereo capture to a timestamped WAV under the
/// OS temp dir (unreal-mcp/). Returns the written path or "" on failure.
std::string writeCaptureWav(const std::string& emulatorId, const std::vector<int16_t>& buffer, size_t frames,
                            uint32_t rate)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path directory = fs::temp_directory_path(ec) / "unreal-mcp";
    if (ec)
        return "";
    fs::create_directories(directory, ec);
    if (ec)
        return "";

    // Defensive id sanitization for the filename (ids are UUID-like already)
    std::string safeId;
    for (char c : emulatorId)
    {
        safeId += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    }
    if (safeId.empty())
        safeId = "emulator";

    static std::atomic<uint64_t> counter{0};
    const uint64_t stamp = static_cast<uint64_t>(std::time(nullptr)) * 1000 + counter++;
    fs::path path = directory / ("audio-" + safeId + "-" + std::to_string(stamp) + ".wav");

    TinyWav wav{};
    if (tinywav_open_write(&wav, 2, static_cast<int32_t>(rate), TW_INT16, TW_INTERLEAVED, path.string().c_str()) != 0)
        return "";

    // tinywav writes FROM the buffer — the const_cast is safe
    tinywav_write_i(&wav, const_cast<void*>(static_cast<const void*>(buffer.data())), static_cast<int>(frames));
    tinywav_close_write(&wav);

    return path.string();
}
}  // namespace

/// @brief POST /api/v1/emulator/{id}/ay/log
/// @brief Manage the AY register-write log session. Body: {"action":"start|stop|clear", "capacity":4096}
void EmulatorAPI::ayLog(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    AnalyzerManager* analyzerManager = nullptr;
    AYLogAnalyzer* aylog = findAYLogAnalyzer(emulator.get(), &analyzerManager);
    if (!aylog || !analyzerManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "AY log analyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string action = "start";
    auto json = req->getJsonObject();
    if (json && json->isMember("action"))
    {
        action = (*json)["action"].asString();
    }

    if (action == "start")
    {
        // Optional ring capacity (applies before activation; clears data)
        if (json && json->isMember("capacity"))
        {
            aylog->setCapacity(static_cast<size_t>((*json)["capacity"].asUInt64()));
        }

        // Mutate analyzer subscriptions only while the emulation thread is parked
        ScopedPause pause(emulator.get());
        bool activated = analyzerManager->activate("aylog");

        Json::Value ret;
        ret["emulator_id"] = id;
        ret["success"] = activated;
        ret["active"] = analyzerManager->isActive("aylog");
        ret["recording"] = aylog->isRecording();
        ret["capacity"] = static_cast<Json::UInt64>(aylog->getCapacity());
        ret["message"] = aylog->isRecording() ? "AY log session started"
                                              : "AY log activated but no AY chip found for this model";

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (action == "stop")
    {
        ScopedPause pause(emulator.get());
        bool deactivated = analyzerManager->deactivate("aylog");

        Json::Value ret;
        ret["emulator_id"] = id;
        ret["success"] = deactivated;
        ret["active"] = analyzerManager->isActive("aylog");
        ret["recording"] = aylog->isRecording();
        ret["entry_count"] = static_cast<Json::UInt64>(aylog->getEntryCount());
        ret["dropped"] = static_cast<Json::UInt64>(aylog->getDroppedCount());
        ret["message"] = deactivated ? "AY log session stopped (data retained)" : "AY log session was not active";

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (action == "clear")
    {
        ScopedPause pause(emulator.get());
        aylog->clear();

        Json::Value ret;
        ret["emulator_id"] = id;
        ret["success"] = true;
        ret["message"] = "AY log cleared";

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value error;
    error["error"] = "Bad Request";
    error["message"] = "Unknown action '" + action + "' (expected start|stop|clear)";

    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/ay/log?limit=256&offset=0&tail=false
/// @brief Read recorded AY register-write entries (chronological) + per-register stats
void EmulatorAPI::getAYLog(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    AnalyzerManager* analyzerManager = nullptr;
    AYLogAnalyzer* aylog = findAYLogAnalyzer(emulator.get(), &analyzerManager);
    if (!aylog || !analyzerManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "AY log analyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    size_t limit = 256;
    size_t offset = 0;
    bool tail = false;

    auto params = req->getParameters();
    try
    {
        auto limitIt = params.find("limit");
        if (limitIt != params.end())
            limit = static_cast<size_t>(std::stoul(limitIt->second));
        auto offsetIt = params.find("offset");
        if (offsetIt != params.end())
            offset = static_cast<size_t>(std::stoul(offsetIt->second));
        auto tailIt = params.find("tail");
        if (tailIt != params.end())
            tail = tailIt->second == "true" || tailIt->second == "1";
    }
    catch (...)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid limit/offset parameter";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const size_t entryCount = aylog->getEntryCount();
    if (tail)
    {
        // Read the newest `limit` entries
        offset = entryCount > limit ? entryCount - limit : 0;
    }

    auto entries = aylog->getEntries(offset, limit);

    // Per-register write counts over the full retained log
    uint64_t registerCounts[16] = { 0 };
    uint64_t selectCount = 0;
    for (const auto& record : aylog->getEntries(0, 0))
    {
        if (record.port == 0xFFFD)
            selectCount++;
        else if (record.reg < 16)
            registerCounts[record.reg]++;
    }

    Json::Value entriesJson(Json::arrayValue);
    size_t index = offset;
    for (const auto& record : entries)
    {
        Json::Value item;
        item["index"] = static_cast<Json::UInt64>(index++);
        item["frame"] = static_cast<Json::UInt64>(record.frame);
        item["tacts"] = static_cast<Json::UInt64>(record.tacts);
        item["pc"] = StringHelper::Format("0x%04X", record.pc);
        item["port"] = StringHelper::Format("0x%04X", record.port);
        item["chip"] = record.chip;
        item["type"] = record.port == 0xFFFD ? (record.value > 0x0F ? "switch" : "select") : "write";
        item["reg"] = record.reg;
        if (record.reg < 16)
            item["reg_name"] = SoundChip_AY8910::AYRegisterNames[record.reg];
        item["value"] = record.value;
        entriesJson.append(item);
    }

    Json::Value registersJson(Json::arrayValue);
    for (int reg = 0; reg < 16; reg++)
    {
        Json::Value item;
        item["reg"] = reg;
        item["name"] = SoundChip_AY8910::AYRegisterNames[reg];
        item["writes"] = static_cast<Json::UInt64>(registerCounts[reg]);
        registersJson.append(item);
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["active"] = analyzerManager->isActive("aylog");
    ret["recording"] = aylog->isRecording();
    ret["entry_count"] = static_cast<Json::UInt64>(entryCount);
    ret["dropped"] = static_cast<Json::UInt64>(aylog->getDroppedCount());
    ret["capacity"] = static_cast<Json::UInt64>(aylog->getCapacity());
    ret["select_writes"] = static_cast<Json::UInt64>(selectCount);
    ret["registers"] = registersJson;
    ret["entries"] = entriesJson;
    ret["returned"] = static_cast<Json::UInt64>(entries.size());
    ret["offset"] = static_cast<Json::UInt64>(offset);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// endregion </AY log endpoints>

/// region <Audio capture endpoints (MCP automation)>

/// @brief POST /api/v1/emulator/{id}/audio/capture
/// @brief Manage an audio capture session. Body: {"action":"start|stop|clear", "seconds":1.0}
void EmulatorAPI::audioCapture(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    AnalyzerManager* analyzerManager = nullptr;
    AudioCaptureAnalyzer* capture = findAudioCaptureAnalyzer(emulator.get(), &analyzerManager);
    if (!capture || !analyzerManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Audio capture analyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string action = "start";
    auto json = req->getJsonObject();
    if (json && json->isMember("action"))
    {
        action = (*json)["action"].asString();
    }

    if (action == "start")
    {
        double seconds = 1.0;
        if (json && json->isMember("seconds"))
        {
            seconds = (*json)["seconds"].asDouble();
        }
        if (seconds < 0.01 || seconds > 30.0)
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "'seconds' must be within [0.01, 30.0]";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        const size_t rate = audioCoreRate(emulator.get());
        const size_t target = static_cast<size_t>(seconds * static_cast<double>(rate)) * 2;  // interleaved stereo

        // Mutate analyzer subscriptions only while the emulation thread is parked
        ScopedPause pause(emulator.get());
        analyzerManager->activate("audiocapture");
        capture->startCapture(target);

        Json::Value ret;
        ret["emulator_id"] = id;
        ret["success"] = capture->isCaptureArmed();
        ret["armed"] = capture->isCaptureArmed();
        ret["target_samples"] = static_cast<Json::UInt64>(capture->getTargetSamples());
        ret["seconds"] = seconds;
        ret["sample_rate"] = static_cast<Json::UInt64>(rate);
        ret["channels"] = 2;
        ret["message"] = "Audio capture armed — the emulator must run to collect samples";

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (action == "stop")
    {
        ScopedPause pause(emulator.get());
        capture->stopCapture();

        Json::Value ret;
        ret["emulator_id"] = id;
        ret["success"] = true;
        ret["armed"] = capture->isCaptureArmed();
        ret["complete"] = capture->isCaptureComplete();
        ret["captured_samples"] = static_cast<Json::UInt64>(capture->getCapturedSamples());
        ret["target_samples"] = static_cast<Json::UInt64>(capture->getTargetSamples());
        ret["message"] = "Audio capture stopped (buffer retained)";

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (action == "clear")
    {
        capture->clearCapture();

        Json::Value ret;
        ret["emulator_id"] = id;
        ret["success"] = true;
        ret["message"] = "Audio capture data cleared";

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value error;
    error["error"] = "Bad Request";
    error["message"] = "Unknown action '" + action + "' (expected start|stop|clear)";

    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/audio/capture/status
void EmulatorAPI::audioCaptureStatus(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id) const
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

    AudioCaptureAnalyzer* capture = findAudioCaptureAnalyzer(emulator.get());
    if (!capture)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Audio capture analyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const size_t captured = capture->getCapturedSamples();
    const size_t target = capture->getTargetSamples();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["armed"] = capture->isCaptureArmed();
    ret["complete"] = capture->isCaptureComplete();
    ret["capturing"] = capture->isCaptureArmed() && !capture->isCaptureComplete();
    ret["captured_samples"] = static_cast<Json::UInt64>(captured);
    ret["captured_frames"] = static_cast<Json::UInt64>(captured / 2);
    ret["target_samples"] = static_cast<Json::UInt64>(target);
    ret["progress"] = target > 0 ? static_cast<double>(captured) / target : 0.0;
    ret["sample_rate"] = static_cast<Json::UInt64>(audioCoreRate(emulator.get()));
    ret["channels"] = 2;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/audio/capture/result?wav=true
/// @brief Offline analysis of the captured buffer: RMS/peak per channel,
/// @brief dominant frequency (zero-crossing estimate) and optional WAV export
void EmulatorAPI::audioCaptureResult(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id) const
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

    AudioCaptureAnalyzer* capture = findAudioCaptureAnalyzer(emulator.get());
    if (!capture)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Audio capture analyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (capture->isCaptureArmed() && !capture->isCaptureComplete())
    {
        Json::Value error;
        error["error"] = "Conflict";
        error["message"] = "Capture still in progress — poll /audio/capture/status first";
        error["captured_samples"] = static_cast<Json::UInt64>(capture->getCapturedSamples());
        error["target_samples"] = static_cast<Json::UInt64>(capture->getTargetSamples());

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k409Conflict);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const auto& buffer = capture->getBuffer();
    const size_t frames = buffer.size() / 2;
    if (frames == 0)
    {
        Json::Value error;
        error["error"] = "Conflict";
        error["message"] = "No captured audio — start a capture with POST /audio/capture first";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k409Conflict);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const size_t rate = audioCoreRate(emulator.get());
    const double duration = static_cast<double>(frames) / static_cast<double>(rate);

    // Per-channel peak and RMS (normalized 0..1) + mono mixdown for zero crossings
    double peak[2] = { 0.0, 0.0 };
    double sumSquares[2] = { 0.0, 0.0 };
    uint64_t crossings = 0;
    int previousSign = 0;  // 0 = unset, so leading silence never counts as a crossing
    for (size_t frame = 0; frame < frames; frame++)
    {
        int32_t mono = (static_cast<int32_t>(buffer[frame * 2]) + buffer[frame * 2 + 1]) / 2;
        for (int channel = 0; channel < 2; channel++)
        {
            const double normalized = static_cast<double>(buffer[frame * 2 + channel]) / 32768.0;
            double absolute = std::abs(normalized);
            if (absolute > peak[channel])
                peak[channel] = absolute;
            sumSquares[channel] += normalized * normalized;
        }
        int sign = mono > 0 ? 1 : (mono < 0 ? -1 : 0);
        if (sign != 0)
        {
            if (previousSign != 0 && sign != previousSign)
                crossings++;
            previousSign = sign;
        }
    }

    const double zeroCrossingRate = crossings / duration;          // crossings per second
    const double dominantHz = zeroCrossingRate / 2.0;              // one period = two crossings

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["sample_rate"] = static_cast<Json::UInt64>(rate);
    ret["channels"] = 2;
    ret["frames"] = static_cast<Json::UInt64>(frames);
    ret["duration_seconds"] = duration;
    ret["complete"] = capture->isCaptureComplete();

    Json::Value left;
    left["peak"] = peak[0];
    left["rms"] = std::sqrt(sumSquares[0] / frames);
    Json::Value right;
    right["peak"] = peak[1];
    right["rms"] = std::sqrt(sumSquares[1] / frames);
    ret["left"] = left;
    ret["right"] = right;

    ret["zero_crossing_rate"] = zeroCrossingRate;
    ret["dominant_hz"] = dominantHz;

    // Optional WAV export (?wav=true)
    bool wantWav = false;
    auto wavIt = req->getParameters().find("wav");
    if (wavIt != req->getParameters().end())
        wantWav = wavIt->second == "true" || wavIt->second == "1";
    if (wantWav)
    {
        std::string path = writeCaptureWav(id, buffer, frames, static_cast<uint32_t>(rate));
        if (!path.empty())
        {
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            ret["wav_path"] = path;
            ret["wav_bytes"] = ec ? 0 : static_cast<Json::UInt64>(size);
        }
        else
        {
            ret["wav_error"] = "Failed to write WAV file (temp directory not writable?)";
        }
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// endregion </Audio capture endpoints>

/// @brief GET /api/v1/emulator/{id}/analysis/regions
/// @brief Get memory segmentation regions
void EmulatorAPI::getAnalysisRegions(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    auto* mra = dynamic_cast<MemoryRegionAnalyzer*>(analyzerManager->getAnalyzer("memory-region"));
    if (!mra)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "MemoryRegionAnalyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    mra->refresh();
    const auto& regions = mra->getRegions();

    Json::Value ret;
    ret["emulator_id"] = id;
    Json::Value regionsJson(Json::arrayValue);

    for (const auto& r : regions)
    {
        Json::Value region;
        region["start"] = r.startAddress;
        region["end"] = r.endAddress;
        region["size"] = r.endAddress - r.startAddress + 1;

        const char* typeName = "UNKNOWN";
        switch (r.type) {
            case BlockType::CODE: typeName = "CODE"; break;
            case BlockType::DATA: typeName = "DATA"; break;
            case BlockType::VARIABLE: typeName = "VARIABLE"; break;
            case BlockType::SMC: typeName = "SMC"; break;
            default: break;
        }
        region["type"] = typeName;
        region["tags"] = static_cast<Json::UInt>(r.tags);

        regionsJson.append(region);
    }

    ret["regions"] = regionsJson;
    ret["total_regions"] = static_cast<Json::UInt>(regions.size());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/analysis/stats
/// @brief Get memory segmentation statistics
void EmulatorAPI::getAnalysisStats(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    AnalyzerManager* analyzerManager = context && context->pDebugManager
        ? context->pDebugManager->GetAnalyzerManager() : nullptr;

    auto* mra = analyzerManager ? dynamic_cast<MemoryRegionAnalyzer*>(analyzerManager->getAnalyzer("memory-region")) : nullptr;
    if (!mra)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "MemoryRegionAnalyzer not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    mra->refresh();
    SegmentationStats s = mra->getStats();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["code_bytes"] = s.codeBytes;
    ret["data_bytes"] = s.dataBytes;
    ret["variable_bytes"] = s.variableBytes;
    ret["smc_bytes"] = s.smcBytes;
    ret["unknown_bytes"] = s.unknownBytes;
    ret["total_regions"] = s.totalRegions;
    ret["tagged_regions"] = s.taggedRegions;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/analysis/ports
/// @brief Get I/O port activity
void EmulatorAPI::getAnalysisPorts(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pPortTracker)
    {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "PortTracker not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto summaries = context->pPortTracker->GetPortSummaries();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["active"] = context->pPortTracker->IsActive();

    Json::Value portsJson(Json::arrayValue);
    for (const auto& s : summaries)
    {
        Json::Value port;
        port["port"] = s.port;
        port["reads"] = s.readCount;
        port["writes"] = s.writeCount;
        port["read_callers"] = s.uniqueReadCallers;
        port["write_callers"] = s.uniqueWriteCallers;
        portsJson.append(port);
    }

    ret["ports"] = portsJson;
    ret["total_active_ports"] = static_cast<Json::UInt>(summaries.size());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/analysis/start
/// @brief Start analysis session
void EmulatorAPI::startAnalysis(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
        error["message"] = "Context or debug manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Enable port tracking feature
    FeatureManager* fm = emulator->GetFeatureManager();
    if (fm)
    {
        fm->setFeature("porttracking", true);
    }

    // Start port tracker session
    if (context->pPortTracker)
    {
        context->pPortTracker->StartSession();
    }

    // Activate memory-region analyzer
    auto* mgr = context->pDebugManager->GetAnalyzerManager();
    if (mgr)
    {
        mgr->activate("memory-region");
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["success"] = true;
    ret["message"] = "Analysis session started";
    ret["port_tracking"] = context->pPortTracker && context->pPortTracker->IsActive();
    ret["analyzer_active"] = mgr && mgr->isActive("memory-region");

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/analysis/stop
/// @brief Stop analysis session
void EmulatorAPI::stopAnalysis(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
        error["message"] = "Context not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Stop port tracker
    if (context->pPortTracker)
    {
        context->pPortTracker->StopSession();
    }

    // Deactivate analyzer
    if (context->pDebugManager)
    {
        auto* mgr = context->pDebugManager->GetAnalyzerManager();
        if (mgr)
        {
            mgr->deactivate("memory-region");
        }
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["success"] = true;
    ret["message"] = "Analysis session stopped";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
