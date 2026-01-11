// WebAPI Tape and Disk Control Implementation
// Generated: 2026-01-08

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/fdc/wd1793.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

// Helper to parse drive parameter (A-D or 0-3)
static uint8_t parseDriveParameter(const std::string& driveStr, std::string& errorMsg)
{
    if (driveStr.empty()) {
        errorMsg = "Missing drive parameter";
        return 0xFF;
    }
    if (driveStr == "A" || driveStr == "a" || driveStr == "0") return 0;
    if (driveStr == "B" || driveStr == "b" || driveStr == "1") return 1;
    if (driveStr == "C" || driveStr == "c" || driveStr == "2") return 2;
    if (driveStr == "D" || driveStr == "d" || driveStr == "3") return 3;
    
    errorMsg = "Invalid drive: " + driveStr + " (use A-D or 0-3)";
    return 0xFF;
}

namespace api
{
namespace v1
{

// Helper to add CORS headers (already exists in emulator_api.cpp, declaring here for consistency)
extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief POST /api/v1/emulator/:id/tape/load
/// @brief Load tape image
void EmulatorAPI::loadTape(const HttpRequestPtr& req,
                           std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";
        
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
    bool success = emulator->LoadTape(path);
    
    Json::Value ret;
    ret["status"] = success ? "success" : "error";
    ret["message"] = success ? "Tape loaded successfully" : "Failed to load tape (check logs for details)";
    ret["path"] = path;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/tape/eject
void EmulatorAPI::ejectTape(const HttpRequestPtr& req,
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
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Thread-safe eject
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    context->pTape->stopTape();
    context->coreState.tapeFilePath.clear();
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Tape ejected";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/tape/play
void EmulatorAPI::playTape(const HttpRequestPtr& req,
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
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Thread-safe play
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    context->pTape->startTape();
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Tape playback started";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/tape/stop
void EmulatorAPI::stopTape(const HttpRequestPtr& req,
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
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Thread-safe stop
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    context->pTape->stopTape();
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Tape playback stopped";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/tape/rewind
void EmulatorAPI::rewindTape(const HttpRequestPtr& req,
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
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Thread-safe rewind
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    context->pTape->reset();
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Tape rewound to beginning";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/tape/info
void EmulatorAPI::getTapeInfo(const HttpRequestPtr& req,
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
    
    if (!context || !context->pTape) {
        ret["status"] = "unavailable";
        ret["message"] = "Tape subsystem not available";
    } else {
        std::string tapePath = context->coreState.tapeFilePath;
        bool isLoaded = !tapePath.empty();
        
        ret["status"] = isLoaded ? "loaded" : "empty";
        ret["file"] = tapePath;
        // Note: _tapeStarted is protected in Tape class, cannot access directly
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/disk/:drive/insert
void EmulatorAPI::insertDisk(const HttpRequestPtr& req,
                             std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id,
                             const std::string& drive) const
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
    
    // Parse drive parameter
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
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
    bool success = emulator->LoadDisk(path);
    
    Json::Value ret;
    ret["status"] = success ? "success" : "error";
    ret["message"] = success ? "Disk inserted successfully" : "Failed to insert disk (check logs for details)";
    ret["path"] = path;
    ret["drive"] = drive;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/disk/:drive/eject
void EmulatorAPI::ejectDisk(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id,
                            const std::string& drive) const
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
    
    // Parse drive parameter
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || driveNum >= 4) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Disk drive not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Thread-safe eject
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    if (context->pBetaDisk) {
        context->pBetaDisk->ejectDisk();
    }
    if (context->coreState.diskDrives[driveNum]) {
        context->coreState.diskDrives[driveNum]->ejectDisk();
    }
    context->coreState.diskFilePaths[driveNum].clear();
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Disk ejected";
    ret["drive"] = drive;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/disk/:drive/info
void EmulatorAPI::getDiskInfo(const HttpRequestPtr& req,
                              std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id,
                              const std::string& drive) const
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
    
    // Parse drive parameter
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    Json::Value ret;
    
    if (!context || driveNum >= 4) {
        ret["status"] = "unavailable";
        ret["message"] = "Disk drive not available";
    } else {
        std::string diskPath = context->coreState.diskFilePaths[driveNum];
        bool isInserted = !diskPath.empty();
        
        ret["status"] = isInserted ? "inserted" : "empty";
        ret["file"] = diskPath;
        ret["drive"] = drive;
        
        if (context->coreState.diskDrives[driveNum]) {
            ret["write_protected"] = context->coreState.diskDrives[driveNum]->isWriteProtect();
        }
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
