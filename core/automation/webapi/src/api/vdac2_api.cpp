// WebAPI: TS-Conf VDAC2 card (FT812) control.
// Implements /vdac2/capture endpoints: the FT812's bus traffic written to an
// .evr replay stream (docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-test-corpus.md §4),
// and /vdac2/metrics: the FT812 line budget metrics (line-budget-metrics.md).
// Every surface goes through Vdac2Control, so all of them answer the same.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/platforms/tsconf/vdac2control.h>
#include <json/json.h>

#include "../emulator_api.h"

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

void sendVdac2Error(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode code,
                    const std::string& error, const std::string& message)
{
    Json::Value body;
    body["error"] = error;
    body["message"] = message;
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

/// The emulator's context with a VDAC2 card; sends the error response and
/// returns nullptr otherwise (404 no emulator, 409 no card)
EmulatorContext* vdac2Context(const std::string& id, std::function<void(const HttpResponsePtr&)>& callback)
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        sendVdac2Error(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator with specified ID not found");
        return nullptr;
    }
    std::string error;
    EmulatorContext* context = emulator->GetContext();
    if (!Vdac2Control::HasCard(context, &error))
    {
        sendVdac2Error(callback, HttpStatusCode::k409Conflict, "No VDAC2 Card", error);
        return nullptr;
    }
    return context;
}

/// The capture status JSON every capture endpoint answers with
void sendCaptureStatus(EmulatorContext* context, std::function<void(const HttpResponsePtr&)>& callback)
{
    Vdac2Control::CaptureStatus status;
    std::string error;
    if (!Vdac2Control::GetCaptureStatus(context, status, &error))
    {
        sendVdac2Error(callback, HttpStatusCode::k409Conflict, "No VDAC2 Card", error);
        return;
    }
    Json::Value body;
    body["capturing"] = status.capturing;
    body["path"] = status.path;
    body["bytes"] = static_cast<Json::UInt64>(status.bytesWritten);
    body["selects"] = static_cast<Json::UInt64>(status.selects);
    body["exchanges"] = static_cast<Json::UInt64>(status.exchanges);
    body["frames"] = static_cast<Json::UInt64>(status.frames);
    body["start_clock"] = static_cast<Json::UInt64>(status.startClock);
    body["last_clock"] = static_cast<Json::UInt64>(status.lastClock);
    body["format"] = "evr";
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(HttpStatusCode::k200OK);
    addCorsHeaders(resp);
    callback(resp);
}

/// The line budget metrics JSON both metrics endpoints answer with
void sendMetrics(EmulatorContext* context, bool withLines, bool inFlight,
                 std::function<void(const HttpResponsePtr&)>& callback)
{
    Vdac2Control::FrameMetrics m;
    std::string error;
    if (!Vdac2Control::GetFrameMetrics(context, m, withLines, inFlight, &error))
    {
        sendVdac2Error(callback, HttpStatusCode::k409Conflict, "No VDAC2 Card", error);
        return;
    }
    Json::Value body;
    body["valid"] = m.valid;
    body["frame"] = static_cast<Json::UInt64>(m.frame);
    body["lines"] = m.lines;
    body["hard_budget"] = m.hardBudget;
    body["soft_budget"] = m.softBudget;
    body["worst_line"] = m.worstLine;
    body["worst_clocks"] = m.worstClocks;
    body["total_clocks"] = static_cast<Json::UInt64>(m.totalClocks);
    body["lines_over_soft"] = m.linesOverSoft;
    body["lines_over_hard"] = m.linesOverHard;
    body["margin"] = m.margin;
    body["measure_always"] = m.measureAlways;
    if (withLines)
    {
        body["line_clocks"] = Json::Value(Json::arrayValue);
        for (uint16_t clocks : m.lineClocks)
            body["line_clocks"].append(clocks);
    }
    if (inFlight)
    {
        Json::Value& flight = body["in_flight"];
        flight["known"] = m.inFlightKnown;
        if (!m.inFlightKnown)
            flight["reason"] = "the machine is running: pause it to read the frame in flight";
        flight["lines_passed"] = m.inFlightLinesPassed;
        if (withLines && m.inFlightKnown)
        {
            flight["line_clocks"] = Json::Value(Json::arrayValue);
            for (int32_t clocks : m.inFlightLineClocks)
                flight["line_clocks"].append(clocks);
        }
    }
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(HttpStatusCode::k200OK);
    addCorsHeaders(resp);
    callback(resp);
}

