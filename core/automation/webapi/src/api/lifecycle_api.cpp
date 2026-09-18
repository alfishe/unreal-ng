// WebAPI Emulator Lifecycle Management Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/buildinfo.h>
#include <emulator/config.h>
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

namespace
{
// Machine identity block shared by lifecycle responses (P0-1: a triage
// session must always see WHICH machine it is talking to). Computed by
// EmulatorManager::GetMachineIdentity - the single source the CLI also uses -
// so every automation surface reports identical information. Every read is
// guarded so a partially initialized instance still yields a complete
// object; "video_mode" is null until the screen subsystem exists.
void AddIdentityFields(Json::Value& target, Emulator& emulator)
{
    const MachineIdentity identity = EmulatorManager::GetMachineIdentity(emulator);

    if (!identity.Valid)
    {
        target["model"] = Json::Value();
        target["model_full_name"] = Json::Value();
        target["ram_kb"] = Json::Value();
        target["video_mode"] = Json::Value();
        target["speed_multiplier"] = Json::Value();
        target["config_folder"] = Json::Value();
        return;
    }

    target["model"] = identity.Model;
    target["model_full_name"] = identity.ModelFullName;
    target["ram_kb"] = static_cast<Json::UInt>(identity.RamKb);
    target["video_mode"] = identity.HasVideoMode ? Json::Value(identity.VideoMode) : Json::Value();
    target["speed_multiplier"] = identity.SpeedMultiplier;
    target["config_folder"] = identity.ConfigFolder;
}
}

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
            AddIdentityFields(emuInfo, *emulator);
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
    Json::Value ret;
    Json::Value modelsArray(Json::arrayValue);

    try
    {
        auto manager = EmulatorManager::GetInstance();
        auto models = manager->GetAvailableModels();

        for (const auto& model : models)
        {
            // Skip empty/sentinel entries (entries with NULL or empty ShortName)
            if (!model.ShortName || model.ShortName[0] == '\0')
            {
                continue;
            }


            Json::Value modelInfo;

            // Order fields logically: id and name first, then details
            modelInfo["id"] = static_cast<int>(model.Model);
            modelInfo["name"] = std::string(model.ShortName);
            modelInfo["full_name"] = model.FullName ? std::string(model.FullName) : "";
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

            // Whether a create request for this model is expected to succeed
            // on this build (port decoder + config folder present). Lets
            // clients filter machine lists instead of discovering unsupported
            // models one failed request at a time.
            modelInfo["creatable"] = Config::IsModelCreatable(model);

            modelsArray.append(modelInfo);
        }

        ret["models"] = modelsArray;
        ret["count"] = static_cast<Json::UInt>(modelsArray.size());

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Failed to retrieve models";
        error["message"] = e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
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

    // Build fingerprint + which machines this build can actually create
    // (P0-4): lets triage sessions attribute behaviour to a specific
    // branch/commit and discover unsupported machines up front.
    Json::Value server(Json::objectValue);
    server["version"] = buildinfo::kVersion;
    server["git_branch"] = buildinfo::kGitBranch;
    server["git_commit"] = buildinfo::kGitCommit;
    server["build_type"] = buildinfo::kBuildType;
    ret["server"] = server;

    Json::Value creatableModels(Json::arrayValue);
    for (const TMemModel& model : manager->GetAvailableModels())
    {
        if (model.ShortName != nullptr && model.ShortName[0] != '\0' && Config::IsModelCreatable(model))
        {
            creatableModels.append(std::string(model.ShortName));
        }
    }
    ret["models_creatable"] = creatableModels;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator
/// @brief Create a new emulator instance
/// @brief Request body (all optional):
/// @brief {
/// @brief   "symbolic_id": "my-emulator",
/// @brief   "model": "48K" | "128K" | "PENTAGON" | etc,
/// @brief   "ram_size": 128 (in KB, only valid for models that support it),
/// @brief }
/// @brief A non-empty "model" that cannot be created on this build fails with
/// @brief 400 + reason - there is NO silent fallback to a default machine.
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
        std::string createError;

        if (!modelName.empty() && ramSize > 0)
        {
            // Create with specific model and RAM size - strict: no fallback
            emulator = manager->CreateEmulatorWithModelAndRAM(symbolicId, modelName, ramSize,
                                                              LoggerLevel::LogWarning, &createError);
        }
        else if (!modelName.empty())
        {
            // Create with specific model (default RAM) - strict: no fallback
            emulator = manager->CreateEmulatorWithModel(symbolicId, modelName, LoggerLevel::LogWarning, &createError);
        }
        else
        {
            // Create with default configuration (48K)
            emulator = manager->CreateEmulator(symbolicId);
        }

        if (!emulator)
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = !createError.empty() ? createError : std::string("Emulator initialization failed");
            if (!modelName.empty())
            {
                error["requested_model"] = modelName;
            }
            error["available_models_endpoint"] = "/api/v1/emulator/models";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        Json::Value ret;
        ret["id"] = emulator->GetId();
        ret["state"] = stateToString(emulator->GetState());
        ret["symbolic_id"] = emulator->GetSymbolicId();
        AddIdentityFields(ret, *emulator);

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
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief GET /api/v1/emulator/{id}
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
    AddIdentityFields(ret, *emulator);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief DELETE /api/v1/emulator/{id}
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
        addCorsHeaders(resp);
        callback(resp);
    }
}

