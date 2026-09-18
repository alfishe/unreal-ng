// WebAPI Kempston Mouse Injection Implementation
// Design: docs/inprogress/2026-09-12-kempston-mouse/automation-interfaces.md §4.4
//
// All range validation lives in DebugMouseManager. Handlers only check JSON presence and
// types, resolve button names and map MouseInjectResult to an HTTP response.

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/ports/portdecoder.h>
#include <debugger/debugmanager.h>
#include <debugger/mouse/debugmousemanager.h>
#include <json/json.h>

#include <cstdint>
#include <limits>
#include <optional>

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

void sendJson(const Callback& callback, const Json::Value& body, HttpStatusCode code = HttpStatusCode::k200OK)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

void sendError(const Callback& callback, HttpStatusCode code, const std::string& reason, const std::string& message)
{
    Json::Value error;
    error["error"] = reason;
    error["message"] = message;
    sendJson(callback, error, code);
}

void sendBadRequest(const Callback& callback, const std::string& message)
{
    sendError(callback, HttpStatusCode::k400BadRequest, "Bad Request", message);
}

/// 404 when the emulator id is unknown. Returns nullptr after sending the response.
std::shared_ptr<Emulator> findEmulator(const std::string& id, const Callback& callback)
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
        sendError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator with specified ID not found");
    return emulator;
}

/// 404 / 500 lookup of the mouse funnel. Returns nullptr after sending the response.
DebugMouseManager* findMouseManager(const std::string& id, const Callback& callback)
{
    auto emulator = findEmulator(id, callback);
    if (!emulator)
        return nullptr;

    EmulatorContext* context = emulator->GetContext();
    DebugMouseManager* mouse =
        (context && context->pDebugManager) ? context->pDebugManager->GetMouseManager() : nullptr;
    if (!mouse)
        sendError(callback, HttpStatusCode::k500InternalServerError, "Internal Error", "Mouse manager not available");
    return mouse;
}

Json::Value stateToJson(const MouseStateSnapshot& state)
{
    Json::Value json;
    json["available"] = state.available;
    json["present"] = state.present;
    json["wheel_enabled"] = state.wheelEnabled;
    json["x"] = state.x;
    json["y"] = state.y;

    Json::Value buttons;
    buttons["left"] = state.IsPressed(MouseButton::Left);
    buttons["right"] = state.IsPressed(MouseButton::Right);
    buttons["middle"] = state.IsPressed(MouseButton::Middle);
    json["buttons"] = buttons;

    json["button_mask"] = state.buttonMask;
    json["wheel"] = state.wheel;

    Json::Value ports;
    ports["FADF"] = state.portButtons;
    ports["FBDF"] = state.portX;
    ports["FFDF"] = state.portY;
    json["ports"] = ports;

    if (state.pendingClickButton.has_value())
    {
        json["pending_click"]["button"] = DebugMouseManager::GetButtonName(*state.pendingClickButton);
        json["pending_click"]["frames_left"] = state.pendingClickFramesLeft;
    }
    else
    {
        json["pending_click"] = Json::Value(Json::nullValue);
    }

    json["ttd_journal"] = state.journalSupported ? "supported" : "unsupported";
    return json;
}

/// Maps a manager result to the response. On success `body` gets success/message/warning/state.
void sendResult(const Callback& callback, DebugMouseManager* mouse, const MouseInjectResult& result, Json::Value body)
{
    switch (result.status)
    {
        case MouseInjectStatus::Ok:
            break;
        case MouseInjectStatus::InvalidArgument:
            sendBadRequest(callback, result.message);
            return;
        case MouseInjectStatus::ReplayActive:
            sendError(callback, HttpStatusCode::k409Conflict, "Conflict", result.message);
            return;
        case MouseInjectStatus::NoDevice:
        default:
            sendError(callback, HttpStatusCode::k500InternalServerError, "Internal Error",
                      "Mouse manager not available");
            return;
    }

    body["success"] = true;
    body["message"] = result.message;
    if (!result.warning.empty())
        body["warning"] = result.warning;
    body["state"] = stateToJson(mouse->GetState());
    sendJson(callback, body);
}

bool isJsonInteger(const Json::Value& value)
{
    return value.type() == Json::intValue || value.type() == Json::uintValue;
}

/// Reads an integer field (JSON int only; strings/floats rejected). Values outside `int` saturate
/// so the manager reports them as out of range. Returns false after sending a 400.
bool readIntField(const Json::Value& json, const char* name, int& out, const Callback& callback)
{
    const Json::Value& value = json[name];
    if (!isJsonInteger(value))
    {
        sendBadRequest(callback, std::string("'") + name + "' must be an integer");
        return false;
    }

    if (!value.isInt64())
        out = std::numeric_limits<int>::max();  // uint64 beyond int64
    else
    {
        const int64_t v = value.asInt64();
        if (v > std::numeric_limits<int>::max())
            out = std::numeric_limits<int>::max();
        else if (v < std::numeric_limits<int>::min())
            out = std::numeric_limits<int>::min();
        else
            out = static_cast<int>(v);
    }
    return true;
}