bool QueryFlag(const HttpRequestPtr& req, const char* name)
{
    const std::string value = req->getParameter(name);
    return value == "1" || value == "true" || value == "yes";
}

} // namespace

/// @brief GET /api/v1/emulator/{id}/vdac2/metrics[?lines=1][&in_flight=1]
void EmulatorAPI::vdac2Metrics(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    EmulatorContext* context = vdac2Context(id, callback);
    if (!context)
        return;
    sendMetrics(context, QueryFlag(req, "lines"), QueryFlag(req, "in_flight"), callback);
}

/// @brief PUT /api/v1/emulator/{id}/vdac2/metrics
/// Body: {"margin": 0..50, "measure_always": bool}, each optional
void EmulatorAPI::vdac2MetricsSet(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    EmulatorContext* context = vdac2Context(id, callback);
    if (!context)
        return;
    auto json = req->getJsonObject();
    if (!json || !json->isObject())
    {
        sendVdac2Error(callback, HttpStatusCode::k400BadRequest, "Bad Request",
                       "JSON body expected: {\"margin\": 0..50, \"measure_always\": bool}");
        return;
    }
    std::string error;
    if (json->isMember("margin"))
    {
        const Json::Value& margin = (*json)["margin"];
        if (!margin.isUInt() || !Vdac2Control::SetLineBudgetMargin(context, margin.asUInt(), &error))
        {
            sendVdac2Error(callback, HttpStatusCode::k400BadRequest, "Bad Request",
                           error.empty() ? "margin is a percent from 0 to 50" : error);
            return;
        }
    }
    if (json->isMember("measure_always"))
    {
        if (!(*json)["measure_always"].isBool())
        {
            sendVdac2Error(callback, HttpStatusCode::k400BadRequest, "Bad Request", "measure_always is a boolean");
            return;
        }
        Vdac2Control::SetMeasureAlways(context, (*json)["measure_always"].asBool(), &error);
    }
    sendMetrics(context, false, false, callback);
}

/// @brief POST /api/v1/emulator/{id}/vdac2/capture/start
/// Body: {"path": "/tmp/game.evr"}; replaces a capture in progress
void EmulatorAPI::vdac2CaptureStart(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id) const
{
    EmulatorContext* context = vdac2Context(id, callback);
    if (!context)
        return;
    auto json = req->getJsonObject();
    if (!json || !json->isMember("path") || (*json)["path"].asString().empty())
    {
        sendVdac2Error(callback, HttpStatusCode::k400BadRequest, "Bad Request", "JSON body with 'path' expected");
        return;
    }
    std::string error;
    if (!Vdac2Control::StartCapture(context, (*json)["path"].asString(), &error))
    {
        sendVdac2Error(callback, HttpStatusCode::k500InternalServerError, "Capture Failed", error);
        return;
    }
    sendCaptureStatus(context, callback);
}

/// @brief POST /api/v1/emulator/{id}/vdac2/capture/stop
void EmulatorAPI::vdac2CaptureStop(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id) const
{
    (void)req;
    EmulatorContext* context = vdac2Context(id, callback);
    if (!context)
        return;
    std::string error;
    if (!Vdac2Control::StopCapture(context, &error))
    {
        sendVdac2Error(callback, HttpStatusCode::k409Conflict, "Not Capturing", error);
        return;
    }
    sendCaptureStatus(context, callback);
}

/// @brief GET /api/v1/emulator/{id}/vdac2/capture/status
void EmulatorAPI::vdac2CaptureStatus(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    (void)req;
    EmulatorContext* context = vdac2Context(id, callback);
    if (!context)
        return;
    sendCaptureStatus(context, callback);
}

} // namespace v1
} // namespace api