///  @brief POST /api/v1/emulator/start
/// @brief Create and start a new emulator
/// @brief Request body (all optional):
/// @brief {
/// @brief   "symbolic_id": "my-emulator",
/// @brief   "model": "48K" | "128K" | "PENTAGON" | etc,
/// @brief   "ram_size": 128 (in KB, only valid for models that support it)
/// @brief }
void EmulatorAPI::startEmulator(const HttpRequestPtr& req,
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
        std::string createError;

        // Create emulator with specified parameters - strict: a requested
        // model that cannot be created on this build fails with 400 + reason
        // instead of falling back to a default machine
        if (!modelName.empty() && ramSize > 0)
        {
            emulator = manager->CreateEmulatorWithModelAndRAM(symbolicId, modelName, ramSize,
                                                              LoggerLevel::LogWarning, &createError);
        }
        else if (!modelName.empty())
        {
            emulator = manager->CreateEmulatorWithModel(symbolicId, modelName, LoggerLevel::LogWarning, &createError);
        }
        else
        {
            emulator = manager->CreateEmulator(symbolicId);
        }

        if (!emulator)
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = !createError.empty() ? createError : std::string("Emulator initialization failed");
            if (!modelName.empty())
            {
                error["requested_model"] = modelName;
            }
            error["available_models_endpoint"] = "/api/v1/emulator/models";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        // Start the emulator
        std::string emulatorId = emulator->GetId();
        bool started = manager->StartEmulatorAsync(emulatorId);

        // Explicitly select this emulator since it was explicitly started via WebAPI
        if (started)
        {
            manager->SetSelectedEmulatorId(emulatorId);
        }

        Json::Value ret;
        ret["id"] = emulatorId;
        ret["symbolic_id"] = emulator->GetSymbolicId();
        ret["state"] = stateToString(emulator->GetState());
        ret["started"] = started;
        ret["message"] = started ? "Emulator created and started" : "Emulator created but failed to start";

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(HttpStatusCode::k201Created);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Failed to create and start emulator";
        error["message"] = e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/start
/// @brief Start an existing emulator
void EmulatorAPI::startExistingEmulator(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
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

        // Explicitly select this emulator since it was explicitly started via WebAPI
        if (success)
        {
            manager->SetSelectedEmulatorId(id);
        }

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
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/stop
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
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/pause
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
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/resume
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

    // Check run-control claim (GDB TDD §3.3 / 1A.7.2)
    auto emulator = manager->GetEmulator(id);
    if (emulator)
    {
        auto* ctx = emulator->GetContext();
        if (ctx && ctx->IsRunControlClaimed())
        {
            auto state = ctx->GetRunControlState();
            Json::Value error;
            error["error"] = "Run-control held";
            error["message"] = "Run-control held by " + state.surfaceLabel + ". Use that surface to resume.";
            error["emulator_id"] = id;

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k409Conflict);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
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
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/reset
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
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/nmi
/// @brief Pulse the Z80 NMI line (accepted at the next instruction boundary)
/// @param body Optional JSON: {"magic": true|false}. true = Scorpion MNI
///        "magic button" - the Shadow Monitor is paged into #0000 before the
///        NMI so the handler at #0066 executes monitor code. Non-Scorpion models
///        fall back to a plain NMI regardless of the flag.
void EmulatorAPI::requestNmi(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
        bool magic = false;
        auto json = req->getJsonObject();
        if (json && json->isMember("magic") && json->get("magic", false).isBool())
            magic = json->get("magic", false).asBool();

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

        if (magic)
            emulator->RequestMNI();
        else
            emulator->RequestNMI();

        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = magic ? "MNI requested (magic button)" : "NMI requested";
        ret["emulator_id"] = id;
        ret["magic"] = magic;

        auto resp = HttpResponse::newHttpJsonResponse(ret);
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
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/model
/// @brief Switch emulator to a different machine model
/// @details This stops the current emulator, destroys it, creates a new one with the specified model, and starts it.
///          The new emulator will have a different ID than the original.
/// @param id The ID of the emulator to switch
/// @param body JSON body with "model" field containing the model short name (e.g., "PENTAGON", "48K", "128K", "SCORPION")
///             and optional "ram_size" field for RAM size in KB
void EmulatorAPI::switchModel(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    // Parse request body
    auto body = req->getJsonObject();
    if (!body || !body->isMember("model"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Request body must contain 'model' field with model short name";
        error["available_models_endpoint"] = "/api/v1/emulator/models";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string modelName = (*body)["model"].asString();
    uint32_t ramSize = 0;
    if (body->isMember("ram_size"))
    {
        ramSize = (*body)["ram_size"].asUInt();
    }

    // Validate BEFORE touching the current instance: a failed model switch
    // must leave the caller's emulator untouched (the old flow removed the
    // instance first and a bad model then left the caller with nothing).
    // These checks mirror the ones inside EmulatorManager create paths.
    const TMemModel* requestedModel = Config::FindModelByShortName(modelName);
    if (!requestedModel)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "unknown model '" + modelName + "'";
        error["requested_model"] = modelName;
        error["available_models_endpoint"] = "/api/v1/emulator/models";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (ramSize > 0 && (ramSize & requestedModel->AvailRAMs) == 0)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "RAM size " + std::to_string(ramSize) + "KB is not supported by model '" + modelName +
                           "' (see available_ram_sizes_kb in /api/v1/emulator/models)";
        error["requested_model"] = modelName;
        error["available_models_endpoint"] = "/api/v1/emulator/models";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (!Config::IsModelCreatable(*requestedModel))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "model '" + modelName +
                           "' is not creatable on this build (port decoder or config folder missing)";
        error["requested_model"] = modelName;
        error["available_models_endpoint"] = "/api/v1/emulator/models";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    try
    {
        // Stop the current emulator
        auto currentEmulator = manager->GetEmulator(id);
        if (currentEmulator && currentEmulator->IsRunning())
        {
            currentEmulator->Stop();
        }

        // Store symbolic ID if any
        std::string symbolicId;
        if (currentEmulator)
        {
            symbolicId = currentEmulator->GetSymbolicId();
        }

        // Remove the old emulator
        manager->RemoveEmulator(id);

        // Create a new emulator with the requested model
        std::shared_ptr<Emulator> newEmulator;
        std::string createError;
        if (ramSize > 0)
        {
            newEmulator = manager->CreateEmulatorWithModelAndRAM(symbolicId, modelName, ramSize,
                                                                 LoggerLevel::LogWarning, &createError);
        }
        else
        {
            newEmulator = manager->CreateEmulatorWithModel(symbolicId, modelName, LoggerLevel::LogWarning, &createError);
        }

        if (!newEmulator)
        {
            Json::Value error;
            error["error"] = "Failed to create emulator";
            error["message"] = !createError.empty()
                                   ? createError
                                   : ("Could not create emulator with model '" + modelName + "'");
            error["requested_model"] = modelName;
            error["available_models_endpoint"] = "/api/v1/emulator/models";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        // Initialize and start the new emulator
        bool initSuccess = newEmulator->Init();
        if (!initSuccess)
        {
            Json::Value error;
            error["error"] = "Initialization failed";
            error["message"] = "Emulator created but failed to initialize";
            error["new_emulator_id"] = newEmulator->GetId();

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k500InternalServerError);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        newEmulator->Start();

        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Model switched successfully";
        ret["old_emulator_id"] = id;
        ret["new_emulator_id"] = newEmulator->GetId();
        ret["state"] = stateToString(newEmulator->GetState());
        AddIdentityFields(ret, *newEmulator);

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(HttpStatusCode::k200OK);
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
        addCorsHeaders(resp);
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
        addCorsHeaders(resp);
        callback(resp);
    }
}

} // namespace v1
} // namespace api
