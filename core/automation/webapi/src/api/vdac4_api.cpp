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
    Json::Value& sections = app["sections"];
    sections = Json::arrayValue;
    for (const auto& section : state.appSections)
    {
        Json::Value item;
        item["elf_address"] = section.elfAddress;
        item["size"] = section.size;
        item["loaded_at"] = section.loadedAt;
        sections.append(item);
    }
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
    Json::Value& threads = body["threads"];
    threads["esp_thread"] = state.espThread;
    threads["ft_thread"] = state.ftThread;
    threads["ft_draw_thread"] = state.ftDrawThread;
    threads["ft_draw_threads"] = state.ftDrawThreads;
    threads["esp_latency_ms"] = state.espLatencyMs;
    threads["esp_jobs"] = static_cast<Json::UInt64>(state.espJobs);
    threads["esp_threaded_jobs"] = static_cast<Json::UInt64>(state.espThreadedJobs);
    threads["esp_waits"] = static_cast<Json::UInt64>(state.espWaits);
    threads["esp_wait_ms"] = state.espWaitMs;
    threads["esp_job_ms"] = state.espJobMs;
    threads["ft_ops"] = static_cast<Json::UInt64>(state.ftOps);
    threads["ft_reads"] = static_cast<Json::UInt64>(state.ftReads);
    threads["ft_apply_ms"] = state.ftApplyMs;
    threads["ft_producer_wait_ms"] = state.ftProducerWaitMs;
    threads["ft_owner_waits"] = static_cast<Json::UInt64>(state.ftOwnerWaits);
    threads["ft_owner_wait_ms"] = state.ftOwnerWaitMs;
    Json::Value& log = body["log"];
    log = Json::arrayValue;
    for (const std::string& line : state.log)
        log.append(line);

    auto resp = HttpResponse::newHttpJsonResponse(body);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/vdac4/esp/memory?address=A&size=N[&app=1]  (N at most 4096; app=1: A is an
/// address of the running app's ELF image)
void EmulatorAPI::vdac4EspMemory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        sendVdac4Error(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator with specified ID not found");
        return;
    }
    const std::string addressParam = req->getParameter("address");
    const std::string sizeParam = req->getParameter("size");
    char* end = nullptr;
    const unsigned long long address = std::strtoull(addressParam.c_str(), &end, 0);
    if (addressParam.empty() || end == nullptr || *end != '\0' || address > 0xFFFFFFFFull)
    {
        sendVdac4Error(callback, HttpStatusCode::k400BadRequest, "bad_request",
                       "address is a 32-bit number (decimal or 0x hex)");
        return;
    }
    const unsigned long size = sizeParam.empty() ? 16 : std::strtoul(sizeParam.c_str(), nullptr, 0);
    const std::string appParam = req->getParameter("app");
    const bool app = appParam == "1" || appParam == "true";

    std::vector<uint8_t> bytes;
    std::string error;
    if (!Vdac4Control::ReadEspMemory(emulator->GetContext(), static_cast<uint32_t>(address),
                                     static_cast<uint32_t>(std::min<unsigned long>(size, 0xFFFFFFFFul)), app, bytes,
                                     &error))
    {
        const bool noBoard = !Vdac4Control::HasBoard(emulator->GetContext());
        sendVdac4Error(callback, noBoard ? HttpStatusCode::k409Conflict : HttpStatusCode::k400BadRequest,
                       noBoard ? "No VDAC4 Board" : "bad_request", error);
        return;
    }
    static const char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (uint8_t byte : bytes)
    {
        hex.push_back(kHex[byte >> 4]);
        hex.push_back(kHex[byte & 0x0F]);
    }
    Json::Value body;
    body["address"] = static_cast<Json::UInt>(address);
    body["size"] = static_cast<Json::UInt>(bytes.size());
    body["app"] = app;
    body["hex"] = hex;
    auto resp = HttpResponse::newHttpJsonResponse(body);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
