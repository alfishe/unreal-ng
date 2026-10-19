/// @file video_window_api.cpp
/// @brief GUI window display settings endpoints: GPU acceleration, CRT effects and profile, temporal blending. Thin:
/// validation and the report are WindowDisplay helpers shared with CLI, Lua and Python (MCP capture_media calls these
/// routes). The window is the front end's: headless every call answers 409 no-window.

#include "../../../windowdisplay.h"
#include "../common/statenode_json.h"
#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
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

void RespondError(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode code, const char* kind,
                  const std::string& message)
{
    Json::Value body;
    body["error"] = message;
    body["kind"] = kind;
    Respond(callback, body, code);
}

/// The settings, or the error with its status: 400 bad-parameter, 409 no-window / unavailable
void RespondResult(std::function<void(const HttpResponsePtr&)>& callback, const WindowDisplay::Result& r)
{
    if (r.Ok())
    {
        Respond(callback, StateNodeToJson(WindowDisplay::Report(r.settings)), k200OK);
        return;
    }
    RespondError(callback, r.status == WindowDisplay::Status::BadParameter ? k400BadRequest : k409Conflict,
                 WindowDisplay::StatusName(r.status), r.error);
}

/// A JSON scalar as the word ApplyWord takes
bool Word(const Json::Value& value, std::string& out)
{
    if (value.isBool())
        out = value.asBool() ? "on" : "off";
    else if (value.isIntegral())
        out = std::to_string(value.asInt64());
    else if (value.isString())
        out = value.asString();
    else
        return false;
    return true;
}

bool KnownEmulator(const std::string& id, std::function<void(const HttpResponsePtr&)>& callback)
{
    if (EmulatorManager::GetInstance()->GetEmulator(id))
        return true;
    RespondError(callback, k404NotFound, "not-found", "Emulator with specified ID not found");
    return false;
}
} // namespace

namespace api
{
namespace v1
{
/// @brief GET /api/v1/emulator/{id}/video/window
void EmulatorAPI::getVideoWindow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    (void)req;
    if (KnownEmulator(id, callback))
        RespondResult(callback, WindowDisplay::Get());
}

/// @brief PUT|POST /api/v1/emulator/{id}/video/window — body {gpu, crt, crt_profile, blending, blending_frames,
/// blending_weights}
void EmulatorAPI::setVideoWindow(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    if (!KnownEmulator(id, callback))
        return;
    auto json = req->getJsonObject();
    if (!json || !json->isObject())
    {
        RespondError(callback, k400BadRequest, "bad-parameter",
                     std::string("Body must be a JSON object of any of: ") + WindowDisplay::kKeys);
        return;
    }
    WindowDisplayChange change;
    for (const std::string& key : json->getMemberNames())
    {
        std::string word;
        std::string error;
        if (!Word((*json)[key], word))
            error = "'" + key + "' takes a boolean, a number or a string";
        if (!error.empty() || !WindowDisplay::ApplyWord(change, key, word, error))
        {
            RespondError(callback, k400BadRequest, "bad-parameter", error);
            return;
        }
    }
    RespondResult(callback, WindowDisplay::Set(change));
}
} // namespace v1
} // namespace api
