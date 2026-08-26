// WebAPI Port Diagnostic Recorder (PDR) implementation
// Implements /profiler/porttrace endpoints.
// Gated by the runtime FeatureManager feature "porttrace" (alias "pt").
// Design: docs/inprogress/2026-08-24-diagnostic-observability/

#include <base/featuremanager.h>
#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/ports/portdecoder.h>
#include <emulator/ports/portdiagrecorder.h>
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

void sendError(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode code,
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

void sendJson(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(HttpStatusCode::k200OK);
    addCorsHeaders(resp);
    callback(resp);
}

/// Resolve the recorder for an emulator id; sends the error response and
/// returns nullptr when unavailable (missing emulator, feature off, ...)
PortDiagnosticRecorder* getRecorder(const std::string& id, PortDecoder** outDecoder,
                                    std::function<void(const HttpResponsePtr&)>& callback)
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    if (!emulator)
    {
        sendError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator with specified ID not found");
        return nullptr;
    }

    auto* context = emulator->GetContext();
    if (!context || !context->pPortDecoder)
    {
        sendError(callback, HttpStatusCode::k500InternalServerError, "Internal Error",
                  "Port decoder not available");
        return nullptr;
    }

    if (!context->pFeatureManager || !context->pFeatureManager->isEnabled(Features::kPortTrace))
    {
        sendError(callback, HttpStatusCode::k409Conflict, "Feature Disabled",
                  "porttrace feature is disabled - enable it first (feature 'porttrace')");
        return nullptr;
    }

    PortDiagnosticRecorder* recorder = context->pPortDecoder->getPortTraceRecorder();
    if (!recorder)
    {
        sendError(callback, HttpStatusCode::k500InternalServerError, "Internal Error",
                  "Port trace recorder not instantiated");
        return nullptr;
    }

    if (outDecoder)
        *outDecoder = context->pPortDecoder;
    return recorder;
}

Json::Value eventToJson(const PortTraceEvent& e, size_t index)
{
    char buf[8];
    Json::Value v;
    v["index"] = static_cast<Json::UInt64>(index);
    v["timestamp"] = static_cast<Json::UInt64>(e.timestamp);
    v["frame"] = e.frameNumber;
    snprintf(buf, sizeof(buf), "0x%04X", e.rawPort);
    v["raw_port"] = buf;
    snprintf(buf, sizeof(buf), "0x%04X", e.decodedPort);
    v["decoded_port"] = buf;
    v["decode_rule"] = e.decodeRuleIndex;
    snprintf(buf, sizeof(buf), "0x%02X", e.value);
    v["value"] = buf;
    snprintf(buf, sizeof(buf), "0x%04X", e.pc);
    v["pc"] = buf;
    v["direction"] = e.isOut() ? "OUT" : "IN";
    v["device"] = PortDiagnosticRecorder::DeviceIdToString(e.deviceId);
    v["decoded"] = e.wasDecoded();
    v["had_handler"] = e.hadHandler();
    v["beta128_gated"] = e.wasBeta128Gated();
    v["handled_inline"] = e.wasHandledInline();
    v["cf_trdos"] = e.cfTrdosActive();
    v["via_legacy"] = (e.flags & PortTraceFlags::kViaLegacyBasePath) != 0;
    return v;
}

const char* sessionStateName(PortTraceSessionState state)
{
    switch (state)
    {
        case PortTraceSessionState::Capturing: return "capturing";
        case PortTraceSessionState::Paused:    return "paused";
        default:                               return "stopped";
    }
}

