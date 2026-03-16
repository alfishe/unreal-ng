// WebAPI Logging Control Implementation
// Per-module log level management via ModuleLogger

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <common/modulelogger.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief GET /api/v1/emulator/{id}/logging
/// @brief Get full module logger state (all modules, submodules, levels)
void EmulatorAPI::getLogging(const HttpRequestPtr& req,
                             std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pModuleLogger)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "ModuleLogger not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ModuleLogger* logger = context->pModuleLogger;
    LoggerLevel globalLevel = logger->GetLevel();

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["global_level"] = ModuleLogger::GetLevelApiName(static_cast<int>(globalLevel));
    ret["global_level_id"] = static_cast<int>(globalLevel);

    Json::Value modules(Json::arrayValue);
    for (int m = 1; m < MODULE_COUNT; m++)
    {
        auto module = static_cast<PlatformModulesEnum>(m);
        LoggerLevel modLevel = logger->GetModuleLogLevel(module);

        Json::Value mod;
        mod["id"] = m;
        mod["name"] = ModuleLogger::GetModuleApiName(m);
        mod["enabled"] = (logger->GetSettings().modules & (1u << m)) != 0;

        // Per-module level (0 = inherit)
        uint8_t rawLevel = logger->GetSettings().moduleLevels[m];
        if (rawLevel == 0)
        {
            mod["level"] = "inherit";
            mod["effective_level"] = ModuleLogger::GetLevelApiName(static_cast<int>(globalLevel));
        }
        else
        {
            mod["level"] = ModuleLogger::GetLevelApiName(rawLevel);
            mod["effective_level"] = ModuleLogger::GetLevelApiName(rawLevel);
        }
        mod["level_id"] = static_cast<int>(rawLevel);

        // Submodule mask
        uint16_t subMask = logger->GetSettings().submodules[m];
        mod["submodule_mask"] = subMask;

        modules.append(mod);
    }
    ret["modules"] = modules;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT /api/v1/emulator/{id}/logging/level
/// @brief Set global log level: {"level": "trace"|"debug"|"info"|"warning"|"error"|"none"}
void EmulatorAPI::setLoggingLevel(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pModuleLogger)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "ModuleLogger not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("level"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'level' field";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string levelStr = (*json)["level"].asString();
    int levelId = ModuleLogger::LevelNameToId(levelStr.c_str());
    if (levelId <= 0) levelId = -1;  // unknown(0) is invalid for setting
    if (levelId < 0)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid level. Valid: trace, debug, info, warning, error, none";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    context->pModuleLogger->SetLoggingLevel(static_cast<LoggerLevel>(levelId));

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["level"] = levelStr;
    ret["message"] = "Global log level set to " + levelStr;
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT /api/v1/emulator/{id}/logging/module/{name}
/// @brief Set module enable/level: {"enabled": true, "level": "debug"|"inherit", "submodule_mask": 0xFFFF}
void EmulatorAPI::setLoggingModule(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id, const std::string& name) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pModuleLogger)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "ModuleLogger not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    int moduleId = ModuleLogger::ModuleNameToId(name.c_str());
    if (moduleId < 0)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown module: " + name;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing JSON body";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ModuleLogger* logger = context->pModuleLogger;
    auto module = static_cast<PlatformModulesEnum>(moduleId);
    Json::Value ret;
    ret["emulator_id"] = id;
    ret["module"] = name;

    // Enable/disable + submodule mask — direct assignment via SetModuleState
    if (json->isMember("enabled"))
    {
        bool enabled = (*json)["enabled"].asBool();
        uint16_t subMask = 0xFFFF;
        if (json->isMember("submodule_mask"))
            subMask = static_cast<uint16_t>((*json)["submodule_mask"].asUInt());

        logger->SetModuleState(module, enabled, subMask);

        ret["enabled"] = enabled;
        ret["submodule_mask"] = subMask;
    }

    // Per-module level
    if (json->isMember("level"))
    {
        std::string levelStr = (*json)["level"].asString();
        if (levelStr == "inherit")
        {
            logger->SetModuleLogLevel(module, static_cast<LoggerLevel>(0));
            ret["level"] = "inherit";
        }
        else
        {
            int levelId = ModuleLogger::LevelNameToId(levelStr.c_str());
            if (levelId <= 0) levelId = -1;  // unknown(0) is invalid
            if (levelId >= 0)
            {
                logger->SetModuleLogLevel(module, static_cast<LoggerLevel>(levelId));
                ret["level"] = levelStr;
            }
        }
    }

    ret["message"] = "Module " + name + " updated";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
