// WebAPI State Audio Inspection Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

#include <bitset>

#include "../emulator_api.h"
#include "statenode_json.h"
#include <emulator/state/devicestate.h>
#include <emulator/sound/chips/soundchip_gs.h>


using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper functions declared in emulator_api.h / emulator_api.cpp.
extern void addCorsHeaders(HttpResponsePtr& resp);
// getEmulatorByIdOrIndex is a free function in api::v1 (emulator_api.h) —
// visible here without an EmulatorAPI instance.

/// @brief GET /api/v1/emulator/{id}/state/audio/ay
void EmulatorAPI::getStateAudioAY(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

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

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Sound manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Core report (DeviceState::Ay): the same tree every interface renders
    Json::Value ret = StateNodeToJson(DeviceState::Ay(context));

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/ay/{chip}
void EmulatorAPI::getStateAudioAYIndex(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id,
                                       const std::string& chipStr) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

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

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager || !soundManager->hasTurboSound())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "AY chips not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse chip index
    int chipIndex = -1;
    try
    {
        chipIndex = std::stoi(chipStr);
    }
    catch (const std::exception&)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid chip index (must be integer)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Get the requested chip
    SoundChip_AY8910* chip = soundManager->getAYChip(chipIndex);

    if (!chip)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "AY chip " + chipStr + " not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Core report (DeviceState::AyChip): the same tree every interface renders
    Json::Value ret = StateNodeToJson(DeviceState::AyChip(context, chipIndex));
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/ay/register/{reg}
void EmulatorAPI::getStateAudioAYRegister(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id,
                                          const std::string& chipStr, const std::string& regStr) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

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

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager || !soundManager->hasTurboSound())
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "AY chips not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse chip index
    int chipIndex = -1;
    try
    {
        chipIndex = std::stoi(chipStr);
    }
    catch (const std::exception&)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid chip index: " + chipStr;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    SoundChip_AY8910* chip = soundManager->getAYChip(chipIndex);
    if (!chip)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "AY chip " + chipStr + " not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Parse register number
    int regNum = -1;
    try
    {
        regNum = std::stoi(regStr);
    }
    catch (const std::exception&)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid register number (must be 0-15)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (regNum < 0 || regNum > 15)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Register number must be between 0 and 15";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const uint8_t* registers = chip->getRegisters();
    uint8_t regValue = registers[regNum];

    Json::Value ret;
    ret["register_number"] = regNum;
    ret["register_name"] = SoundChip_AY8910::AYRegisterNames[regNum];
    ret["value_hex"] = "0x" + std::string((regValue < 16 ? "0" : "") + std::to_string(regValue));
    ret["value_dec"] = (int)regValue;
    ret["value_bin"] = std::bitset<8>(regValue).to_string();

    // Add specific decoding based on register
    Json::Value decoding;

    switch (regNum)
    {
        case 0:
        case 2:
        case 4:  // Fine period registers
        {
            int channel = regNum / 2;
            const char* channelNames[] = {"A", "B", "C"};
            decoding["description"] = std::string("Channel ") + channelNames[channel] + " tone period (fine)";
            decoding["note"] = "Lower 8 bits of 12-bit period value";
            uint8_t coarse = registers[regNum + 1];
            uint16_t period = (coarse << 8) | regValue;
            decoding["full_period"] = period;
            decoding["frequency_hz"] = 1750000.0 / (16.0 * (period + 1));
            break;
        }
        case 1:
        case 3:
        case 5:  // Coarse period registers
        {
            int channel = (regNum - 1) / 2;
            const char* channelNames[] = {"A", "B", "C"};
            decoding["description"] = std::string("Channel ") + channelNames[channel] + " tone period (coarse)";
            decoding["note"] = "Upper 4 bits of 12-bit period value";
            uint8_t fine = registers[regNum - 1];
            uint16_t period = (regValue << 8) | fine;
            decoding["full_period"] = period;
            decoding["frequency_hz"] = 1750000.0 / (16.0 * (period + 1));
            break;
        }
        case 6:  // Noise period
            decoding["description"] = "Noise generator period";
            decoding["period_value"] = (int)(regValue & 0x1F);
            decoding["frequency_hz"] = 1750000.0 / (16.0 * ((regValue & 0x1F) + 1));
            break;
        case 7:  // Mixer control
            decoding["description"] = "Mixer control and I/O port direction";
            decoding["channel_a_tone_enabled"] = ((regValue & 0x01) == 0);
            decoding["channel_b_tone_enabled"] = ((regValue & 0x02) == 0);
            decoding["channel_c_tone_enabled"] = ((regValue & 0x04) == 0);
            decoding["channel_a_noise_enabled"] = ((regValue & 0x08) == 0);
            decoding["channel_b_noise_enabled"] = ((regValue & 0x10) == 0);
            decoding["channel_c_noise_enabled"] = ((regValue & 0x20) == 0);
            decoding["porta_direction"] = ((regValue & 0x40) ? "input" : "output");
            decoding["portb_direction"] = ((regValue & 0x80) ? "input" : "output");
            break;
        case 8:
        case 9:
        case 10:  // Volume registers
        {
            int channel = regNum - 8;
            const char* channelNames[] = {"A", "B", "C"};
            decoding["description"] = std::string("Channel ") + channelNames[channel] + " volume";
            decoding["volume_level"] = (int)(regValue & 0x0F);
            decoding["envelope_mode"] = ((regValue & 0x10) != 0);
            if (regValue & 0x10)
            {
                decoding["note"] = "Volume controlled by envelope generator";
            }
            else
            {
                decoding["note"] = "Fixed volume level";
            }
            break;
        }
        case 11:  // Envelope period fine
            decoding["description"] = "Envelope period (fine)";
            decoding["note"] = "Lower 8 bits of 16-bit envelope period";
            {
                uint8_t coarse = registers[12];
                uint16_t period = (coarse << 8) | regValue;
                decoding["full_period"] = period;
                decoding["frequency_hz"] = 1750000.0 / (256.0 * (period + 1));
            }
            break;
        case 12:  // Envelope period coarse
            decoding["description"] = "Envelope period (coarse)";
            decoding["note"] = "Upper 8 bits of 16-bit envelope period";
            {
                uint8_t fine = registers[11];
                uint16_t period = (regValue << 8) | fine;
                decoding["full_period"] = period;
                decoding["frequency_hz"] = 1750000.0 / (256.0 * (period + 1));
            }
            break;
        case 13:  // Envelope shape
            decoding["description"] = "Envelope shape control";
            decoding["shape_value"] = (int)(regValue & 0x0F);
            decoding["continue"] = ((regValue & 0x01) != 0);
            decoding["attack"] = ((regValue & 0x02) != 0);
            decoding["alternate"] = ((regValue & 0x04) != 0);
            decoding["hold"] = ((regValue & 0x08) != 0);
            break;
        case 14:  // I/O Port A
            decoding["description"] = "I/O Port A";
            decoding["direction"] = ((registers[7] & 0x40) ? "input" : "output");
            decoding["value"] = (int)regValue;
            break;
        case 15:  // I/O Port B
            decoding["description"] = "I/O Port B";
            decoding["direction"] = ((registers[7] & 0x80) ? "input" : "output");
            decoding["value"] = (int)regValue;
            break;
    }

    ret["decoding"] = decoding;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/beeper