Json::Value statusToJson(PortDiagnosticRecorder* recorder, PortDecoder* decoder)
{
    Json::Value session;
    session["state"] = sessionStateName(recorder->getSessionState());
    session["events"] = static_cast<Json::UInt64>(recorder->eventCount());
    session["capacity"] = static_cast<Json::UInt64>(recorder->capacity());
    session["total_produced"] = static_cast<Json::UInt64>(recorder->totalProduced());
    session["total_evicted"] = static_cast<Json::UInt64>(recorder->totalEvicted());
    session["total_filtered"] = static_cast<Json::UInt64>(recorder->totalFiltered());
    session["auto_stopped"] = recorder->wasAutoStopped();
    session["overflow"] = recorder->overflowMode() == PortTraceOverflowMode::Ring ? "ring" : "stop-when-full";
    session["filter"] = recorder->describeFilter();

    const PortActivitySummary& summary = decoder->getActivitySummary();
    Json::Value activity;
    activity["frame"] = summary.frameNumber;
    activity["in"] = summary.inCount;
    activity["out"] = summary.outCount;
    activity["unmapped_in"] = summary.unmappedInCount;
    activity["unmapped_out"] = summary.unmappedOutCount;
    activity["beta128_gated"] = summary.beta128GatedCount;
    session["activity"] = activity;

    return session;
}

/// Parse one rule object {"port": "0xFFFD", "direction": "out", ...} into a
/// compound filter rule. Returns false with outError on invalid input.
bool ruleFromJson(const Json::Value& spec, PortTraceFilterRule& rule, std::string& outError)
{
    auto parsePortField = [&](const char* field, std::optional<uint16_t>& target) -> bool {
        if (!spec.isMember(field))
            return true;
        try
        {
            const Json::Value& v = spec[field];
            unsigned long parsed =
                v.isString() ? std::stoul(v.asString(), nullptr, 16) : static_cast<unsigned long>(v.asUInt());
            if (parsed > 0xFFFF)
                throw std::out_of_range("16-bit");
            target = static_cast<uint16_t>(parsed);
            return true;
        }
        catch (...)
        {
            outError = std::string("Invalid ") + field;
            return false;
        }
    };

    if (!parsePortField("port", rule.decodedPort))
        return false;
    if (!parsePortField("raw", rule.rawPort))
        return false;

    if (spec.isMember("device"))
    {
        std::string name = spec["device"].asString();
        bool found = false;
        for (int devId = 0; devId <= static_cast<int>(PortDeviceId::Custom); devId++)
        {
            if (name == PortDiagnosticRecorder::DeviceIdToString(static_cast<PortDeviceId>(devId)))
            {
                rule.device = static_cast<PortDeviceId>(devId);
                found = true;
                break;
            }
        }
        if (!found)
        {
            outError = "Unknown device: " + name;
            return false;
        }
    }

    if (spec.isMember("direction"))
    {
        std::string dir = spec["direction"].asString();
        if (dir == "in" || dir == "IN")
            rule.directionOut = false;
        else if (dir == "out" || dir == "OUT")
            rule.directionOut = true;
        else
        {
            outError = "Invalid direction (in/out expected)";
            return false;
        }
    }

    if (spec.isMember("pc") && spec["pc"].isArray() && spec["pc"].size() == 2)
        rule.pcRange = {static_cast<uint16_t>(std::stoul(spec["pc"][0].asString(), nullptr, 16)),
                        static_cast<uint16_t>(std::stoul(spec["pc"][1].asString(), nullptr, 16))};

    if (spec.isMember("unmapped"))
        rule.unmappedOnly = spec["unmapped"].asBool();

    return true;
}

}  // namespace

/// @brief POST /api/v1/emulator/{id}/profiler/porttrace/start
void EmulatorAPI::portTraceStart(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    (void)req;
    PortDecoder* decoder = nullptr;
    auto* recorder = getRecorder(id, &decoder, callback);
    if (!recorder)
        return;

    recorder->start();
    sendJson(callback, statusToJson(recorder, decoder));
}

/// @brief POST /api/v1/emulator/{id}/profiler/porttrace/stop
void EmulatorAPI::portTraceStop(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    (void)req;
    PortDecoder* decoder = nullptr;
    auto* recorder = getRecorder(id, &decoder, callback);
    if (!recorder)
        return;

    recorder->stop();
    sendJson(callback, statusToJson(recorder, decoder));
}

/// @brief POST /api/v1/emulator/{id}/profiler/porttrace/pause
void EmulatorAPI::portTracePause(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    (void)req;
    PortDecoder* decoder = nullptr;
    auto* recorder = getRecorder(id, &decoder, callback);
    if (!recorder)
        return;

    recorder->pause();
    sendJson(callback, statusToJson(recorder, decoder));
}

