// WebAPI State Screen Inspection Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include "../common/statenode_json.h"
#include "emulator/state/devicestate.h"
#include "../common/jsonnumber.h"
#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <common/stringhelper.h>
#include <emulator/config.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/platform.h>
#include <emulator/video/screen.h>
#include <emulator/video/screendigest.h>
#include <json/json.h>

#include <sstream>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief Get screen configuration
void EmulatorAPI::getStateScreen(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Unable to access emulator context";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Check if verbose mode is requested
    bool verbose = false;
    auto params = req->getParameters();
    if (params.find("verbose") != params.end())
    {
        const std::string& verboseParam = params.at("verbose");
        verbose = (verboseParam == "true" || verboseParam == "1" || verboseParam == "yes");
    }

    // One report for every automation module (DeviceState::Screen)
    Json::Value ret = StateNodeToJson(DeviceState::Screen(context, verbose));
    // Legacy aliases kept for existing clients
    ret["is_128k"] = ret["shadow_screen_capable"];
    ret["display_mode"] = ret["video_mode"];

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/screen/mode
/// @brief Get video mode details
void EmulatorAPI::getStateScreenMode(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pScreen)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Unable to access emulator context or screen";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // One report for every automation module (DeviceState::ScreenMode, the per-mode flags included)
    Json::Value ret = StateNodeToJson(DeviceState::ScreenMode(context));


    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/screen/flash
/// @brief Get flash state
void EmulatorAPI::getStateScreenFlash(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Unable to access emulator context";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret = StateNodeToJson(DeviceState::ScreenFlash(context));

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/screen/attributes
/// @brief Per-cell ink/paper/bright/flash decoded from screen attribute memory
void EmulatorAPI::getStateScreenAttributes(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Unable to access emulator context";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    int screen = -1;
    const std::string screenText = req->getParameter("screen");
    if (!screenText.empty())
        screen = std::atoi(screenText.c_str());

    Json::Value ret = StateNodeToJson(DeviceState::ScreenAttributes(context, screen));

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/video/beam
/// @brief Current raster beam position — t-state, line, dot, zone — plus
///        frame timing derived from the machine model. Zone boundaries follow
///        the canonical Screen::InitRaster calculation (RasterState).
void EmulatorAPI::getBeamPosition(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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
    if (!context)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Unable to access emulator context";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const CONFIG& config = context->config;
    Screen* screen = context->pScreen;

    if (!screen || config.t_line == 0 || config.frame == 0)
    {
        Json::Value error;
        error["error"] = "Conflict";
        error["message"] = "Machine model timing is not initialized yet";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k409Conflict);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // One report for every automation module (DeviceState::VideoBeam, on VideoMapService)
    Json::Value ret = StateNodeToJson(DeviceState::VideoBeam(context));
    ret.removeMember("available");

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/screen/digest
/// @brief Deterministic FNV-1a 64 digest over screen memory with poll-driven
///        change tracking. Query params:
///        - mode=active     Hash the RAM pages the CURRENT video mode actually
///                          displays (ATM hardware modes follow the 7FFD-selected
///                          bit-plane pair {videoPage-4, videoPage}); default
///                          `default` keeps the model-dependent pages 5/7
///        - banks=5,7     Physical RAM pages to hash (default: model-dependent;
///                        overrides mode=active)
///        - start/end     Explicit Z80 address range override (hex or decimal;
///                        overrides mode=active and banks)
///        - include_border=false  Drop the border color from the combined digest
void EmulatorAPI::getStateScreenDigest(const HttpRequestPtr& req,
                                       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
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
    if (!context || !context->pMemory || !context->pScreen)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Emulator context is not fully initialized";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // One computation for every interface (ScreenDigestCompute; DeviceState::ScreenDigestReport)
    auto param = [&](const char* name) { return req->getParameter(name); };
    ScreenDigestQuery query;
    std::string error;
    if (!ScreenDigestCompute::QueryFromStrings(param("mode"), param("banks"), param("start"), param("end"),
                                               param("include_border"), query, error))
    {
        Json::Value err;
        err["error"] = "Bad Request";
        err["message"] = error;
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    Json::Value ret;
    ret["emulator_id"] = id;
    const Json::Value report = StateNodeToJson(DeviceState::ScreenDigestReport(context, query));
    for (const std::string& key : report.getMemberNames())
        ret[key] = report[key];

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
