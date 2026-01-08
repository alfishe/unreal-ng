// WebAPI Emulator Lifecycle Management Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/platform.h>
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

/// @brief GET /api/v1/emulator
/// @brief List all emulators
void EmulatorAPI::get(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulatorIds = manager->GetEmulatorIds();

    Json::Value ret;
    Json::Value emulators(Json::arrayValue);

    for (const auto& id : emulatorIds)
    {
        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            Json::Value emuInfo;
            emuInfo["id"] = id;
            emuInfo["state"] = stateToString(emulator->GetState());
            emuInfo["is_running"] = emulator->IsRunning();
            emuInfo["is_paused"] = emulator->IsPaused();
            emuInfo["is_debug"] = emulator->IsDebug();
            emulators.append(emuInfo);
        }
    }

    ret["emulators"] = emulators;
    ret["count"] = static_cast<Json::UInt>(emulatorIds.size());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/models
/// @brief Get available emulator models
void EmulatorAPI::getModels(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto manager = EmulatorManager::GetInstance();
    auto models = manager->GetAvailableModels();

    Json::Value ret;
    Json::Value modelsArray(Json::arrayValue);

    for (const auto& model : models)
    {
        Json::Value modelInfo;
        modelInfo["name"] = model.ShortName;
        modelInfo["full_name"] = model.FullName;
        modelInfo["model_id"] = static_cast<int>(model.Model);
        modelInfo["default_ram_kb"] = model.defaultRAM;

        // Parse available RAM sizes from bitmask
        Json::Value availableRAMs(Json::arrayValue);
        unsigned ramMask = model.AvailRAMs;
        const int ramSizes[] = {48, 128, 256, 512, 1024, 2048, 4096};
        for (int ram : ramSizes)
        {
            if (ramMask & ram)
            {
                availableRAMs.append(ram);
            }
        }
        modelInfo["available_ram_sizes_kb"] = availableRAMs;

        modelsArray.append(modelInfo);
    }

    ret["models"] = modelsArray;
    ret["count"] = static_cast<Json::UInt>(models.size());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/status
/// @brief Get overall emulator status
void EmulatorAPI::status(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulatorIds = manager->GetEmulatorIds();

    Json::Value ret;
    ret["emulator_count"] = static_cast<Json::UInt>(emulatorIds.size());

    // Count emulators by state
    Json::Value states(Json::objectValue);
    for (const auto& id : emulatorIds)
    {
        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            std::string state = stateToString(emulator->GetState());
            if (states.isMember(state))
            {
                states[state] = states[state].asInt() + 1;
            }
            else
            {
                states[state] = 1;
            }
        }
    }
    ret["states"] = states;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator
/// @brief Create a new emulator instance
void EmulatorAPI::createEmulator(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto manager = EmulatorManager::GetInstance();

    // Parse request body
    auto json = req->getJsonObject();
    std::string symbolicId = json ? (*json)["symbolic_id"].asString() : "";
    std::string modelName = json ? (*json)["model"].asString() : "";
    uint32_t ramSize = json && json->isMember("ram_size") ? (*json)["ram_size"].asUInt() : 0;

    try
    {
        std::shared_ptr<Emulator> emulator;

        if (!modelName.empty() && ramSize > 0)
        {
            // Create with specific model and RAM size
            emulator = manager->CreateEmulatorWithModelAndRAM(symbolicId, modelName, ramSize);
            if (!emulator)
            {
                Json::Value error;
                error["error"] = "Failed to create emulator";
                error["message"] = "Invalid model '" + modelName + "' or RAM size " + std::to_string(ramSize) +
                                   "KB not supported by this model";

                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
        }
        else if (!modelName.empty())
        {
            // Create with specific model (default RAM)
            emulator = manager->CreateEmulatorWithModel(symbolicId, modelName);
            if (!emulator)
            {
                Json::Value error;
                error["error"] = "Failed to create emulator";
                error["message"] = "Unknown or invalid model: '" + modelName + "'";

                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
        }
        else
        {
            // Create with default configuration
            emulator = manager->CreateEmulator(symbolicId);
        }

        if (!emulator)
        {
            Json::Value error;
            error["error"] = "Failed to create emulator";
            error["message"] = "Emulator initialization failed";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k500InternalServerError);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        Json::Value ret;
        ret["id"] = emulator->GetId();
        ret["state"] = stateToString(emulator->GetState());
        ret["symbolic_id"] = emulator->GetSymbolicId();

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(HttpStatusCode::k201Created);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Failed to create emulator";
        error["message"] = e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        callback(resp);
    }
}

/// @brief GET /api/v1/emulator/:id
/// @brief Get emulator details
void EmulatorAPI::getEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    Json::Value ret;
    ret["id"] = id;
    ret["state"] = stateToString(emulator->GetState());
    ret["is_running"] = emulator->IsRunning();
    ret["is_paused"] = emulator->IsPaused();
    ret["is_debug"] = emulator->IsDebug();

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief DELETE /api/v1/emulator/:id
/// @brief Remove an emulator
void EmulatorAPI::removeEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    bool removed = manager->RemoveEmulator(id);

    Json::Value ret;
    if (removed)
    {
        ret["status"] = "success";
        ret["message"] = "Emulator removed successfully";

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(HttpStatusCode::k200OK);
        addCorsHeaders(resp);
        callback(resp);
    }
    else
    {
        ret["status"] = "error";
        ret["message"] = "Failed to remove emulator";

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/:id/start
/// @brief Start an emulator
void EmulatorAPI::startEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    try
    {
        bool success = manager->StartEmulatorAsync(id);

        Json::Value ret;
        ret["status"] = success ? "success" : "error";
        ret["message"] = success ? "Emulator started" : "Failed to start emulator (already running or error)";
        ret["emulator_id"] = id;

        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            ret["state"] = stateToString(emulator->GetState());
        }

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/:id/stop
/// @brief Stop an emulator
void EmulatorAPI::stopEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    try
    {
        bool success = manager->StopEmulator(id);

        Json::Value ret;
        ret["status"] = success ? "success" : "error";
        ret["message"] = success ? "Emulator stopped" : "Failed to stop emulator (not running or error)";
        ret["emulator_id"] = id;

        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            ret["state"] = stateToString(emulator->GetState());
        }

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/:id/pause
/// @brief Pause an emulator
void EmulatorAPI::pauseEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    try
    {
        bool success = manager->PauseEmulator(id);

        Json::Value ret;
        ret["status"] = success ? "success" : "error";
        ret["message"] = success ? "Emulator paused" : "Failed to pause emulator (not running or error)";
        ret["emulator_id"] = id;

        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            ret["state"] = stateToString(emulator->GetState());
        }

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/:id/resume
/// @brief Resume an emulator
void EmulatorAPI::resumeEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    try
    {
        bool success = manager->ResumeEmulator(id);

        Json::Value ret;
        ret["status"] = success ? "success" : "error";
        ret["message"] = success ? "Emulator resumed" : "Failed to resume emulator (not paused or error)";
        ret["emulator_id"] = id;

        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            ret["state"] = stateToString(emulator->GetState());
        }

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/:id/reset
/// @brief Reset an emulator
void EmulatorAPI::resetEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    try
    {
        bool success = manager->ResetEmulator(id);

        Json::Value ret;
        ret["status"] = success ? "success" : "error";
        ret["message"] = success ? "Emulator reset" : "Failed to reset emulator";
        ret["emulator_id"] = id;

        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            ret["state"] = stateToString(emulator->GetState());
        }

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        callback(resp);
    }
}

/// @brief Helper method to handle emulator actions with common error handling
void EmulatorAPI::handleEmulatorAction(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id,
                                       std::function<std::string(std::shared_ptr<Emulator>)> action) const
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

    try
    {
        std::string message = action(emulator);

        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = message;
        ret["emulator_id"] = id;
        ret["state"] = stateToString(emulator->GetState());

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        callback(resp);
    }
}

} // namespace v1
} // namespace api