/// @brief POST /api/v1/emulator/{id}/profiler/porttrace/resume
void EmulatorAPI::portTraceResume(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    (void)req;
    PortDecoder* decoder = nullptr;
    auto* recorder = getRecorder(id, &decoder, callback);
    if (!recorder)
        return;

    recorder->resume();
    sendJson(callback, statusToJson(recorder, decoder));
}

/// @brief POST /api/v1/emulator/{id}/profiler/porttrace/clear
void EmulatorAPI::portTraceClear(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    (void)req;
    PortDecoder* decoder = nullptr;
    auto* recorder = getRecorder(id, &decoder, callback);
    if (!recorder)
        return;

    recorder->clear();
    sendJson(callback, statusToJson(recorder, decoder));
}

/// @brief GET /api/v1/emulator/{id}/profiler/porttrace/status
void EmulatorAPI::getPortTraceStatus(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    (void)req;
    PortDecoder* decoder = nullptr;
    auto* recorder = getRecorder(id, &decoder, callback);
    if (!recorder)
        return;

    Json::Value body;
    body["session"] = statusToJson(recorder, decoder);
    sendJson(callback, body);
}

/// @brief GET /api/v1/emulator/{id}/profiler/porttrace/events?limit=N&since=T
/// limit defaults to 0 = unlimited (Retrieval Depth Mandate)
void EmulatorAPI::getPortTraceEvents(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    PortDecoder* decoder = nullptr;
    auto* recorder = getRecorder(id, &decoder, callback);
    if (!recorder)
        return;

    size_t limit = 0;
    uint64_t since = 0;
    bool hasSince = false;
    try
    {
        std::string limitParam = req->getParameter("limit");
        if (!limitParam.empty())
            limit = std::stoul(limitParam);
        std::string sinceParam = req->getParameter("since");
        if (!sinceParam.empty())
        {
            since = std::stoull(sinceParam);
            hasSince = true;
        }
    }
    catch (...)
    {
        sendError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "Invalid limit/since parameter");
        return;
    }

    std::vector<PortTraceEvent> events = hasSince ? recorder->getSince(since)
                                                  : (limit > 0 ? recorder->getLast(limit) : recorder->getAll());
    if (hasSince && limit > 0 && events.size() > limit)
        events.resize(limit);

    Json::Value body;
    body["session"] = statusToJson(recorder, decoder);
    body["events"] = Json::Value(Json::arrayValue);
    for (size_t i = 0; i < events.size(); i++)
        body["events"].append(eventToJson(events[i], i));

    sendJson(callback, body);
}

/// @brief GET /api/v1/emulator/{id}/profiler/porttrace/filter
void EmulatorAPI::getPortTraceFilter(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    (void)req;
    auto* recorder = getRecorder(id, nullptr, callback);
    if (!recorder)
        return;

    Json::Value body;
    body["filter"] = recorder->describeFilter();
    sendJson(callback, body);
}

/// @brief POST /api/v1/emulator/{id}/profiler/porttrace/filter
/// Body: {"include": [{...rule...}], "exclude": [{...rule...}]}  or  {"preset": "name"}
/// Rules replace the current filter wholesale.
void EmulatorAPI::setPortTraceFilter(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    auto* recorder = getRecorder(id, nullptr, callback);
    if (!recorder)
        return;

    auto json = req->getJsonObject();
    if (!json)
    {
        sendError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "JSON body expected");
        return;
    }

    if (json->isMember("preset"))
    {
        std::string name = (*json)["preset"].asString();
        if (name == "all") recorder->presetAll();
        else if (name == "ay-only") recorder->presetAyOnly();
        else if (name == "fdc-only") recorder->presetFdcOnly();
        else if (name == "no-fdc") recorder->presetNoFdc();
        else if (name == "outs-only") recorder->presetOutsOnly();
        else if (name == "ins-only") recorder->presetInsOnly();
        else if (name == "unmapped") recorder->presetUnmapped();
        else
        {
            sendError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "Unknown preset: " + name);
            return;
        }
    }
    else
    {
        PortTraceFilterSet filter;
        std::string error;

        for (const char* section : {"include", "exclude"})
        {
            if (!json->isMember(section))
                continue;
            for (const Json::Value& spec : (*json)[section])
            {
                PortTraceFilterRule rule;
                if (!ruleFromJson(spec, rule, error))
                {
                    sendError(callback, HttpStatusCode::k400BadRequest, "Bad Request", error);
                    return;
                }
                if (std::string(section) == "include")
                    filter.include.push_back(rule);
                else
                    filter.exclude.push_back(rule);
            }
        }

        recorder->setFilter(filter);
    }

    Json::Value body;
    body["filter"] = recorder->describeFilter();
    sendJson(callback, body);
}

