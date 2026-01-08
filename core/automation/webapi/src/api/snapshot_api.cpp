/// @author rfishe  
/// @date 08.01.2026
/// @brief WebAPI Snapshot control endpoints

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

// Helper to add CORS headers (declared extern in tape_disk_api.cpp)
extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief POST /api/v1/emulator/:id/snapshot/load
/// @brief Load snapshot file
void EmulatorAPI::loadSnapshot(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto json = req->getJsonObject();
    if (!json || !json->isMember("path")) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'path' parameter in request body";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::string path = (*json)["path"].asString();
    bool success = emulator->LoadSnapshot(path);
    
    Json::Value ret;
    ret["status"] = success ? "success" : "error";
    ret["message"] = success ? "Snapshot loaded successfully" : "Failed to load snapshot (check logs for details)";
    ret["path"] = path;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/snapshot/info
void EmulatorAPI::getSnapshotInfo(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    Json::Value ret;
    
    std::string snapshotPath = context->coreState.snapshotFilePath;
    bool isLoaded = !snapshotPath.empty();
    
    ret["status"] = isLoaded ? "loaded" : "empty";
    ret["file"] = snapshotPath;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
