// WebAPI BASIC Control Implementation
// Generated: 2026-01-17

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <drogon/utils/Utilities.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <debugger/analyzers/basic-lang/basicextractor.h>
#include <debugger/analyzers/basic-lang/basicencoder.h>
#include <common/stringhelper.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper to add CORS headers (already exists in emulator_api.cpp)
extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief POST /api/v1/emulator/:id/basic/run
/// @brief Execute a BASIC command (or RUN if no command specified)
void EmulatorAPI::basicRun(const HttpRequestPtr& req,
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
    
    Memory* memory = emulator->GetMemory();
    if (!memory) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Memory subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Get command from JSON body (optional, defaults to "RUN")
    std::string command = "RUN";
    auto json = req->getJsonObject();
    if (json && json->isMember("command")) {
        command = (*json)["command"].asString();
    }
    
    Json::Value ret;
    
    // Use new runCommand API - handles menu navigation automatically
    auto result = BasicEncoder::runCommand(emulator.get(), command);
    
    ret["success"] = result.success;
    ret["command"] = command;
    ret["message"] = result.message;
    
    // Include state info
    switch (result.state) {
        case BasicEncoder::BasicState::Basic48K:
            ret["basic_mode"] = "48K";
            break;
        case BasicEncoder::BasicState::Basic128K:
            ret["basic_mode"] = "128K";
            break;
        case BasicEncoder::BasicState::TRDOS_Active:
        case BasicEncoder::BasicState::TRDOS_SOS_Call:
            ret["basic_mode"] = "trdos";
            break;
        default:
            ret["basic_mode"] = "unknown";
            break;
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(result.success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/basic/inject
/// @brief Inject a BASIC program into memory (without executing)
void EmulatorAPI::basicInject(const HttpRequestPtr& req,
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
    
    // Accept both 'program' (legacy) and 'command' (new) parameters
    std::string command;
    if (json && json->isMember("command")) {
        command = (*json)["command"].asString();
    } else if (json && json->isMember("program")) {
        command = (*json)["program"].asString();
    } else {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'command' or 'program' parameter in request body";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Use autoNavigateAndInject - handles menu navigation automatically
    auto result = BasicEncoder::autoNavigateAndInject(emulator.get(), command);
    
    Json::Value ret;
    ret["success"] = result.success;
    ret["message"] = result.message;
    
    // Include detected state
    switch (result.state) {
        case BasicEncoder::BasicState::Basic48K:
            ret["state"] = "basic48k";
            break;
        case BasicEncoder::BasicState::Basic128K:
            ret["state"] = "basic128k";
            break;
        case BasicEncoder::BasicState::TRDOS_Active:
        case BasicEncoder::BasicState::TRDOS_SOS_Call:
            ret["state"] = "trdos";
            break;
        default:
            ret["state"] = "unknown";
            break;
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(result.success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/basic/extract
/// @brief Extract the current BASIC program from memory
void EmulatorAPI::basicExtract(const HttpRequestPtr& req,
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
    
    Memory* memory = emulator->GetMemory();
    if (!memory) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Memory subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    BasicExtractor extractor;
    std::string basicListing = extractor.extractFromMemory(memory);
    
    Json::Value ret;
    if (basicListing.empty()) {
        ret["success"] = true;
        ret["program"] = "";
        ret["message"] = "No BASIC program found in memory";
    } else {
        ret["success"] = true;
        ret["program"] = basicListing;
        ret["message"] = "BASIC program extracted successfully";
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/basic/clear
/// @brief Clear the BASIC program in memory (equivalent to NEW command)
void EmulatorAPI::basicClear(const HttpRequestPtr& req,
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
    
    Memory* memory = emulator->GetMemory();
    if (!memory) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Memory subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    BasicEncoder encoder;
    bool success = encoder.loadProgram(memory, "");
    
    Json::Value ret;
    ret["success"] = success;
    if (success) {
        ret["message"] = "BASIC program cleared";
    } else {
        ret["message"] = "Failed to clear BASIC program";
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/basic/state
/// @brief Get the current BASIC environment state
void EmulatorAPI::basicState(const HttpRequestPtr& req,
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
    
    Memory* memory = emulator->GetMemory();
    if (!memory) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Memory subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto state = BasicEncoder::detectState(memory);
    bool isInEditor = BasicEncoder::isInBasicEditor(memory);
    
    Json::Value ret;
    ret["success"] = true;
    ret["in_editor"] = isInEditor;
    
    switch (state) {
        case BasicEncoder::BasicState::Menu128K:
            ret["state"] = "menu128k";
            ret["description"] = "In 128K main menu";
            ret["ready_for_commands"] = false;
            break;
        case BasicEncoder::BasicState::Basic128K:
            ret["state"] = "basic128k";
            ret["description"] = "In 128K BASIC editor";
            ret["ready_for_commands"] = true;
            break;
        case BasicEncoder::BasicState::Basic48K:
            ret["state"] = "basic48k";
            ret["description"] = "In 48K BASIC mode";
            ret["ready_for_commands"] = true;
            break;
        default:
            ret["state"] = "unknown";
            ret["description"] = "Unable to determine state";
            ret["ready_for_commands"] = false;
            break;
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/basic/mode
/// @brief Switch BASIC mode (from 128K menu to 48K or 128K BASIC)
void EmulatorAPI::basicMode(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback,
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
    
    Memory* memory = emulator->GetMemory();
    if (!memory)
    {
        Json::Value error;
        error["error"] = "Memory Error";
        error["message"] = "Memory not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Parse request body for mode
    auto json = req->getJsonObject();
    std::string mode = json ? (*json)["mode"].asString() : "";
    
    if (mode.empty())
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Mode is required. Use '48k' or '128k'.";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Check current state
    auto currentState = BasicEncoder::detectState(memory);
    
    Json::Value ret;
    
    if (mode == "48k" || mode == "48K")
    {
        if (currentState == BasicEncoder::BasicState::Menu128K)
        {
            BasicEncoder::navigateToBasic48K(emulator.get());
            ret["success"] = true;
            ret["message"] = "Switched to 48K BASIC mode from menu";
            ret["mode"] = "48k";
        }
        else if (currentState == BasicEncoder::BasicState::Basic48K)
        {
            ret["success"] = true;
            ret["message"] = "Already in 48K BASIC mode";
            ret["mode"] = "48k";
        }
        else
        {
            ret["success"] = false;
            ret["message"] = "Cannot switch to 48K mode from current state";
            ret["current_state"] = static_cast<int>(currentState);
        }
    }
    else if (mode == "128k" || mode == "128K")
    {
        if (currentState == BasicEncoder::BasicState::Menu128K)
        {
            BasicEncoder::navigateToBasic128K(emulator.get());
            ret["success"] = true;
            ret["message"] = "Switched to 128K BASIC mode from menu";
            ret["mode"] = "128k";
        }
        else if (currentState == BasicEncoder::BasicState::Basic128K)
        {
            ret["success"] = true;
            ret["message"] = "Already in 128K BASIC mode";
            ret["mode"] = "128k";
        }
        else
        {
            ret["success"] = false;
            ret["message"] = "Cannot switch to 128K mode from current state";
            ret["current_state"] = static_cast<int>(currentState);
        }
    }
    else
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid mode. Use '48k' or '128k'.";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
