// WebAPI Kempston Joystick Injection Implementation
// Design: docs/inprogress/2026-09-15-atm-baseconf-highres-ports/tdd-kempston-joystick.md §5
//
// The behavior (parsing, manager calls, JSON, status codes) lives in common/joystickjson.h so core-tests
// can exercise it without a server. Handlers only look the emulator up and send the reply.

#include "../common/joystickjson.h"
#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <debugger/debugmanager.h>
#include <debugger/joystick/debugjoystickmanager.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
using Callback = std::function<void(const HttpResponsePtr&)>;

void sendReply(const Callback& callback, const JoystickWeb::Reply& reply)
{
    auto resp = HttpResponse::newHttpJsonResponse(reply.body);
    resp->setStatusCode(static_cast<HttpStatusCode>(reply.status));
    addCorsHeaders(resp);
    callback(resp);
}

/// 404 for an unknown emulator, 500 when there is no manager. Returns nullptr after sending the response.
DebugJoystickManager* findJoystickManager(const std::string& id, const Callback& callback)
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        sendReply(callback, JoystickWeb::Error(404, "Not Found", "Emulator with specified ID not found"));
        return nullptr;
    }

    EmulatorContext* context = emulator->GetContext();
    DebugJoystickManager* joystick =
        (context && context->pDebugManager) ? context->pDebugManager->GetJoystickManager() : nullptr;
    if (!joystick)
        sendReply(callback, JoystickWeb::Error(500, "Internal Error", "Joystick manager not available"));
    return joystick;
}

void handleVerb(const HttpRequestPtr& req, const Callback& callback, const std::string& id, const char* verb)
{
    DebugJoystickManager* joystick = findJoystickManager(id, callback);
    if (!joystick)
        return;

    const auto json = req->getJsonObject();
    sendReply(callback, JoystickWeb::Handle(*joystick, verb, json.get()));
}

}  // namespace

/// @brief POST /api/v1/emulator/{id}/joystick/press  {"buttons":"up+fire"} or {"buttons":["up","fire"]}
void EmulatorAPI::joystickPress(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    handleVerb(req, callback, id, "press");
}

/// @brief POST /api/v1/emulator/{id}/joystick/release  {"buttons":"up"}
void EmulatorAPI::joystickRelease(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    handleVerb(req, callback, id, "release");
}

/// @brief POST /api/v1/emulator/{id}/joystick/set  {"state":0..255} or {"buttons":[...]}
void EmulatorAPI::joystickSet(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    handleVerb(req, callback, id, "set");
}

/// @brief POST /api/v1/emulator/{id}/joystick/tap  {"buttons":"fire", "frames":2}
void EmulatorAPI::joystickTap(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    handleVerb(req, callback, id, "tap");
}

/// @brief GET /api/v1/emulator/{id}/joystick
void EmulatorAPI::joystickStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    (void)req;
    DebugJoystickManager* joystick = findJoystickManager(id, callback);
    if (!joystick)
        return;
    sendReply(callback, JoystickWeb::StatusReply(*joystick, id));
}

}  // namespace v1
}  // namespace api
