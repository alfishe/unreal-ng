// WebAPI Static Port-Map Introspection Implementation
// Gap C-3 / recommendation P1-5: "which devices respond to which ports on this
// machine right now, under which gating conditions" - the entry point for
// ATM-port and Kempston-mouse-routing triage, derived from the port decoder
// (PortDecoder::getPortMapEntries / GetMouseRoutingState, single source).

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/config.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/ports/portdecoder.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);

/// @brief GET /api/v1/emulator/{id}/ports
void EmulatorAPI::getPortsMap(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    (void)req;

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
    if (!context || !context->pPortDecoder)
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

    PortDecoder* decoder = context->pPortDecoder;
    const CONFIG& config = context->config;
    EmulatorState& state = context->emulatorState;

    Json::Value ret;
    ret["emulator_id"] = id;
    ret["model"] = Config::GetModelFullName(config.mem_model);

    // Static map: one row per decoded device/port family
    Json::Value entries(Json::arrayValue);
    for (const PortMapEntry& entry : decoder->getPortMapEntries())
    {
        Json::Value item;
        item["port"] = StringHelper::Format("0x%04X", entry.port);
        item["mask"] = StringHelper::Format("0x%04X", entry.mask);
        item["match"] = StringHelper::Format("0x%04X", entry.match);
        item["device"] = entry.device;
        item["gate"] = entry.gate ? Json::Value(entry.gate) : Json::Value(Json::nullValue);
        entries.append(item);
    }
    ret["entries"] = entries;

    // Live routing state: the flags that flip rows on/off right now
    bool mouseDecoded = false;
    std::string mouseNote;
    decoder->GetMouseRoutingState(mouseDecoded, mouseNote);

    Json::Value live;
    live["trdos_active"] = (state.flags & (CF_TRDOS | CF_DOSPORTS)) != 0;
    live["mouse_ports_decoded"] = mouseDecoded;
    live["mouse_routing_note"] = mouseNote;

    const bool scorpion = (config.mem_model == MM_SCORP || config.mem_model == MM_PROFSCORP);
    if (scorpion)
        live["shadow_monitor_paged"] = (state.p1FFD & 0x02) != 0;
    else
        live["shadow_monitor_paged"] = Json::Value(Json::nullValue);  // latch does not exist on this model

    ret["live"] = live;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
