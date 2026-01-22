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

}  // namespace v1
}  // namespace api