/// @brief POST /api/v1/emulator/{id}/profiler/porttrace/config
/// Body: {"capacity": N} and/or {"overflow": "ring"|"stop"} (only while stopped)
void EmulatorAPI::setPortTraceConfig(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    auto* recorder = getRecorder(id, nullptr, callback);
    if (!recorder)
        return;

    auto json = req->getJsonObject();
    if (!json)
    {
        sendError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "JSON body expected");
        return;
    }

    if (json->isMember("capacity"))
    {
        if (!recorder->setCapacity((*json)["capacity"].asUInt()))
        {
            sendError(callback, HttpStatusCode::k409Conflict, "Conflict",
                      "Capacity can only be changed while stopped (and must be > 0)");
            return;
        }
    }

    if (json->isMember("overflow"))
    {
        std::string mode = (*json)["overflow"].asString();
        PortTraceOverflowMode overflow;
        if (mode == "ring")
            overflow = PortTraceOverflowMode::Ring;
        else if (mode == "stop")
            overflow = PortTraceOverflowMode::StopWhenFull;
        else
        {
            sendError(callback, HttpStatusCode::k400BadRequest, "Bad Request",
                      "Invalid overflow mode (ring/stop expected)");
            return;
        }
        if (!recorder->setOverflowMode(overflow))
        {
            sendError(callback, HttpStatusCode::k409Conflict, "Conflict",
                      "Overflow mode can only be changed while stopped");
            return;
        }
    }

    Json::Value body;
    body["capacity"] = static_cast<Json::UInt64>(recorder->capacity());
    body["overflow"] = recorder->overflowMode() == PortTraceOverflowMode::Ring ? "ring" : "stop-when-full";
    sendJson(callback, body);
}

/// @brief POST /api/v1/emulator/{id}/profiler/porttrace/save
/// Body: {"path": "/tmp/trace.json", "format": "json"|"csv"|"bin"}
void EmulatorAPI::savePortTrace(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    PortDecoder* decoder = nullptr;
    auto* recorder = getRecorder(id, &decoder, callback);
    if (!recorder)
        return;

    auto json = req->getJsonObject();
    if (!json || !json->isMember("path"))
    {
        sendError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "JSON body with 'path' expected");
        return;
    }

    std::string path = (*json)["path"].asString();
    std::string format = json->isMember("format") ? (*json)["format"].asString() : "json";

    PortTraceExportFormat fmt = PortTraceExportFormat::JSON;
    if (format == "csv")
        fmt = PortTraceExportFormat::CSV;
    else if (format == "bin" || format == "binary")
        fmt = PortTraceExportFormat::Binary;
    else if (format != "json")
    {
        sendError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "Unknown format: " + format);
        return;
    }

    size_t count = recorder->eventCount();
    if (!recorder->saveToFile(path, fmt, decoder->getPortTraceSessionInfo()))
    {
        sendError(callback, HttpStatusCode::k500InternalServerError, "Internal Error",
                  "Failed to save trace to " + path);
        return;
    }

    Json::Value body;
    body["saved"] = static_cast<Json::UInt64>(count);
    body["path"] = path;
    body["format"] = format;
    sendJson(callback, body);
}

}  // namespace v1
}  // namespace api
