// WebAPI Keyboard Injection Implementation
// 2026-01-27

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <debugger/debugmanager.h>
#include <debugger/keyboard/debugkeyboardmanager.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief POST /api/v1/emulator/{id}/keyboard/tap
/// @brief Tap a key (press and release)
void EmulatorAPI::keyTap(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pDebugManager->GetKeyboardManager())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Keyboard manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("key"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'key' field in request body";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string keyName = (*json)["key"].asString();
    uint16_t frames = json->isMember("frames") ? (*json)["frames"].asUInt() : 2;

    context->pDebugManager->GetKeyboardManager()->TapKey(keyName, frames);

    Json::Value ret;
    ret["success"] = true;
    ret["key"] = keyName;
    ret["frames"] = frames;
    ret["message"] = "Key tapped: " + keyName;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/keyboard/press
/// @brief Press and hold a key
void EmulatorAPI::keyPress(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pDebugManager->GetKeyboardManager())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Keyboard manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("key"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'key' field in request body";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string keyName = (*json)["key"].asString();
    context->pDebugManager->GetKeyboardManager()->PressKey(keyName);

    Json::Value ret;
    ret["success"] = true;
    ret["key"] = keyName;
    ret["message"] = "Key pressed: " + keyName;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/keyboard/release
/// @brief Release a held key
void EmulatorAPI::keyRelease(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pDebugManager->GetKeyboardManager())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Keyboard manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("key"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'key' field in request body";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string keyName = (*json)["key"].asString();
    context->pDebugManager->GetKeyboardManager()->ReleaseKey(keyName);

    Json::Value ret;
    ret["success"] = true;
    ret["key"] = keyName;
    ret["message"] = "Key released: " + keyName;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/keyboard/combo
/// @brief Tap multiple keys simultaneously
void EmulatorAPI::keyCombo(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pDebugManager->GetKeyboardManager())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Keyboard manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("keys") || !(*json)["keys"].isArray())
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing or invalid 'keys' array in request body";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::vector<std::string> keyNames;
    for (const auto& key : (*json)["keys"])
    {
        keyNames.push_back(key.asString());
    }

    uint16_t frames = json->isMember("frames") ? (*json)["frames"].asUInt() : 2;
    context->pDebugManager->GetKeyboardManager()->TapCombo(keyNames, frames);

    Json::Value ret;
    ret["success"] = true;
    ret["keys"] = (*json)["keys"];
    ret["frames"] = frames;
    ret["message"] = "Key combo tapped";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/keyboard/macro
/// @brief Execute a predefined macro
void EmulatorAPI::keyMacro(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pDebugManager->GetKeyboardManager())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Keyboard manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("name"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'name' field in request body";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string macroName = (*json)["name"].asString();
    bool success = context->pDebugManager->GetKeyboardManager()->ExecuteNamedSequence(macroName);

    Json::Value ret;
    ret["success"] = success;
    ret["macro"] = macroName;
    ret["message"] = success ? ("Macro executed: " + macroName) : ("Unknown macro: " + macroName);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    if (!success)
    {
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
    }
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/keyboard/type
/// @brief Type text with auto modifier handling
void EmulatorAPI::keyType(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pDebugManager->GetKeyboardManager())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Keyboard manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("text"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'text' field in request body";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string text = (*json)["text"].asString();
    uint16_t delayFrames = json->isMember("delay_frames") ? (*json)["delay_frames"].asUInt() : 2;
    bool tokenized = json->isMember("tokenized") ? (*json)["tokenized"].asBool() : false;

    if (tokenized)
    {
        context->pDebugManager->GetKeyboardManager()->TypeBasicCommand(text, delayFrames);
    }
    else
    {
        context->pDebugManager->GetKeyboardManager()->TypeText(text, delayFrames);
    }

    Json::Value ret;
    ret["success"] = true;
    ret["text"] = text;
    ret["length"] = static_cast<Json::UInt>(text.length());
    ret["delay_frames"] = delayFrames;
    ret["tokenized"] = tokenized;
    ret["message"] = tokenized ? "BASIC command queued" : "Text queued for typing";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/keyboard/release_all
/// @brief Release all pressed keys
void EmulatorAPI::keyReleaseAll(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pDebugManager->GetKeyboardManager())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Keyboard manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    context->pDebugManager->GetKeyboardManager()->ReleaseAllKeys();

    Json::Value ret;
    ret["success"] = true;
    ret["message"] = "All keys released";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/keyboard/abort
/// @brief Abort current key sequence
void EmulatorAPI::keyAbort(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pDebugManager->GetKeyboardManager())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Keyboard manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    context->pDebugManager->GetKeyboardManager()->AbortSequence();

    Json::Value ret;
    ret["success"] = true;
    ret["message"] = "Key sequence aborted";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/keyboard/status
/// @brief Get keyboard injection status
void EmulatorAPI::keyStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pDebugManager->GetKeyboardManager())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Keyboard manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["sequence_running"] = context->pDebugManager->GetKeyboardManager()->IsSequenceRunning();
    ret["available"] = true;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/keyboard/keys
/// @brief List all recognized key names
void EmulatorAPI::keyList(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    ret["emulator_id"] = id;

    Json::Value keys(Json::arrayValue);
    auto keyNames = DebugKeyboardManager::GetAllKeyNames();
    for (const auto& name : keyNames)
    {
        keys.append(name);
    }
    ret["keys"] = keys;
    ret["count"] = static_cast<Json::UInt>(keyNames.size());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
