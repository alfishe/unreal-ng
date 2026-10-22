/// @file video_window_api.cpp
/// @brief GUI window display settings endpoints: GPU acceleration, CRT effects and profile, temporal blending, full
/// screen, scale, toolbar / status bar, HUD, on-screen keyboard, the Next layers window; the active CRT profile's
/// parameters; the overscan frame's viewport. Thin: validation and the report are WindowDisplay / VideoViewport helpers
/// shared with CLI, Lua and Python (MCP capture_media calls these routes). The window is the front end's: headless
/// every window call answers 409 no-window (the viewport is the machine's and answers headless too).

#include "../../../windowdisplay.h"
#include "../common/statenode_json.h"
#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/video/viewportcontrol.h>
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
    else if (value.isDouble())
        out = WindowDisplay::NumberWord(value.asDouble());
    else if (value.isString())
        out = value.asString();
    else
        return false;
    return true;
}

/// The parameters, or the error with its status: 400 bad-parameter, 409 no-window / unavailable
void RespondParameters(std::function<void(const HttpResponsePtr&)>& callback, const WindowDisplay::ParametersResult& r)
{
    if (r.Ok())
    {
        Respond(callback, StateNodeToJson(WindowDisplay::CrtParametersReport(r.parameters)), k200OK);
        return;
    }
    RespondError(callback, r.status == WindowDisplay::Status::BadParameter ? k400BadRequest : k409Conflict,
                 WindowDisplay::StatusName(r.status), r.error);
}

/// {name: number} into `out`; false with `error` for a value that is not a number
bool NumberMembers(const Json::Value& object, const char* field, std::vector<std::pair<std::string, double>>& out,
                   std::string& error)
{
    if (object.isNull())
        return true;
    if (!object.isObject())
    {
        error = std::string("'") + field + "' takes an object of parameter: number";
        return false;
    }
    for (const std::string& name : object.getMemberNames())
    {
        const Json::Value& value = object[name];
        double number = 0;
        if (value.isNumeric())
            number = value.asDouble();
        else if (!value.isString() || !WindowDisplay::ParseNumber(value.asString(), number))
        {
            error = std::string("'") + field + "." + name + "' takes a number";
            return false;
        }
        out.emplace_back(name, number);
    }
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
        // The HUD's notices and the Next layers' ticks may come as one object: {"hud_notices": {"ram-page": false}}
        const Json::Value& member = (*json)[key];
        const char* prefix = key == "hud_notices" ? WindowDisplay::kHudNoticePrefix
                             : key == "next_layers_shown" ? WindowDisplay::kNextLayerPrefix
                                                          : nullptr;
        if (prefix != nullptr && member.isObject())
        {
            for (const std::string& id : member.getMemberNames())
            {
                std::string word;
                std::string error;
                if (!Word(member[id], word))
                    error = "'" + key + "." + id + "' takes a boolean";
                if (!error.empty() || !WindowDisplay::ApplyWord(change, prefix + id, word, error))
                {
                    RespondError(callback, k400BadRequest, "bad-parameter", error);
                    return;
                }
            }
            continue;
        }
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

/// @brief GET /api/v1/emulator/{id}/video/window/crt_params
void EmulatorAPI::getVideoWindowCrtParams(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback,
                                          const std::string& id) const
{
    (void)req;
    if (KnownEmulator(id, callback))
        RespondParameters(callback, WindowDisplay::GetCrtParameters());
}

/// @brief PUT|POST /api/v1/emulator/{id}/video/window/crt_params — body {values: {name: number}, steps: {name: steps},
/// reset: true | [names]}
void EmulatorAPI::setVideoWindowCrtParams(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback,
                                          const std::string& id) const
{
    if (!KnownEmulator(id, callback))
        return;
    auto json = req->getJsonObject();
    if (!json || !json->isObject())
    {
        RespondError(callback, k400BadRequest, "bad-parameter",
                     "Body must be a JSON object: {values: {name: number}, steps: {name: steps}, reset: true | [names]}");
        return;
    }
    CrtParameterChange change;
    std::string error;
    for (const std::string& key : json->getMemberNames())
    {
        if (key != "values" && key != "steps" && key != "reset")
        {
            RespondError(callback, k400BadRequest, "bad-parameter",
                         "Unknown field '" + key + "': use values, steps, reset");
            return;
        }
    }
    if (!NumberMembers((*json)["values"], "values", change.values, error) ||
        !NumberMembers((*json)["steps"], "steps", change.steps, error))
    {
        RespondError(callback, k400BadRequest, "bad-parameter", error);
        return;
    }
    const Json::Value& reset = (*json)["reset"];
    if (reset.isBool())
        change.resetAll = reset.asBool();
    else if (reset.isString())
        change.reset.push_back(reset.asString());
    else if (reset.isArray())
    {
        for (const Json::Value& name : reset)
            change.reset.push_back(name.asString());
    }
    else if (!reset.isNull())
    {
        RespondError(callback, k400BadRequest, "bad-parameter", "'reset' takes true, a name or a list of names");
        return;
    }
    RespondParameters(callback, WindowDisplay::SetCrtParameters(change));
}

/// @brief POST /api/v1/emulator/{id}/video/window/crt_params/reset
void EmulatorAPI::resetVideoWindowCrtParams(const HttpRequestPtr& req,
                                            std::function<void(const HttpResponsePtr&)>&& callback,
                                            const std::string& id) const
{
    (void)req;
    if (!KnownEmulator(id, callback))
        return;
    CrtParameterChange change;
    change.resetAll = true;
    RespondParameters(callback, WindowDisplay::SetCrtParameters(change));
}

/// @brief GET /api/v1/emulator/{id}/video/viewport
void EmulatorAPI::getVideoViewport(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    (void)req;
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        RespondError(callback, k404NotFound, "not-found", "Emulator with specified ID not found");
        return;
    }
    Respond(callback, StateNodeToJson(VideoViewport::Report(*emulator)), k200OK);
}

/// @brief PUT|POST /api/v1/emulator/{id}/video/viewport — body {viewport: full | symmetric | standard | screen_only |
/// next | prev}
void EmulatorAPI::setVideoViewport(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        RespondError(callback, k404NotFound, "not-found", "Emulator with specified ID not found");
        return;
    }
    auto json = req->getJsonObject();
    if (!json || !json->isObject() || !(*json)["viewport"].isString())
    {
        RespondError(callback, k400BadRequest, "bad-parameter",
                     "Body must be {\"viewport\": \"" + VideoViewport::NameList() + " | next | prev\"}");
        return;
    }
    const VideoViewport::Result r = VideoViewport::SetViewport(*emulator, (*json)["viewport"].asString());
    if (!r.Ok())
    {
        RespondError(callback, r.status == 400 ? k400BadRequest : k409Conflict,
                     r.status == 400 ? "bad-parameter" : "unavailable", r.error);
        return;
    }
    Json::Value body = StateNodeToJson(VideoViewport::Report(*emulator));
    body["changed"] = r.changed;
    Respond(callback, body, k200OK);
}
} // namespace v1
} // namespace api
