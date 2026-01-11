// WebAPI Feature Management Implementation
// Implements /features endpoints - 2026-01-10

#include <base/featuremanager.h>
#include <drogon/HttpResponse.h>
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

/// @brief GET /api/v1/emulator/{id}/features
/// @brief Get all features for an emulator
void EmulatorAPI::getFeatures(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    auto* featureManager = emulator->GetFeatureManager();
    if (!featureManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Feature manager not available for this emulator";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    Json::Value features(Json::arrayValue);

    for (const auto& f : featureManager->listFeatures())
    {
        Json::Value feature(Json::objectValue);
        feature["id"] = f.id;
        feature["enabled"] = f.enabled;
        feature["description"] = f.description;

        if (!f.mode.empty())
        {
            feature["mode"] = f.mode;
        }

        features.append(feature);
    }

    ret["emulator_id"] = id;
    ret["features"] = features;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/feature/{name}
/// @brief Get a specific feature state
void EmulatorAPI::getFeature(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    auto* featureManager = emulator->GetFeatureManager();
    if (!featureManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Feature manager not available for this emulator";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Find the feature
    bool found = false;
    Json::Value ret;

    for (const auto& f : featureManager->listFeatures())
    {
        if (f.id == name || f.alias == name)
        {
            ret["emulator_id"] = id;
            ret["feature_id"] = f.id;
            ret["enabled"] = f.enabled;
            ret["description"] = f.description;

            if (!f.mode.empty())
            {
                ret["mode"] = f.mode;
            }

            if (!f.alias.empty())
            {
                ret["alias"] = f.alias;
            }

            found = true;
            break;
        }
    }

    if (!found)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown feature: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT/POST /api/v1/emulator/{id}/feature/{name}
/// @brief Set a specific feature state
void EmulatorAPI::setFeature(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    auto* featureManager = emulator->GetFeatureManager();
    if (!featureManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Feature manager not available for this emulator";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse request body for the new value
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

    // Apply the feature change
    bool success = featureManager->setFeature(name, enabled);

    if (!success)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown feature: " + name;

        // List available features
        Json::Value available(Json::arrayValue);
        for (const auto& f : featureManager->listFeatures())
        {
            available.append(f.id);
        }
        error["available_features"] = available;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["feature_id"] = name;
    ret["enabled"] = enabled;
    ret["message"] = std::string("Feature '") + name + "' " + (enabled ? "enabled" : "disabled");

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
