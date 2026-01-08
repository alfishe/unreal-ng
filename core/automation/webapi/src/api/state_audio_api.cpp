// WebAPI State Audio Inspection Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper functions declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);
extern std::shared_ptr<Emulator> getEmulatorByIdOrIndex(const std::string& idOrIndex);

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

    Json::Value ret;

    // Count available AY chips
    int ayCount = soundManager->getAYChipCount();
    bool hasTurboSound = soundManager->hasTurboSound();

    ret["available_chips"] = ayCount;
    ret["turbo_sound"] = hasTurboSound;

    if (ayCount == 0)
    {
        ret["description"] = "No AY chips available";
    }
    else if (ayCount == 1)
    {
        ret["description"] = "Standard AY-3-8912";
    }
    else if (ayCount == 2)
    {
        ret["description"] = "TurboSound (dual AY-3-8912)";
    }
    else if (ayCount == 3)
    {
        ret["description"] = "ZX Next (triple AY-3-8912)";
    }

    // Brief info for each chip
    Json::Value chips(Json::arrayValue);
    for (int i = 0; i < ayCount; i++)
    {
        Json::Value chipInfo;
        chipInfo["index"] = i;
        chipInfo["type"] = "AY-3-8912";

        SoundChip_AY8910* chip = soundManager->getAYChip(i);
        if (chip)
        {
            // Check if any channels are active
            bool hasActiveChannels = false;
            const auto* toneGens = chip->getToneGenerators();
            for (int ch = 0; ch < 3; ch++)
            {
                if (toneGens[ch].toneEnabled() || toneGens[ch].noiseEnabled())
                {
                    hasActiveChannels = true;
                    break;
                }
            }
            chipInfo["active_channels"] = hasActiveChannels;
            chipInfo["envelope_active"] = (chip->getEnvelopeGenerator().out() > 0);
        }

        chipInfo["sound_played_since_reset"] = false;  // TODO: Implement sound played tracking
        chips.append(chipInfo);
    }

    ret["chips"] = chips;
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

    Json::Value ret;
    ret["chip_index"] = chipIndex;
    ret["chip_type"] = "AY-3-8912";

    // Get registers using public method
    const uint8_t* chipRegisters = chip->getRegisters();

    // Register values
    Json::Value registers(Json::objectValue);
    for (int reg = 0; reg < 16; reg++)
    {
        registers[SoundChip_AY8910::AYRegisterNames[reg]] = (int)chipRegisters[reg];
    }
    ret["registers"] = registers;

    // Channel information
    Json::Value channels(Json::arrayValue);
    const char* channelNames[] = {"A", "B", "C"};
    const auto* toneGens = chip->getToneGenerators();
    for (int ch = 0; ch < 3; ch++)
    {
        Json::Value channel;
        const auto& toneGen = toneGens[ch];

        uint8_t fine = chipRegisters[ch * 2];
        uint8_t coarse = chipRegisters[ch * 2 + 1];
        uint16_t period = (coarse << 8) | fine;

        channel["name"] = channelNames[ch];
        channel["period"] = period;
        channel["fine"] = (int)fine;
        channel["coarse"] = (int)coarse;
        channel["frequency_hz"] = 1750000.0 / (16.0 * (period + 1));
        channel["volume"] = (int)toneGen.volume();
        channel["tone_enabled"] = toneGen.toneEnabled();
        channel["noise_enabled"] = toneGen.noiseEnabled();
        channel["envelope_enabled"] = toneGen.envelopeEnabled();

        channels.append(channel);
    }
    ret["channels"] = channels;

    // Envelope generator
    Json::Value envelope;
    uint8_t envShape = chipRegisters[13];
    uint16_t envPeriod = (chipRegisters[12] << 8) | chipRegisters[11];
    envelope["shape"] = (int)envShape;
    envelope["period"] = envPeriod;
    envelope["current_output"] = (int)chip->getEnvelopeGenerator().out();
    envelope["frequency_hz"] = 1750000.0 / (256.0 * (envPeriod + 1));
    ret["envelope"] = envelope;

    // Noise generator
    Json::Value noise;
    uint8_t noisePeriod = chipRegisters[6] & 0x1F;
    noise["period"] = (int)noisePeriod;
    noise["frequency_hz"] = 1750000.0 / (16.0 * (noisePeriod + 1));
    ret["noise"] = noise;

    // Mixer state
    Json::Value mixer;
    uint8_t mixerValue = chipRegisters[7];
    mixer["register_value"] = (int)mixerValue;
    mixer["channel_a_tone"] = ((mixerValue & 0x01) == 0);
    mixer["channel_b_tone"] = ((mixerValue & 0x02) == 0);
    mixer["channel_c_tone"] = ((mixerValue & 0x04) == 0);
    mixer["channel_a_noise"] = ((mixerValue & 0x08) == 0);
    mixer["channel_b_noise"] = ((mixerValue & 0x10) == 0);
    mixer["channel_c_noise"] = ((mixerValue & 0x20) == 0);
    mixer["porta_input"] = ((mixerValue & 0x40) != 0);
    mixer["portb_input"] = ((mixerValue & 0x80) != 0);
    ret["mixer"] = mixer;

    // I/O ports
    Json::Value ports;
    ports["porta_value"] = (int)chipRegisters[14];
    ports["porta_direction"] = ((mixerValue & 0x40) ? "input" : "output");
    ports["portb_value"] = (int)chipRegisters[15];
    ports["portb_direction"] = ((mixerValue & 0x80) ? "input" : "output");
    ret["io_ports"] = ports;

    ret["sound_played_since_reset"] = false;  // TODO: Implement sound played tracking

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
void EmulatorAPI::getStateAudioGS(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    Json::Value ret;
    ret["status"] = "not_implemented";
    ret["description"] =
        "General Sound (GS) is a sound expansion device that was planned for the ZX Spectrum but never released "
        "commercially.";
    ret["note"] = "This endpoint is reserved for future implementation.";

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

    // General Sound (not implemented)
    Json::Value gs;
    gs["available"] = false;
    gs["status"] = "not_implemented";
    ret["general_sound"] = gs;

    // Covox (not implemented)
    Json::Value covox;
    covox["available"] = false;
    covox["status"] = "not_implemented";
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
/// Uses stateless auto-selection: only works if exactly one emulator exists
void EmulatorAPI::getStateAudioAYActive(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorStateless();

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
    auto emulator = getEmulatorStateless();

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
    auto emulator = getEmulatorStateless();

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
    auto emulator = getEmulatorStateless();

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
    auto emulator = getEmulatorStateless();

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
    auto emulator = getEmulatorStateless();

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
    auto emulator = getEmulatorStateless();

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

} // namespace v1
} // namespace api
