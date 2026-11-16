// WebAPI: TS-Conf VDAC4 board (Tang Nano 20K FT812 core + ESP32-S3).
// GET /vdac4/state: the Tang's personality, the ESP's phase, the running app and the ESP log
// (docs/inprogress/2026-11-15-tsconf-vdac4/design.md §9). Every surface goes through Vdac4Control.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/platforms/tsconf/vdac4/vdac4control.h>
#include <json/json.h>

#include <algorithm>
#include <cstdlib>

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

void sendVdac4Error(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode code,
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

} // namespace

/// @brief GET /api/v1/emulator/{id}/vdac4/state[?log=N]  (N recent ESP log lines, default 50, at most 256)
void EmulatorAPI::vdac4State(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        sendVdac4Error(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator with specified ID not found");
        return;
    }
    size_t logLines = 50;
    const std::string logParam = req->getParameter("log");
    if (!logParam.empty())
        logLines = std::min<size_t>(std::strtoul(logParam.c_str(), nullptr, 10), 256);

    Vdac4Control::BoardState state;
    std::string error;
    if (!Vdac4Control::GetState(emulator->GetContext(), state, logLines, &error))
    {
        sendVdac4Error(callback, HttpStatusCode::k409Conflict, "No VDAC4 Board", error);
        return;
    }

    Json::Value body;
    Json::Value& tang = body["tang"];
    tang["personality"] = state.personality;
    tang["armed"] = state.armed;
    tang["esp_host"] = state.espHost;
    tang["ram_g_3mb"] = state.ramG3M;
    tang["ft_cfg"] = state.ftCfg;
    Json::Value& esp = body["esp"];
    esp["phase"] = state.espPhase;
    esp["ms"] = state.espMs;
    esp["boots"] = state.boots;
    esp["boot_nonce"] = state.bootNonce;
    esp["last_step"] = state.lastStep;
    esp["last_error"] = state.lastError;
    esp["resources"] = static_cast<Json::UInt64>(state.resources);
    esp["clock_restarts"] = static_cast<Json::UInt64>(state.clockRestarts);
    esp["clock_offset_us"] = static_cast<Json::UInt64>(state.clockOffsetUs);
    Json::Value& app = body["app"];
    app["running"] = state.appRunning;
    app["module"] = state.appModule;
    app["name"] = state.appName;
    app["version"] = state.appVersion;
    app["command_first"] = state.commandFirst;
    app["command_last"] = state.commandLast;
    app["error"] = state.appError;
    Json::Value& packets = body["packets"];
    packets["count"] = static_cast<Json::UInt64>(state.packets);
    Json::Value& link = body["link"];
    link["pending_from_z80"] = static_cast<Json::UInt64>(state.pendingFromZ80);
    link["pending_to_z80"] = static_cast<Json::UInt64>(state.pendingToZ80);
    link["send_blocked"] = state.sendBlocked;
    link["bad_checksums"] = state.badChecksums;
    link["resyncs"] = state.resyncs;
    link["dropped_frames"] = state.droppedFrames;
    Json::Value& recent = packets["recent"];
    recent = Json::arrayValue;
    for (const auto& packet : state.recentPackets)
    {
        Json::Value item;
        item["command"] = packet.command;
        item["length"] = packet.length;
        item["ms"] = packet.ms;
        recent.append(item);
    }
    Json::Value& audio = body["audio"];
    audio["input_rate"] = state.audioInputRate;
    audio["output_rate"] = state.audioOutputRate;
    audio["queued"] = state.audioQueued;
    audio["underruns"] = static_cast<Json::UInt64>(state.audioUnderruns);
    audio["dropped"] = static_cast<Json::UInt64>(state.audioDropped);
    audio["sound"] = state.audioSound;
    Json::Value& log = body["log"];
    log = Json::arrayValue;
    for (const std::string& line : state.log)
        log.append(line);

    auto resp = HttpResponse::newHttpJsonResponse(body);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
