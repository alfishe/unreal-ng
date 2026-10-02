/// @file video_map_api.cpp
/// @brief Video debug translation endpoints (PLAN #42 phase 2): layout, pixel
/// sources, byte -> pixels, text grid. Thin: every report is a DeviceState
/// builder shared with MCP, CLI, Lua and Python.

#include "../common/statenode_json.h"
#include "../emulator_api.h"
#include "emulator/state/devicestate.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

#include <string>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{
// CORS helper (defined in emulator_api.cpp)
extern void addCorsHeaders(HttpResponsePtr& resp);
} // namespace v1
} // namespace api

namespace
{
void Respond(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body, HttpStatusCode code)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

void RespondError(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode code, const char* error,
                  const std::string& message)
{
    Json::Value body;
    body["error"] = error;
    body["message"] = message;
    Respond(callback, body, code);
}

/// The emulator's context, or a 404 / 500 already sent
EmulatorContext* ResolveContext(const std::string& id, std::function<void(const HttpResponsePtr&)>& callback)
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        RespondError(callback, k404NotFound, "Not Found", "Emulator with specified ID not found");
        return nullptr;
    }
    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pScreen)
    {
        RespondError(callback, k500InternalServerError, "Internal Error", "Unable to access emulator screen");
        return nullptr;
    }
    return context;
}

/// Unsigned query parameter, decimal or 0x-hex; false when absent or not a number
bool UIntParam(const HttpRequestPtr& req, const char* name, unsigned& out)
{
    const std::string text = req->getParameter(name);
    if (text.empty())
        return false;
    try
    {
        size_t used = 0;
        const unsigned long value = std::stoul(text, &used, 0);
        if (used != text.size())
            return false;
        out = static_cast<unsigned>(value);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace

namespace api
{
namespace v1
{
/// @brief GET /api/v1/emulator/{id}/video/layout
void EmulatorAPI::getVideoLayout(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    (void)req;
    if (EmulatorContext* context = ResolveContext(id, callback))
        Respond(callback, StateNodeToJson(DeviceState::VideoLayout(context)), k200OK);
}

/// @brief GET /api/v1/emulator/{id}/video/pixel?x=&y=[&layer=] or ?t=
void EmulatorAPI::getVideoPixel(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    EmulatorContext* context = ResolveContext(id, callback);
    if (!context)
        return;
    unsigned t = 0, x = 0, y = 0, layer = 0;
    if (UIntParam(req, "t", t))
    {
        Respond(callback, StateNodeToJson(DeviceState::VideoPixelAtBeam(context, t)), k200OK);
        return;
    }
    if (!UIntParam(req, "x", x) || !UIntParam(req, "y", y))
    {
        RespondError(callback, k400BadRequest, "Bad Request",
                     "Give x and y (surface pixels, optional layer) or t (T-state in the frame)");
        return;
    }
    UIntParam(req, "layer", layer);
    Respond(callback, StateNodeToJson(DeviceState::VideoPixel(context, layer, x, y)), k200OK);
}

/// @brief GET /api/v1/emulator/{id}/video/address?page=&offset= or ?z80= or ?space=&offset=
void EmulatorAPI::getVideoAddress(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    EmulatorContext* context = ResolveContext(id, callback);
    if (!context)
        return;
    unsigned z80 = 0, page = 0, offset = 0;
    if (UIntParam(req, "z80", z80))
    {
        Respond(callback, StateNodeToJson(DeviceState::VideoAddressZ80(context, z80)), k200OK);
        return;
    }
    const std::string space = req->getParameter("space");
    if (!space.empty() && space != "ram")
    {
        // A sprite attribute word or a palette cell: the byte offset in that space
        if (!UIntParam(req, "offset", offset))
        {
            RespondError(callback, k400BadRequest, "Bad Request", "Give offset (the byte offset in the space)");
            return;
        }
        Respond(callback, StateNodeToJson(DeviceState::VideoAddressIn(context, space, 0, offset)), k200OK);
        return;
    }
    if (!UIntParam(req, "page", page) || !UIntParam(req, "offset", offset))
    {
        RespondError(callback, k400BadRequest, "Bad Request", "Give z80 (address) or page and offset (RAM page, 0..0x3FFF)");
        return;
    }
    Respond(callback, StateNodeToJson(DeviceState::VideoAddress(context, page, offset)), k200OK);
}

/// @brief GET /api/v1/emulator/{id}/video/text[?layer=]
void EmulatorAPI::getVideoText(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    EmulatorContext* context = ResolveContext(id, callback);
    if (!context)
        return;
    unsigned layer = 0;
    UIntParam(req, "layer", layer);
    Respond(callback, StateNodeToJson(DeviceState::VideoText(context, layer)), k200OK);
}

void EmulatorAPI::getVideoChanges(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    EmulatorContext* context = ResolveContext(id, callback);
    if (!context)
        return;
    unsigned frames = 2;
    UIntParam(req, "frames", frames);
    Respond(callback, StateNodeToJson(DeviceState::VideoChanges(context, frames)), k200OK);
}
} // namespace v1
} // namespace api