std::optional<MouseButton> resolveButton(const Json::Value& value, const Callback& callback)
{
    std::optional<MouseButton> button;
    if (value.isString())
        button = DebugMouseManager::ResolveButtonName(value.asString());

    if (!button)
    {
        std::string shown = value.isString() ? value.asString() : value.toStyledString();
        while (!shown.empty() && (shown.back() == '\n' || shown.back() == ' '))
            shown.pop_back();
        sendBadRequest(callback, "Unknown button '" + shown + "'. Valid: left, right, middle (l, r, m)");
    }
    return button;
}

/// Reads the required "button" field. Returns nullopt after sending a 400.
std::optional<MouseButton> readButtonField(const std::shared_ptr<Json::Value>& json, const Callback& callback)
{
    if (!json || !json->isObject() || !json->isMember("button"))
    {
        sendBadRequest(callback, "Missing 'button' field in request body");
        return std::nullopt;
    }
    return resolveButton((*json)["button"], callback);
}

}  // namespace

/// @brief POST /api/v1/emulator/{id}/mouse/move  {"dx":int, "dy":int}
void EmulatorAPI::mouseMove(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    DebugMouseManager* mouse = findMouseManager(id, callback);
    if (!mouse)
        return;

    auto json = req->getJsonObject();
    if (!json || !json->isObject() || (!json->isMember("dx") && !json->isMember("dy")))
    {
        sendBadRequest(callback, "Missing 'dx' or 'dy' field in request body");
        return;
    }

    int dx = 0;
    int dy = 0;
    if (json->isMember("dx") && !readIntField(*json, "dx", dx, callback))
        return;
    if (json->isMember("dy") && !readIntField(*json, "dy", dy, callback))
        return;

    Json::Value body;
    body["dx"] = dx;
    body["dy"] = dy;
    sendResult(callback, mouse, mouse->Move(dx, dy), body);
}

/// @brief POST /api/v1/emulator/{id}/mouse/press  {"button":"left"}
void EmulatorAPI::mousePress(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    DebugMouseManager* mouse = findMouseManager(id, callback);
    if (!mouse)
        return;

    auto button = readButtonField(req->getJsonObject(), callback);
    if (!button)
        return;

    Json::Value body;
    body["button"] = DebugMouseManager::GetButtonName(*button);
    sendResult(callback, mouse, mouse->PressButton(*button), body);
}

/// @brief POST /api/v1/emulator/{id}/mouse/release  {"button":"left"}
void EmulatorAPI::mouseRelease(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    DebugMouseManager* mouse = findMouseManager(id, callback);
    if (!mouse)
        return;

    auto button = readButtonField(req->getJsonObject(), callback);
    if (!button)
        return;

    Json::Value body;
    body["button"] = DebugMouseManager::GetButtonName(*button);
    sendResult(callback, mouse, mouse->ReleaseButton(*button), body);
}

/// @brief POST /api/v1/emulator/{id}/mouse/click  {"button":"left", "frames":2}
void EmulatorAPI::mouseClick(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    DebugMouseManager* mouse = findMouseManager(id, callback);
    if (!mouse)
        return;

    auto json = req->getJsonObject();
    auto button = readButtonField(json, callback);
    if (!button)
        return;

    uint32_t frames = DebugMouseManager::DEFAULT_CLICK_FRAMES;
    if (json->isMember("frames"))
    {
        const Json::Value& value = (*json)["frames"];
        if (!isJsonInteger(value))
        {
            sendBadRequest(callback, "'frames' must be an integer");
            return;
        }
        if (!value.isUInt())  // negative or beyond 32 bits: cannot reach the manager as uint32
        {
            const std::string shown =
                value.isInt64() ? std::to_string(value.asInt64()) : std::to_string(value.asUInt64());
            sendBadRequest(callback, "frames=" + shown + " out of range 1.." +
                                         std::to_string(DebugMouseManager::MAX_CLICK_FRAMES));
            return;
        }
        frames = value.asUInt();
    }

    Json::Value body;
    body["button"] = DebugMouseManager::GetButtonName(*button);
    body["frames"] = frames;
    sendResult(callback, mouse, mouse->Click(*button, frames), body);
}

