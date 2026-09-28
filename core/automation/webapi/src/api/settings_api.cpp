// WebAPI Settings Management Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include "../common/jsonnumber.h"
#include "../emulator_api.h"

#include <algorithm>

#include <drogon/HttpResponse.h>
#include <base/featuremanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/sound/audio.h>
#include <emulator/sound/soundcharactersettings.h>
#include <emulator/sound/soundmanager.h>
#include <json/json.h>

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
/// Sound-character setting value as JSON: on/off settings as bool, the rest as text
Json::Value SoundCharacterJson(const SoundManager& sound, const SoundCharacterSettings::Descriptor& d)
{
    const std::string value = SoundCharacterSettings::Get(sound, d.name);
    if (d.isBool)
        return Json::Value(value == "on");
    return Json::Value(value);
}

Json::Value AllowedJson(const std::string& name)
{
    Json::Value allowed(Json::arrayValue);
    for (const std::string& v : SoundCharacterSettings::AllowedValues(name))
        allowed.append(v);
    return allowed;
}
}  // namespace

/// @brief GET /api/v1/emulator/{id}/settings
/// @brief Get all settings for an emulator
void EmulatorAPI::getSettings(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    CONFIG& config = context->config;

    Json::Value ret;
    Json::Value settings(Json::objectValue);

    // I/O Acceleration settings (fast_tape, turbo_tape, fast_disk are backed by FeatureManager)
    FeatureManager* featureManager = context->pFeatureManager;
    Json::Value io_accel(Json::objectValue);
    io_accel["fast_tape"] = featureManager && featureManager->isEnabled(Features::kFastTape);
    io_accel["turbo_tape"] = featureManager && featureManager->isEnabled(Features::kTurboTape);
    io_accel["fast_disk"] = featureManager && featureManager->isEnabled(Features::kFastDisk);
    settings["io_acceleration"] = io_accel;

    // Turbo (max speed) mode - also FeatureManager-backed, forced off and blocked
    // from re-enabling while TTD recording is active (same gate as io_acceleration)
    settings["turbo_mode"] = featureManager && featureManager->isEnabled(Features::kTurboMode);
    // Host speed control, and whether the engine runs unthrottled right now
    // (turbo_mode, or turbo tape warping a load); both locked while TTD records
    settings["speed"]        = static_cast<unsigned>(context->pCore ? context->pCore->GetHostSpeedMultiplier() : 1);
    settings["turbo_active"] = config.turbo_mode;
    settings["turbo_audio"]  = config.turbo_mode_audio;

    // Disk Interface settings
    Json::Value disk_if(Json::objectValue);
    disk_if["trdos_present"] = config.trdos_present;
    disk_if["trdos_traps"] = config.trdos_traps;
    settings["disk_interface"] = disk_if;

    // Audio settings: the core-rate priority chain (runtime pin > device >
    // [SOUND] CoreRate > 44100) - same source the CLI 'setting audio_rate'
    // and Lua/Python set_audio_rate serve
    Json::Value audio(Json::objectValue);
    SoundManager* soundManager = context->pSoundManager;
    const uint32_t pin = soundManager ? soundManager->getCoreRatePin() : 0;
    audio["audio_rate"] = pin ? Json::Value(pin) : Json::Value(std::string("auto"));
    audio["core_rate_hz"] = static_cast<unsigned>(soundManager ? soundManager->getCoreRate() : 44100u);
    // Sound character (ay_voicing, ay_punch, ay_room, beeper_punch) - the same
    // source the CLI 'setting' and Lua/Python get_sound_character serve
    if (soundManager)
    {
        for (const SoundCharacterSettings::Descriptor& d : SoundCharacterSettings::Descriptors())
            audio[d.name] = SoundCharacterJson(*soundManager, d);
    }
    settings["audio"] = audio;

    ret["emulator_id"] = id;
    ret["settings"] = settings;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/settings/{name}
/// @brief Get a specific setting value
void EmulatorAPI::getSetting(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id, const std::string& name) const
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

    CONFIG& config = context->config;
    Json::Value ret;

    const SoundCharacterSettings::Descriptor* soundSetting = SoundCharacterSettings::Find(name);
    if (soundSetting && context->pSoundManager)
    {
        ret["name"] = soundSetting->name;
        ret["value"] = SoundCharacterJson(*context->pSoundManager, *soundSetting);
        ret["allowed"] = AllowedJson(soundSetting->name);
        ret["description"] = std::string(soundSetting->description) +
                             ". Applied at the next frame boundary; runtime only, never written to the ini";
    }
    else if (name == "fast_tape")
    {
        FeatureManager* featureManager = context->pFeatureManager;
        ret["name"] = "fast_tape";
        ret["value"] = featureManager && featureManager->isEnabled(Features::kFastTape);
        ret["description"] = "Fast tape loading (bypasses audio emulation)";
    }
    else if (name == "turbo_tape")
    {
        FeatureManager* featureManager = context->pFeatureManager;
        ret["name"] = "turbo_tape";
        ret["value"] = featureManager && featureManager->isEnabled(Features::kTurboTape);
        ret["description"] = "Turbo tape loading (warp speed while a tape signal plays, custom loaders included)";
    }
    else if (name == "fast_disk")
    {
        FeatureManager* featureManager = context->pFeatureManager;
        ret["name"] = "fast_disk";
        ret["value"] = featureManager && featureManager->isEnabled(Features::kFastDisk);
        ret["description"] = "Fast disk loading (FDC timing compression and TR-DOS ROM traps)";
    }
    else if (name == "turbo_mode")
    {
        FeatureManager* featureManager = context->pFeatureManager;
        ret["name"] = "turbo_mode";
        ret["value"] = featureManager && featureManager->isEnabled(Features::kTurboMode);
        ret["description"] = "Turbo (max speed) mode. Forced off and blocked from re-enabling while TTD recording is active.";
    }
    else if (name == "speed")
    {
        ret["name"] = "speed";
        ret["value"] = static_cast<unsigned>(context->pCore ? context->pCore->GetHostSpeedMultiplier() : 1);
        ret["allowed"] = Json::Value(Json::arrayValue);
        for (unsigned m : {1u, 2u, 4u, 8u, 16u})
            ret["allowed"].append(m);
        ret["description"] = "Host speed multiplier (the emulated machine runs N times faster). Only 1 while TTD "
                             "records; a change on a stopped TTD session drops it.";
    }
    else if (name == "turbo_active")
    {
        ret["name"] = "turbo_active";
        ret["value"] = config.turbo_mode;
        ret["read_only"] = true;
        ret["description"] = "Whether emulation runs unthrottled right now: turbo_mode, or turbo tape warping a load";
    }
    else if (name == "turbo_audio")
    {
        ret["name"] = "turbo_audio";
        ret["value"] = config.turbo_mode_audio;
        ret["description"] = "Keep generating audio (at raised pitch) while in turbo mode";
    }
    else if (name == "trdos_present")
    {
        ret["name"] = "trdos_present";
        ret["value"] = config.trdos_present;
        ret["description"] = "Enable Beta128 TR-DOS disk interface";
    }
    else if (name == "trdos_traps")
    {
        ret["name"] = "trdos_traps";
        ret["value"] = config.trdos_traps;
        ret["description"] = "Use TR-DOS traps for faster disk operations";
    }
    else if (name == "audio_rate")
    {
        SoundManager* soundManager = context->pSoundManager;
        const uint32_t pin = soundManager ? soundManager->getCoreRatePin() : 0;
        ret["name"] = "audio_rate";
        ret["value"] = pin ? Json::Value(pin) : Json::Value(std::string("auto"));
        ret["core_rate_hz"] = static_cast<unsigned>(soundManager ? soundManager->getCoreRate() : 44100u);
        ret["description"] = "Runtime core audio rate pin (44100..192000 or auto). "
                             "Applied at the next frame boundary; deferred while recording. "
                             "Never persisted to the ini.";
    }
    else
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown setting: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ret["emulator_id"] = id;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief PUT/POST /api/v1/emulator/{id}/settings/{name}
/// @brief Set a specific setting value
void EmulatorAPI::setSetting(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id, const std::string& name) const
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

    // Parse request body for the new value
    auto json = req->getJsonObject();
    if (!json || !json->isMember("value"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'value' field in request body";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Sound character settings (ay_voicing, ay_punch, ay_room, beeper_punch):
    // one parser for every automation surface (SoundCharacterSettings). The
    // value may be a string or, for the on/off ones, a JSON bool
    if (const SoundCharacterSettings::Descriptor* soundSetting = SoundCharacterSettings::Find(name))
    {
        SoundManager* soundManager = context->pSoundManager;
        auto badRequest = [&](const std::string& message) {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = message;
            error["allowed"] = AllowedJson(soundSetting->name);
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
        };
        if (!soundManager)
        {
            Json::Value error;
            error["error"] = "Internal Error";
            error["message"] = "Sound manager not available for this emulator";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k500InternalServerError);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        const Json::Value value = (*json)["value"];
        std::string text;
        if (value.isBool())
            text = value.asBool() ? "on" : "off";
        else if (value.isString())
            text = value.asString();
        else
        {
            badRequest(std::string("Invalid ") + soundSetting->name + " value: expected a string" +
                       (soundSetting->isBool ? " or a bool" : ""));
            return;
        }

        std::string errorMessage;
        if (!SoundCharacterSettings::Set(*soundManager, soundSetting->name, text, errorMessage))
        {
            badRequest(errorMessage);
            return;
        }

        Json::Value ret;
        ret["name"] = soundSetting->name;
        ret["value"] = SoundCharacterJson(*soundManager, *soundSetting);
        ret["message"] = std::string(soundSetting->name) + " set (applied at the next frame boundary)";
        ret["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto reply = [&](HttpStatusCode code, const std::string& error, const std::string& message) {
        Json::Value body;
        body["error"] = error;
        body["message"] = message;
        auto resp = HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(code);
        addCorsHeaders(resp);
        callback(resp);
    };

    if (name == "turbo_active")
    {
        reply(HttpStatusCode::k400BadRequest, "Bad Request",
              "turbo_active is read-only (switch turbo with turbo_mode)");
        return;
    }

    // Host speed multiplier (same switch as CLI 'setting speed N'): 1, 2, 4, 8 or 16
    if (name == "speed")
    {
        uint32_t multiplier = 0;
        if (!ParseJsonUInt((*json)["value"], 16, multiplier) ||
            (multiplier != 1 && multiplier != 2 && multiplier != 4 && multiplier != 8 && multiplier != 16))
        {
            reply(HttpStatusCode::k400BadRequest, "Bad Request", "speed must be 1, 2, 4, 8 or 16");
            return;
        }
        if (!emulator->SetSpeedMultiplier(static_cast<uint8_t>(multiplier)))
        {
            reply(HttpStatusCode::k409Conflict, "Conflict", "Cannot change the speed while TTD recording is active (only 1)");
            return;
        }
        Json::Value ret;
        ret["name"] = "speed";
        ret["value"] = multiplier;
        ret["message"] = "Speed multiplier set to " + std::to_string(multiplier) + "x (applied at the next frame)";
        ret["emulator_id"] = id;
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Non-boolean setting handled first: the value is a rate number or
    // "auto" (same switch as CLI 'setting audio_rate'). Never persisted to
    // the ini - runtime pin only.
    if (name == "audio_rate")
    {
        SoundManager* soundManager = context->pSoundManager;
        if (!soundManager)
        {
            Json::Value error;
            error["error"] = "Internal Error";
            error["message"] = "Sound manager not available for this emulator";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k500InternalServerError);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        const Json::Value value = (*json)["value"];
        uint32_t pin = 0;
        if (value.isString())
        {
            std::string text = value.asString();
            std::transform(text.begin(), text.end(), text.begin(), ::tolower);
            if (text != "auto")
            {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Invalid audio_rate value. Use 44100, 48000, 88200, 96000, 176400, 192000 or auto";

                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
        }
        else if (value.isNumeric())
        {
            pin = static_cast<uint32_t>(value.asUInt());
            if (!IsSupportedCoreRate(pin))
            {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "Unsupported audio_rate " + std::to_string(pin) +
                                    ". Use 44100, 48000, 88200, 96000, 176400, 192000 or auto";

                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
        }
        else
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = "Invalid audio_rate value. Use 44100..192000 or auto";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        soundManager->setCoreRatePin(pin);

        Json::Value ret;
        ret["name"] = "audio_rate";
        ret["value"] = pin ? Json::Value(pin) : Json::Value(std::string("auto"));
        ret["message"] = pin ? "Core audio rate pinned to " + std::to_string(pin) +
                                   " Hz (applied at the next frame boundary; deferred while recording)"
                             : "Core audio rate pin released (follows device, then config)";
        ret["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    bool boolValue = (*json)["value"].asBool();
    CONFIG& config = context->config;
    Json::Value ret;

    // fast_tape / turbo_tape / fast_disk are the runtime features behind the
    // feature API; TTD holds them off while recording or replaying history
    struct ShortcutSetting { const char* name; const char* feature; const char* label; };
    static const ShortcutSetting kShortcuts[] = {
        {"fast_tape", Features::kFastTape, "Fast tape loading"},
        {"turbo_tape", Features::kTurboTape, "Turbo tape loading"},
        {"fast_disk", Features::kFastDisk, "Fast disk loading"},
    };
    const ShortcutSetting* shortcut = nullptr;
    for (const ShortcutSetting& candidate : kShortcuts)
        if (name == candidate.name)
            shortcut = &candidate;

    if (shortcut)
    {
        FeatureManager* featureManager = context->pFeatureManager;
        if (!featureManager || !featureManager->setFeature(shortcut->feature, boolValue))
        {
            Json::Value error;
            error["error"] = "Conflict";
            error["message"] = std::string("Cannot enable ") + shortcut->name +
                               " while TTD recording is active or history is being replayed";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k409Conflict);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        if (name == "fast_disk")
            config.wd93_nodelay = boolValue;
        ret["name"] = shortcut->name;
        ret["value"] = boolValue;
        ret["message"] = std::string(shortcut->label) + " is now " + (boolValue ? "enabled" : "disabled");
    }
    else if (name == "turbo_mode")
    {
        // Routed through FeatureManager (not Core::EnableTurboMode directly) so the
        // TTD-recording lock applies: setFeature() refuses to enable turbo mode while
        // a recording is in progress.
        FeatureManager* featureManager = context->pFeatureManager;
        bool applied = featureManager && featureManager->setFeature(Features::kTurboMode, boolValue);
        if (boolValue && !applied)
        {
            Json::Value error;
            error["error"] = "Conflict";
            error["message"] = "Cannot enable turbo mode while TTD recording is active";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k409Conflict);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
        ret["name"] = "turbo_mode";
        ret["value"] = boolValue;
        ret["message"] = std::string("Turbo mode is now ") + (boolValue ? "enabled" : "disabled");
    }
    else if (name == "turbo_audio")
    {
        // Same as CLI 'setting turbo_audio': re-applied at once if turbo is on
        config.turbo_mode_audio = boolValue;
        if (config.turbo_mode)
            emulator->EnableTurboMode(boolValue);
        ret["name"] = "turbo_audio";
        ret["value"] = boolValue;
        ret["message"] = std::string("Audio in turbo mode is now ") + (boolValue ? "enabled" : "disabled");
    }
    else if (name == "trdos_present")
    {
        config.trdos_present = boolValue;
        ret["name"] = "trdos_present";
        ret["value"] = boolValue;
        ret["message"] = std::string("TR-DOS interface is now ") + (boolValue ? "enabled" : "disabled");
        ret["restart_required"] = true;
    }
    else if (name == "trdos_traps")
    {
        config.trdos_traps = boolValue;
        ret["name"] = "trdos_traps";
        ret["value"] = boolValue;
        ret["message"] = std::string("TR-DOS traps are now ") + (boolValue ? "enabled" : "disabled");
    }
    else
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Unknown setting: " + name;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    ret["emulator_id"] = id;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api