void EmulatorAPI::getStateAudioBeeper(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                      const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

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

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager)
    {
        Json::Value error;
        error["error"] = "Internal Error";
        error["message"] = "Sound manager not available";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["device"] = "Beeper (ULA integrated)";
    ret["output_port"] = "0xFE";
    ret["current_level"] = "unknown";  // Internal state not accessible
    ret["last_output"] = "unknown";    // Internal state not accessible
    ret["frequency_range_hz"] = "20 - 10000";
    ret["bit_resolution"] = 1;
    ret["sound_played_since_reset"] = false;  // TODO: Implement sound played tracking

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/gs
/// General Sound card state (GS design §10.1): mailbox flags, MPAG page,
/// per-channel DAC sample/volume and the coprocessor core. 404 when the
/// machine has no GS fitted ([SOUND] GSType=Z80 selects the card).
void EmulatorAPI::getStateAudioGS(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    SoundManager* soundManager = context ? context->pSoundManager : nullptr;
    SoundChip_GeneralSound* gs = soundManager ? soundManager->getGeneralSound() : nullptr;

    if (!gs)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "General Sound card not fitted (configure [SOUND] GSType=Z80)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const uint8_t status = gs->getStatusRaw();

    Json::Value ret;
    ret["device"] = "General Sound (Z80 coprocessor @ 12 MHz, 4 x 8-bit DAC)";
    ret["enabled"] = true;
    ret["rom_loaded"] = gs->isROMLoaded();
    ret["ram_kb"] = static_cast<Json::UInt64>(gs->getRamSizeKB());
    ret["status"] = status;
    ret["command_pending"] = (status & 0x01) != 0;  // bit0: ZX command waiting
    ret["data_pending"] = (status & 0x80) != 0;     // bit7: GS data waiting
    ret["command_from_host"] = gs->getCommandFromHost();
    ret["data_from_host"] = gs->getDataFromHost();
    ret["data_to_host"] = gs->getDataToHost();
    ret["page"] = gs->getMPAG();  // MPAG banking latch (GS design §2.3)

    Json::Value channels(Json::arrayValue);
    for (int i = 0; i < 4; i++)
    {
        Json::Value channel;
        channel["sample"] = gs->getChannelSample(i);
        channel["volume"] = gs->getChannelVolume(i);
        channels.append(channel);
    }
    ret["channels"] = channels;

    Json::Value cpu;
    cpu["pc"] = gs->getCPUReg(regPC);
    cpu["sp"] = gs->getCPUReg(regSP);
    cpu["af"] = gs->getCPUReg(regAF);
    cpu["halted"] = gs->isCPUHalted();
    ret["cpu"] = cpu;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/control/audio/gs
/// @param body {"action": "reset|reset_card|nmi|send_command|send_data|read_status|read_data",
///              "value": 0..255 (byte actions)}
/// Actions mirror the host-port semantics - each flushes the GS coprocessor
/// to the current ZX tact first (GS design §10.1):
///   reset        - full power-on reset (mailbox, volumes and timing too)
///   reset_card   - #33 bit7 pulse (CPU/banking/timing only, mailbox survives)
///   nmi          - #33 bit6 pulse
///   send_command - OUT #BB semantics (sets the command-pending flag)
///   send_data    - OUT #B3 semantics (sets the data-pending flag)
///   read_status  - IN #BB semantics (returns status | 0x7E)
///   read_data    - IN #B3 semantics (clears bit7, returns the GS->ZX byte)
void EmulatorAPI::postControlAudioGS(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    SoundManager* soundManager = context ? context->pSoundManager : nullptr;
    SoundChip_GeneralSound* gs = soundManager ? soundManager->getGeneralSound() : nullptr;

    if (!gs)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "General Sound card not fitted (configure [SOUND] GSType=Z80)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("action") || !json->get("action", "").isString())
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing or invalid 'action' field";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const std::string action = json->get("action", "").asString();
    const bool needsValue = (action == "send_command" || action == "send_data");

    int value = 0;
    if (json->isMember("value"))
    {
        if (!json->get("value", 0).isNumeric() || json->get("value", 0).asInt() < 0
            || json->get("value", 0).asInt() > 255)
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "'value' must be an integer in 0..255";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        value = json->get("value", 0).asInt();
    }
    else if (needsValue)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Action '" + action + "' requires a 'value' field (0..255)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret;
    ret["status"] = "success";
    ret["action"] = action;

    if (action == "reset")
    {
        gs->reset();
    }
    else if (action == "reset_card")
    {
        gs->resetCard();
    }
    else if (action == "nmi")
    {
        gs->triggerNMI();
    }
    else if (action == "send_command")
    {
        gs->sendCommand(static_cast<uint8_t>(value));
    }
    else if (action == "send_data")
    {
        gs->sendData(static_cast<uint8_t>(value));
    }
    else if (action == "read_status")
    {
        ret["value"] = gs->readStatus();
    }
    else if (action == "read_data")
    {
        ret["value"] = gs->readData();
    }
    else
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] =
            "Unknown action '" + action +
            "' (expected reset, reset_card, nmi, send_command, send_data, read_status or read_data)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

namespace
{
const char* gsTraceSideToString(GSTraceSide side)
{
    switch (side)
    {
        case GSTraceSide::Host: return "host";
        case GSTraceSide::GsInternal: return "gs";
        case GSTraceSide::DacFetch: return "dac";
        case GSTraceSide::Interrupt: return "interrupt";
    }
    return "unknown";
}
}  // namespace

/// @brief GET /api/v1/emulator/{id}/state/audio/gs/porttrace?events=N
/// Always-on activity counters (proves whether the GS coprocessor is
/// executing and pushing DAC samples) + trace session status, optionally the
/// last N buffered events. Same data model as CLI 'gsporttrace' / MCP / Lua /
/// Python - the GS-coprocessor triage tool (see gsporttrace.h).
void EmulatorAPI::getStateAudioGSPortTrace(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                           const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    SoundManager* soundManager = context ? context->pSoundManager : nullptr;
    SoundChip_GeneralSound* gs = soundManager ? soundManager->getGeneralSound() : nullptr;

    if (!gs)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "General Sound card not fitted (configure [SOUND] GSType=Z80)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const GSActivityCounters& c = gs->getActivityCounters();
    Json::Value ret;
    Json::Value counters;
    counters["cpu_steps"] = static_cast<Json::UInt64>(c.cpuSteps);
    counters["interrupts_accepted"] = static_cast<Json::UInt64>(c.interruptsAccepted);
    counters["nmis_accepted"] = static_cast<Json::UInt64>(c.nmisAccepted);
    counters["dac_fetches"] = static_cast<Json::UInt64>(c.dacFetches);
    counters["volume_latch_writes"] = static_cast<Json::UInt64>(c.volumeLatchWrites);
    counters["host_commands_received"] = static_cast<Json::UInt64>(c.hostCommandsReceived);
    counters["host_data_written"] = static_cast<Json::UInt64>(c.hostDataWritten);
    counters["host_data_read"] = static_cast<Json::UInt64>(c.hostDataRead);
    counters["last_dac_fetch_gs_cycle"] = static_cast<Json::Int64>(c.lastDacFetchGsCycle);
    counters["last_dac_fetch_frame"] = static_cast<Json::UInt64>(c.lastDacFetchFrame);
    ret["counters"] = counters;

    Json::Value trace;
    trace["capturing"] = gs->isPortTraceCapturing();
    trace["armed"] = gs->isPortTraceArmed();
    trace["event_count"] = static_cast<Json::UInt64>(gs->getPortTraceEventCount());
    trace["total_produced"] = static_cast<Json::UInt64>(gs->getPortTraceTotalProduced());
    trace["total_evicted"] = static_cast<Json::UInt64>(gs->getPortTraceTotalEvicted());
    ret["trace"] = trace;

    Json::Value cpu;
    cpu["pc"] = gs->getCPUReg(regPC);
    cpu["halted"] = gs->isCPUHalted();
    ret["cpu"] = cpu;

    std::string eventsParam = req->getParameter("events");
    if (!eventsParam.empty())
    {
        size_t count = 50;
        try { count = static_cast<size_t>(std::stoul(eventsParam)); } catch (...) {}
        auto events = gs->getPortTraceLast(count);

        Json::Value eventsJson(Json::arrayValue);
        for (const auto& e : events)
        {
            Json::Value ev;
            ev["timestamp"] = static_cast<Json::Int64>(e.timestamp);
            ev["frame"] = e.frameNumber;
            ev["side"] = gsTraceSideToString(e.side);
            ev["direction"] = e.isOut() ? "out" : "in";
            ev["port"] = e.port;
            ev["value"] = e.value;
            ev["pc"] = e.pc;
            if (e.side == GSTraceSide::DacFetch)
                ev["channel"] = e.channel;
            if (e.side == GSTraceSide::Interrupt)
                ev["nmi"] = e.isNmi();
            eventsJson.append(ev);
        }
        ret["events"] = eventsJson;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/control/audio/gs/porttrace — body: {"action": "start|stop|pause|resume|clear"}
void EmulatorAPI::postControlAudioGSPortTrace(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                              const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    SoundManager* soundManager = context ? context->pSoundManager : nullptr;
    SoundChip_GeneralSound* gs = soundManager ? soundManager->getGeneralSound() : nullptr;

    if (!gs)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "General Sound card not fitted (configure [SOUND] GSType=Z80)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("action") || !json->get("action", "").isString())
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing or invalid 'action' field";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    const std::string action = json->get("action", "").asString();
    Json::Value ret;
    ret["status"] = "success";
    ret["action"] = action;

    if (action == "start")
        gs->startPortTrace();
    else if (action == "stop")
        gs->stopPortTrace();
    else if (action == "pause")
        gs->pausePortTrace();
    else if (action == "resume")
        gs->resumePortTrace();
    else if (action == "clear")
        gs->clearPortTrace();
    else
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Unknown action '" + action + "' (expected start, stop, pause, resume or clear)";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/covox
void EmulatorAPI::getStateAudioCovox(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    Json::Value ret;
    ret["status"] = "not_implemented";
    ret["description"] =
        "Covox is an 8-bit DAC (Digital-to-Analog Converter) that connects to various ports on the ZX Spectrum for "
        "sample playback.";
    ret["note"] = "This endpoint is reserved for future implementation.";

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/channels
void EmulatorAPI::getStateAudioChannels(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found with ID: " + id;

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

    SoundManager* soundManager = context->pSoundManager;
    Json::Value ret;

    // Beeper channel
    Json::Value beeper;
    beeper["available"] = true;
    beeper["current_level"] = "unknown";
    beeper["active"] = "unknown";
    ret["beeper"] = beeper;

    // AY channels
    Json::Value ayChannels;
    bool hasAY = (soundManager && soundManager->hasTurboSound());
    ayChannels["available"] = hasAY;

    if (hasAY)
    {
        Json::Value chips(Json::arrayValue);
        int ayCount = soundManager->getAYChipCount();

        for (int chipIdx = 0; chipIdx < ayCount; chipIdx++)
        {
            SoundChip_AY8910* chip = soundManager->getAYChip(chipIdx);
            if (!chip)
                continue;

            Json::Value chipChannels(Json::arrayValue);
            const char* channelNames[] = {"A", "B", "C"};
            const auto* toneGens = chip->getToneGenerators();

            for (int ch = 0; ch < 3; ch++)
            {
                Json::Value channel;
                const auto& toneGen = toneGens[ch];
                channel["name"] = std::string("AY") + std::to_string(chipIdx) + channelNames[ch];
                channel["active"] = (toneGen.toneEnabled() || toneGen.noiseEnabled());
                channel["volume"] = (int)toneGen.volume();
                channel["envelope_enabled"] = toneGen.envelopeEnabled();
                chipChannels.append(channel);
            }

            Json::Value chipInfo;
            chipInfo["chip_index"] = chipIdx;
            chipInfo["channels"] = chipChannels;
            chips.append(chipInfo);
        }
        ayChannels["chips"] = chips;
    }
    ret["ay_channels"] = ayChannels;

    // General Sound (dedicated detail endpoint: /state/audio/gs)
    Json::Value gs;
    bool hasGS = (soundManager && soundManager->hasGeneralSound());
    gs["available"] = hasGS;
    if (hasGS)
    {
        SoundChip_GeneralSound* gsChip = soundManager->getGeneralSound();
        const uint8_t gsStatus = gsChip->getStatusRaw();
        gs["rom_loaded"] = gsChip->isROMLoaded();
        gs["ram_kb"] = (int)gsChip->getRamSizeKB();
        gs["cpu_halted"] = gsChip->isCPUHalted();
        gs["command_pending"] = (gsStatus & 0x01) != 0;
        gs["data_pending"] = (gsStatus & 0x80) != 0;
        Json::Value gsChannels(Json::arrayValue);
        for (int i = 0; i < 4; i++)
        {
            Json::Value channel;
            channel["name"] = std::string("GS") + std::to_string(i + 1);
            channel["sample"] = (int)gsChip->getChannelSample(i);
            channel["volume"] = (int)gsChip->getChannelVolume(i);
            gsChannels.append(channel);
        }
        gs["channels"] = gsChannels;
    }
    ret["general_sound"] = gs;

    // Covox (detail endpoint reserved; presence follows the config)
    Json::Value covox;
    covox["available"] = (soundManager && soundManager->hasCovox());
    ret["covox"] = covox;

    // Master audio state
    Json::Value master;
    master["muted"] = (soundManager ? soundManager->isMuted() : false);
    master["sample_rate_hz"] = 44100;
    master["channels"] = "stereo";
    master["bit_depth"] = 16;
    ret["master"] = master;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief Audio state inspection (active emulator - no ID required)
/// Uses global selection priority, then stateless fallback
void EmulatorAPI::getStateAudioAYActive(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/ay";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioAY(req, std::move(callback), emulator->GetId());
}

/// @brief Get specific AY chip details (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioAYIndexActive(const HttpRequestPtr& req,
                                             std::function<void(const HttpResponsePtr&)>&& callback,
                                             const std::string& chip) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/ay/" +
                                            chip;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioAYIndex(req, std::move(callback), emulator->GetId(), chip);
}

/// @brief Get AY chip register details (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioAYRegisterActive(const HttpRequestPtr& req,
                                                std::function<void(const HttpResponsePtr&)>&& callback,
                                                const std::string& chip, const std::string& reg) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/ay/" +
                                            chip + "/register/" + reg;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioAYRegister(req, std::move(callback), emulator->GetId(), chip, reg);
}

/// @brief Get beeper state (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioBeeperActive(const HttpRequestPtr& req,
                                            std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/beeper";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioBeeper(req, std::move(callback), emulator->GetId());
}

/// @brief Get GS state (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioGSActive(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/gs";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioGS(req, std::move(callback), emulator->GetId());
}

/// @brief Get Covox state (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioCovoxActive(const HttpRequestPtr& req,
                                           std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/covox";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioCovox(req, std::move(callback), emulator->GetId());
}

/// @brief Get audio channels state (active emulator)
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioChannelsActive(const HttpRequestPtr& req,
                                              std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();

    if (!emulator)
    {
        auto manager = EmulatorManager::GetInstance();
        auto count = manager->GetEmulatorIds().size();

        Json::Value error;
        error["error"] = count == 0 ? "Not Found" : "Bad Request";
        error["message"] = count == 0 ? "No emulator available (none running)"
                                      : "Multiple emulators running. Please specify emulator ID in path: "
                                        "/api/v1/emulator/{id}/state/audio/channels";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    getStateAudioChannels(req, std::move(callback), emulator->GetId());
}

}  // namespace v1
}  // namespace api