/// @brief POST /api/v1/emulator/{id}/mouse/buttons  {"pressed":["left","middle"]}
void EmulatorAPI::mouseButtons(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    DebugMouseManager* mouse = findMouseManager(id, callback);
    if (!mouse)
        return;

    auto json = req->getJsonObject();
    if (!json || !json->isObject() || !json->isMember("pressed"))
    {
        sendBadRequest(callback, "Missing 'pressed' field in request body");
        return;
    }
    if (!(*json)["pressed"].isArray())
    {
        sendBadRequest(callback, "'pressed' must be an array of button names");
        return;
    }

    uint8_t pressedBits = 0;
    for (const auto& item : (*json)["pressed"])
    {
        auto button = resolveButton(item, callback);
        if (!button)
            return;
        pressedBits |= static_cast<uint8_t>(*button);
    }

    Json::Value body;
    body["pressed"] = Json::Value(Json::arrayValue);
    for (MouseButton button : {MouseButton::Left, MouseButton::Right, MouseButton::Middle})
    {
        if (pressedBits & static_cast<uint8_t>(button))
            body["pressed"].append(DebugMouseManager::GetButtonName(button));
    }
    sendResult(callback, mouse, mouse->SetPressedButtons(pressedBits), body);
}

/// @brief POST /api/v1/emulator/{id}/mouse/wheel  {"steps":int}
void EmulatorAPI::mouseWheel(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    DebugMouseManager* mouse = findMouseManager(id, callback);
    if (!mouse)
        return;

    auto json = req->getJsonObject();
    if (!json || !json->isObject() || !json->isMember("steps"))
    {
        sendBadRequest(callback, "Missing 'steps' field in request body");
        return;
    }

    int steps = 0;
    if (!readIntField(*json, "steps", steps, callback))
        return;

    Json::Value body;
    body["steps"] = steps;
    sendResult(callback, mouse, mouse->Wheel(steps), body);
}

/// @brief POST /api/v1/emulator/{id}/mouse/release_all
void EmulatorAPI::mouseReleaseAll(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id) const
{
    DebugMouseManager* mouse = findMouseManager(id, callback);
    if (!mouse)
        return;

    sendResult(callback, mouse, mouse->ReleaseAllButtons(), Json::Value(Json::objectValue));
}

/// @brief POST /api/v1/emulator/{id}/mouse/counters  {"x":0..255, "y":0..255}
void EmulatorAPI::mouseSetCounters(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id) const
{
    DebugMouseManager* mouse = findMouseManager(id, callback);
    if (!mouse)
        return;

    auto json = req->getJsonObject();
    if (!json || !json->isObject() || !json->isMember("x") || !json->isMember("y"))
    {
        sendBadRequest(callback, "Missing 'x' or 'y' field in request body");
        return;
    }

    int x = 0;
    int y = 0;
    if (!readIntField(*json, "x", x, callback) || !readIntField(*json, "y", y, callback))
        return;

    Json::Value body;
    body["x"] = x;
    body["y"] = y;
    sendResult(callback, mouse, mouse->SetCounters(x, y), body);
}

/// @brief GET /api/v1/emulator/{id}/mouse/status
void EmulatorAPI::mouseStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    (void)req;

    auto emulator = findEmulator(id, callback);
    if (!emulator)
        return;

    EmulatorContext* context = emulator->GetContext();
    DebugMouseManager* mouse =
        (context && context->pDebugManager) ? context->pDebugManager->GetMouseManager() : nullptr;
    if (!mouse)
    {
        sendError(callback, HttpStatusCode::k500InternalServerError, "Internal Error", "Mouse manager not available");
        return;
    }

    const MouseStateSnapshot state = mouse->GetState();
    Json::Value body = stateToJson(state);
    body["emulator_id"] = id;

    // Routing (mouse design Q4 / gap D-1): `present` alone cannot distinguish
    // "not fitted" from "fitted but shadowed" - report whether the decoder
    // actually routes mouse port reads right now, and why not if it doesn't
    if (context && context->pPortDecoder)
    {
        bool decoded = false;
        std::string note;
        context->pPortDecoder->GetMouseRoutingState(decoded, note);

        Json::Value routing;
        routing["ports_decoded"] = decoded;
        routing["note"] = note;
        body["routing"] = routing;
    }

    if (!state.available)
        body["warning"] = "Mouse device not available";
    else if (!state.present)
        body["warning"] = "mouse not present: guest reads floating bus on the mouse ports";
    sendJson(callback, body);
}

/// @brief GET /api/v1/emulator/{id}/mouse/buttons
void EmulatorAPI::mouseButtonList(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id) const
{
    if (!findEmulator(id, callback))
        return;

    Json::Value body;
    body["emulator_id"] = id;
    body["buttons"] = Json::Value(Json::arrayValue);
    const auto names = DebugMouseManager::GetAllButtonNames();
    for (const auto& name : names)
        body["buttons"].append(name);
    body["aliases"]["l"] = "left";
    body["aliases"]["r"] = "right";
    body["aliases"]["m"] = "middle";
    body["count"] = static_cast<Json::UInt>(names.size());
    sendJson(callback, body);
}

}  // namespace v1
}  // namespace api
