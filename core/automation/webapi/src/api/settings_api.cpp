// WebAPI Settings Management Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief GET /api/v1/emulator/{id}/settings
/// @brief Get all settings for an emulator
void EmulatorAPI::getSettings(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    EmulatorContext* context = emulator->GetContext();
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

    CONFIG& config = context->config;

    Json::Value ret;
    Json::Value settings(Json::objectValue);

    // I/O Acceleration settings
    Json::Value io_accel(Json::objectValue);
    io_accel["fast_tape"] = config.tape_traps != 0;
    io_accel["fast_disk"] = config.wd93_nodelay;
    settings["io_acceleration"] = io_accel;

    // Disk Interface settings
    Json::Value disk_if(Json::objectValue);
    disk_if["trdos_present"] = config.trdos_present;
    disk_if["trdos_traps"] = config.trdos_traps;
    settings["disk_interface"] = disk_if;

    ret["emulator_id"] = id;
    ret["settings"] = settings;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/settings/{name}
/// @brief Get a specific setting value
void EmulatorAPI::getSetting(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    EmulatorContext* context = emulator->GetContext();
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

    CONFIG& config = context->config;
    Json::Value ret;

    if (name == "fast_tape")
    {
        ret["name"] = "fast_tape";
        ret["value"] = config.tape_traps != 0;
        ret["description"] = "Fast tape loading (bypasses audio emulation)";
    }
    else if (name == "fast_disk")
    {
        ret["name"] = "fast_disk";
        ret["value"] = config.wd93_nodelay;
        ret["description"] = "Fast disk I/O (removes WD1793 controller delays)";
    }
    else if (name == "trdos_present")
    {
        ret["name"] = "trdos_present";
        ret["value"] = config.trdos_present;
        ret["description"] = "Enable Beta128 TR-DOS disk interface";
    }
    else if (name == "trdos_traps")
    {
        ret["name"] = "trdos_traps";
        ret["value"] = config.trdos_traps;
        ret["description"] = "Use TR-DOS traps for faster disk operations";
    }
    else
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown setting: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ret["emulator_id"] = id;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT/POST /api/v1/emulator/{id}/settings/{name}
/// @brief Set a specific setting value
void EmulatorAPI::setSetting(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    EmulatorContext* context = emulator->GetContext();
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

    // Parse request body for the new value
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

    bool boolValue = (*json)["value"].asBool();
    CONFIG& config = context->config;
    Json::Value ret;

    if (name == "fast_tape")
    {
        config.tape_traps = boolValue ? 1 : 0;
        ret["name"] = "fast_tape";
        ret["value"] = boolValue;
        ret["message"] = std::string("Fast tape loading is now ") + (boolValue ? "enabled" : "disabled");
    }
    else if (name == "fast_disk")
    {
        config.wd93_nodelay = boolValue;
        ret["name"] = "fast_disk";
        ret["value"] = boolValue;
        ret["message"] = std::string("Fast disk I/O is now ") + (boolValue ? "enabled" : "disabled");
    }
    else if (name == "trdos_present")
    {
        config.trdos_present = boolValue;
        ret["name"] = "trdos_present";
        ret["value"] = boolValue;
        ret["message"] = std::string("TR-DOS interface is now ") + (boolValue ? "enabled" : "disabled");
        ret["restart_required"] = true;
    }
    else if (name == "trdos_traps")
    {
        config.trdos_traps = boolValue;
        ret["name"] = "trdos_traps";
        ret["value"] = boolValue;
        ret["message"] = std::string("TR-DOS traps are now ") + (boolValue ? "enabled" : "disabled");
    }
    else
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown setting: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ret["emulator_id"] = id;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
