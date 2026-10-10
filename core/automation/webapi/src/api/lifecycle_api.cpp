// WebAPI Emulator Lifecycle Management Implementation
// Extracted from emulator_api.cpp - 2026-01-08

#include <emulator/ports/models/profiboard.h>
#include <emulator/ports/models/sprinter/sprinterbios.h>
#include "../emulator_api.h"
#include "../common/statenode_json.h"

#include <drogon/HttpResponse.h>
#include <emulator/buildinfo.h>
#include <emulator/config.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/zxpoly/zxpolygroup.h>
#include <emulator/machinevariants.h>
#include <emulator/media/modelswitch.h>
#include <emulator/slots/slotcontrol.h>
#include <emulator/platform.h>
#include <emulator/ports/portdecoder.h>
#include <json/json.h>

#include <functional>
#include <optional>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function declared in emulator_api.cpp
extern void addCorsHeaders(HttpResponsePtr& resp);
extern std::string stateToString(EmulatorStateEnum state);

namespace
{
/// Optional "ram_power_on": "random" | "zero" of a create / start / model
/// switch body. True when absent or valid (mode set only when present);
/// false with a 400 already sent when the value is not one of the two
bool ParseRamPowerOnField(const std::shared_ptr<Json::Value>& json, std::optional<RamPowerOn>& mode,
                          const std::function<void(const HttpResponsePtr&)>& callback)
{
    if (!json || !json->isMember("ram_power_on"))
        return true;
    RamPowerOn parsed = RamPowerOn::Random;
    const Json::Value& value = (*json)["ram_power_on"];
    if (value.isString() && Config::ParseRamPowerOn(value.asString(), parsed))
    {
        mode = parsed;
        return true;
    }
    Json::Value error;
    error["error"] = "Bad Request";
    error["message"] = "ram_power_on must be \"random\" or \"zero\"";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
    return false;
}

/// The create-time override for a parsed ram_power_on (none when absent)
std::function<void(CONFIG&)> RamPowerOnOverride(const std::optional<RamPowerOn>& mode)
{
    return mode ? Config::RamPowerOnOverride(*mode) : std::function<void(CONFIG&)>();
}

/// Optional "sprinter": {"bios": "3.06" | <file>, "fast_start": bool, "accel_int_suspend": bool, "isa_slot1": "none",
/// "isa_slot2": "ne2000"} of a create /
/// start body (SprinterBios, automation audit G11): the BIOS image and start options of a new SPRINTER.
/// True when absent or valid; false with a 400 already sent
bool ParseSprinterField(const std::shared_ptr<Json::Value>& json, std::function<void(CONFIG&)>& out,
                        const std::function<void(const HttpResponsePtr&)>& callback)
{
    if (!json || !json->isMember("sprinter"))
        return true;
    const Json::Value& value = (*json)["sprinter"];
    auto text = [&](const char* key) -> std::string {
        if (!value.isMember(key))
            return std::string();
        const Json::Value& v = value[key];
        return v.isBool() ? (v.asBool() ? "1" : "0") : v.asString();
    };
    SprinterBios::Options options;
    std::string error, path;
    if (!value.isObject() ||
        !SprinterBios::OptionsFromStrings(text("bios"), text("fast_start"), text("accel_int_suspend"), "", options, error) ||
        !SprinterBios::IsaSlotFromString(text("isa_slot1"), 0, options, error) ||
        !SprinterBios::IsaSlotFromString(text("isa_slot2"), 1, options, error) ||
        (!options.bios.empty() && !SprinterBios::Resolve(options.bios, path, error)))
    {
        Json::Value err;
        err["error"] = "Bad Request";
        err["message"] = error.empty() ? std::string("sprinter must be an object {bios, fast_start, accel_int_suspend, isa_slot1, isa_slot2}")
                                       : "sprinter: " + error;
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return false;
    }
    out = SprinterBios::CreateOverride(options);
    return true;
}

std::function<void(CONFIG&)> Combine(std::function<void(CONFIG&)> first, std::function<void(CONFIG&)> second);

/// Optional "slots": {"zxbus.1": "multisound", "zxbus.1.dip": "ym,gs", "ay-socket": "none"} of a create / start body
/// (ZX-bus slots architecture.md §9): the new machine's slot set in the [SLOTS] key form, replacing its INI's. A
/// value may be an array (a set option). True when absent or valid; false with a 400 already sent
bool ParseSlotsField(const std::shared_ptr<Json::Value>& json, std::function<void(CONFIG&)>& out,
                     const std::function<void(const HttpResponsePtr&)>& callback)
{
    if (!json || !json->isMember("slots"))
        return true;
    const Json::Value& value = (*json)["slots"];
    std::vector<std::pair<std::string, std::string>> keyValues;
    std::string error = "slots must be an object of [SLOTS] keys: {\"zxbus.1\": \"multisound\", \"zxbus.1.dip\": \"ym,gs\"}";
    bool ok = value.isObject();
    if (ok)
    {
        for (const std::string& key : value.getMemberNames())
        {
            const Json::Value& v = value[key];
            std::string text;
            if (v.isArray())
            {
                for (const Json::Value& item : v)
                    text += (text.empty() ? "" : ",") + item.asString();
            }
            else if (v.isBool())
                text = v.asBool() ? "on" : "off";
            else
                text = v.asString();
            keyValues.emplace_back(key, text);
        }
        std::string why;
        ok = SlotControl::CreateOverride(keyValues, out, why);
        if (!ok)
            error = "slots: " + why;
    }
    if (ok)
        return true;
    Json::Value err;
    err["error"] = "Bad Request";
    err["message"] = error;
    auto resp = HttpResponse::newHttpJsonResponse(err);
    resp->setStatusCode(HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
    return false;
}

/// Optional "profi": {"keyboard": "matrix" | "xt" | "xttable" | "default", "zq3_mhz": 16..24 (even), "ay_clock":
/// "old" | "new"} of a create / start body: the keyboard on a new Profi's keyboard connector ([PROFI] Keyboard=,
/// ProfiKeyboardOverride) and its hi-res clocks ([PROFI] ZQ3MHz / AyClock, ProfiClockOverride). True when absent or
/// valid; false with a 400 already sent
bool ParseProfiField(const std::shared_ptr<Json::Value>& json, std::function<void(CONFIG&)>& out,
                     const std::function<void(const HttpResponsePtr&)>& callback)
{
    if (!json || !json->isMember("profi"))
        return true;
    const Json::Value& value = (*json)["profi"];
    ProfiKeyboard keyboard = ProfiKeyboard::Default;
    uint8_t zq3 = 0;
    int ayNew = -1;
    bool clocksOk = value.isObject();
    if (clocksOk && value.isMember("zq3_mhz"))
    {
        const int mhz = value["zq3_mhz"].isInt() ? value["zq3_mhz"].asInt() : 0;
        clocksOk = mhz >= 16 && mhz <= 24 && (mhz % 2) == 0;
        zq3 = static_cast<uint8_t>(mhz);
    }
    if (clocksOk && value.isMember("ay_clock"))
    {
        const std::string ay = value["ay_clock"].isString() ? value["ay_clock"].asString() : "";
        clocksOk = ay == "old" || ay == "new";
        ayNew = ay == "new" ? 1 : 0;
    }
    if (!clocksOk || (value.isMember("keyboard") && !value["keyboard"].isString()) ||
        !ParseProfiKeyboard(value.get("keyboard", "").asString().c_str(), keyboard))
    {
        Json::Value err;
        err["error"] = "Bad Request";
        err["message"] = "profi must be an object {keyboard: matrix | xt | xttable | default, zq3_mhz: 16..24 (even), "
                         "ay_clock: old | new}";
        auto resp = HttpResponse::newHttpJsonResponse(err);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return false;
    }
    out = Combine(ProfiKeyboardOverride(keyboard), ProfiClockOverride(zq3, ayNew));
    return true;
}

/// Both create-time overrides in order (either may be empty)
std::function<void(CONFIG&)> Combine(std::function<void(CONFIG&)> first, std::function<void(CONFIG&)> second)
{
    if (!first)
        return second;
    if (!second)
        return first;
    return [first, second](CONFIG& config) {
        first(config);
        second(config);
    };
}

// Machine identity block shared by lifecycle responses (P0-1: a triage
// session must always see WHICH machine it is talking to). Computed by
// EmulatorManager::GetMachineIdentity - the single source the CLI also uses -
// so every automation surface reports identical information. Every read is
// guarded so a partially initialized instance still yields a complete
// object; "video_mode" is null until the screen subsystem exists.
void AddIdentityFields(Json::Value& target, Emulator& emulator)
{
    const MachineIdentity identity = EmulatorManager::GetMachineIdentity(emulator);

    if (!identity.Valid)
    {
        target["model"] = Json::Value();
        target["model_full_name"] = Json::Value();
        target["ram_kb"] = Json::Value();
        target["video_mode"] = Json::Value();
        target["speed_multiplier"] = Json::Value();
        target["config_folder"] = Json::Value();
        target["ram_power_on"] = Json::Value();
        return;
    }

    target["model"] = identity.Model;
    target["model_full_name"] = identity.ModelFullName;
    target["ram_kb"] = static_cast<Json::UInt>(identity.RamKb);
    target["video_mode"] = identity.HasVideoMode ? Json::Value(identity.VideoMode) : Json::Value();
    target["speed_multiplier"] = identity.SpeedMultiplier;
    target["config_folder"] = identity.ConfigFolder;
    target["ram_power_on"] = identity.RamPowerOn;
    // A machine variant (MachineVariants): the base model with a fixed board ("TSL-VDAC2")
    if (!identity.Variant.empty())
    {
        target["variant"] = identity.Variant;
        target["variant_title"] = identity.VariantTitle;
    }

    // ZX-Poly: the instance is a module of a four-CPU group
    if (identity.ZXPoly)
    {
        Json::Value zxpoly;
        zxpoly["module"] = identity.ZXPolyModule;
        zxpoly["master_id"] = identity.ZXPolyMasterId;
        zxpoly["locked"] = identity.ZXPolyLocked;
        zxpoly["video_mode"] = identity.ZXPolyVideoMode;
        target["zxpoly"] = zxpoly;
    }
}

/// ZXPolyGroup::Status as JSON (GET .../zxpoly, the start response)
Json::Value ZXPolyStatusJson(const ZXPolyGroup::Status& status)
{
    Json::Value out;
    out["master_id"] = status.memberIds[0];
    out["locked"] = status.locked;
    out["slaves_running"] = status.slavesRunning;
    out["parallel_slaves"] = status.parallelSlaves;
    out["pipelined_slaves"] = status.pipelinedSlaves;
    out["port_3d00"] = static_cast<Json::UInt>(status.port3D00);
    out["video_mode"] = static_cast<Json::UInt>(status.videoMode);

    Json::Value modules(Json::arrayValue);
    for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
    {
        Json::Value module;
        module["module"] = static_cast<Json::UInt>(m);
        module["id"] = status.memberIds[m];
        Json::Value registers(Json::arrayValue);
        for (uint8_t value : status.registers[m])
            registers.append(static_cast<Json::UInt>(value));
        module["registers"] = registers;
        modules.append(module);
    }
    out["modules"] = modules;

    Json::Value divergence;
    divergence["diverged"] = status.divergence.diverged;
    if (status.divergence.diverged)
    {
        divergence["module"] = static_cast<Json::UInt>(status.divergence.module);
        divergence["what"] = status.divergence.what;
    }
    out["divergence"] = divergence;
    return out;
}
}

/// @brief GET /api/v1/emulator
/// @brief List all emulators
void EmulatorAPI::get(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulatorIds = manager->GetEmulatorIds();

    Json::Value ret;
    Json::Value emulators(Json::arrayValue);

    for (const auto& id : emulatorIds)
    {
        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            Json::Value emuInfo;
            emuInfo["id"] = id;
            emuInfo["symbolic_id"] = emulator->GetSymbolicId();
            emuInfo["state"] = stateToString(emulator->GetState());
            emuInfo["is_running"] = emulator->IsRunning();
            emuInfo["is_paused"] = emulator->IsPaused();
            emuInfo["is_debug"] = emulator->IsDebug();
            AddIdentityFields(emuInfo, *emulator);
            emulators.append(emuInfo);
        }
    }

    ret["emulators"] = emulators;
    ret["count"] = static_cast<Json::UInt>(emulatorIds.size());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/models
/// @brief Get available emulator models
void EmulatorAPI::getModels(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    Json::Value ret;
    Json::Value modelsArray(Json::arrayValue);

    try
    {
        auto manager = EmulatorManager::GetInstance();
        auto models = manager->GetAvailableModels();

        for (const auto& model : models)
        {
            // Skip empty/sentinel entries (entries with NULL or empty ShortName)
            if (!model.ShortName || model.ShortName[0] == '\0')
            {
                continue;
            }


            Json::Value modelInfo;

            // Order fields logically: id and name first, then details
            modelInfo["id"] = static_cast<int>(model.Model);
            modelInfo["name"] = std::string(model.ShortName);
            modelInfo["full_name"] = model.FullName ? std::string(model.FullName) : "";
            modelInfo["default_ram_kb"] = model.defaultRAM;

            // Parse available RAM sizes from bitmask
            Json::Value availableRAMs(Json::arrayValue);
            unsigned ramMask = model.AvailRAMs;
            const int ramSizes[] = {48, 128, 256, 512, 1024, 2048, 4096};
            for (int ram : ramSizes)
            {
                if (ramMask & ram)
                {
                    availableRAMs.append(ram);
                }
            }
            modelInfo["available_ram_sizes_kb"] = availableRAMs;

            // Whether a create request for this model is expected to succeed
            // on this build (port decoder + config folder present). Lets
            // clients filter machine lists instead of discovering unsupported
            // models one failed request at a time.
            modelInfo["creatable"] = Config::IsModelCreatable(model);

            modelsArray.append(modelInfo);
        }

        // ZX-Poly configurations: four synchronized instances of a base model,
        // created by name like any model
        for (const ZXPolyGroup::Configuration& configuration : ZXPolyGroup::Configurations())
        {
            const TMemModel* base = Config::FindModelByShortName(configuration.baseModel);
            Json::Value modelInfo;
            modelInfo["name"] = std::string(configuration.name);
            modelInfo["full_name"] = std::string(configuration.title);
            modelInfo["zxpoly"] = true;
            modelInfo["base_model"] = std::string(configuration.baseModel);
            modelInfo["default_ram_kb"] = base ? base->defaultRAM : 0;
            modelInfo["creatable"] = base != nullptr && Config::IsModelCreatable(*base);
            modelsArray.append(modelInfo);
        }

        // Machine variants: a base model with a fixed board, created by name like any model
        for (const MachineVariant& variant : MachineVariants::All())
        {
            const TMemModel* base = Config::FindModelByShortName(variant.baseModel);
            Json::Value modelInfo;
            modelInfo["name"] = std::string(variant.name);
            modelInfo["full_name"] = std::string(variant.title);
            modelInfo["description"] = std::string(variant.description);
            modelInfo["variant"] = true;
            modelInfo["base_model"] = std::string(variant.baseModel);
            modelInfo["default_ram_kb"] = variant.ramKb;
            modelInfo["creatable"] = base != nullptr && Config::IsModelCreatable(*base) && variant.supported(nullptr);
            modelsArray.append(modelInfo);
        }

        ret["models"] = modelsArray;
        ret["count"] = static_cast<Json::UInt>(modelsArray.size());

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Failed to retrieve models";
        error["message"] = e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief GET /api/v1/emulator/status
/// @brief Get overall emulator status
void EmulatorAPI::status(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulatorIds = manager->GetEmulatorIds();

    Json::Value ret;
    ret["emulator_count"] = static_cast<Json::UInt>(emulatorIds.size());

    // Count emulators by state
    Json::Value states(Json::objectValue);
    for (const auto& id : emulatorIds)
    {
        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            std::string state = stateToString(emulator->GetState());
            if (states.isMember(state))
            {
                states[state] = states[state].asInt() + 1;
            }
            else
            {
                states[state] = 1;
            }
        }
    }
    ret["states"] = states;

    // Build fingerprint + which machines this build can actually create
    // (P0-4): lets triage sessions attribute behaviour to a specific
    // branch/commit and discover unsupported machines up front.
    Json::Value server(Json::objectValue);
    server["version"] = buildinfo::kVersion;
    server["git_branch"] = buildinfo::kGitBranch;
    server["git_commit"] = buildinfo::kGitCommit;
    server["build_type"] = buildinfo::kBuildType;
    ret["server"] = server;

    Json::Value creatableModels(Json::arrayValue);
    for (const TMemModel& model : manager->GetAvailableModels())
    {
        if (model.ShortName != nullptr && model.ShortName[0] != '\0' && Config::IsModelCreatable(model))
        {
            creatableModels.append(std::string(model.ShortName));
        }
    }
    for (const ZXPolyGroup::Configuration& configuration : ZXPolyGroup::Configurations())
    {
        const TMemModel* base = Config::FindModelByShortName(configuration.baseModel);
        if (base != nullptr && Config::IsModelCreatable(*base))
            creatableModels.append(std::string(configuration.name));
    }
    for (const MachineVariant& variant : MachineVariants::All())
    {
        const TMemModel* base = Config::FindModelByShortName(variant.baseModel);
        if (base != nullptr && Config::IsModelCreatable(*base) && variant.supported(nullptr))
            creatableModels.append(std::string(variant.name));
    }
    ret["models_creatable"] = creatableModels;

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator
/// @brief Create a new emulator instance
/// @brief Request body (all optional):
/// @brief {
/// @brief   "symbolic_id": "my-emulator",
/// @brief   "model": "48K" | "128K" | "PENTAGON" | etc,
/// @brief   "ram_size": 128 (in KB, only valid for models that support it),
/// @brief   "ram_power_on": "random" | "zero" (RAM contents at creation; default: the model's unreal.ini)
/// @brief }
/// @brief A non-empty "model" that cannot be created on this build fails with
/// @brief 400 + reason - there is NO silent fallback to a default machine.
void EmulatorAPI::createEmulator(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto manager = EmulatorManager::GetInstance();

    // Parse request body
    auto json = req->getJsonObject();
    std::string symbolicId = json ? (*json)["symbolic_id"].asString() : "";
    std::string modelName = json ? (*json)["model"].asString() : "";
    uint32_t ramSize = json && json->isMember("ram_size") ? (*json)["ram_size"].asUInt() : 0;
    std::optional<RamPowerOn> ramPowerOn;
    if (!ParseRamPowerOnField(json, ramPowerOn, callback))
        return;
    std::function<void(CONFIG&)> sprinterOverride;
    if (!ParseSprinterField(json, sprinterOverride, callback))
        return;
    std::function<void(CONFIG&)> profiOverride;
    if (!ParseProfiField(json, profiOverride, callback))
        return;
    std::function<void(CONFIG&)> slotsOverride;
    if (!ParseSlotsField(json, slotsOverride, callback))
        return;
    const std::function<void(CONFIG&)> createOverride =
        Combine(Combine(Combine(RamPowerOnOverride(ramPowerOn), sprinterOverride), profiOverride), slotsOverride);

    try
    {
        std::shared_ptr<Emulator> emulator;
        std::string createError;

        if (!modelName.empty() && ramSize > 0)
        {
            // Create with specific model and RAM size - strict: no fallback
            emulator = manager->CreateEmulatorWithModelAndRAM(symbolicId, modelName, ramSize,
                                                              LoggerLevel::LogWarning, &createError,
                                                              createOverride);
        }
        else if (!modelName.empty())
        {
            // Create with specific model (default RAM) - strict: no fallback
            emulator = manager->CreateEmulatorWithModel(symbolicId, modelName, LoggerLevel::LogWarning, &createError,
                                                        createOverride);
        }
        else
        {
            // Create with default configuration (48K)
            emulator = manager->CreateEmulator(symbolicId, LoggerLevel::LogWarning, createOverride);
        }

        if (!emulator)
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = !createError.empty() ? createError : std::string("Emulator initialization failed");
            if (!modelName.empty())
            {
                error["requested_model"] = modelName;
            }
            error["available_models_endpoint"] = "/api/v1/emulator/models";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        Json::Value ret;
        ret["id"] = emulator->GetId();
        ret["state"] = stateToString(emulator->GetState());
        ret["symbolic_id"] = emulator->GetSymbolicId();
        AddIdentityFields(ret, *emulator);

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(HttpStatusCode::k201Created);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Failed to create emulator";
        error["message"] = e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief GET /api/v1/emulator/{id}
/// @brief Get emulator details
void EmulatorAPI::getEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
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

    Json::Value ret;
    ret["id"] = id;
    ret["symbolic_id"] = emulator->GetSymbolicId();
    ret["state"] = stateToString(emulator->GetState());
    ret["is_running"] = emulator->IsRunning();
    ret["is_paused"] = emulator->IsPaused();
    ret["is_debug"] = emulator->IsDebug();
    AddIdentityFields(ret, *emulator);

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/{id}/zxpoly
/// @details The ZX-Poly group of any member: modules, platform registers,
/// lock, video mode and the lockstep check
void EmulatorAPI::getZXPolyStatus(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id) const
{
    (void)req;
    auto manager = EmulatorManager::GetInstance();
    ZXPolyGroup* group = manager->GetZXPolyGroup(id);
    if (!group)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = manager->GetEmulator(id) ? "The emulator is not a ZX-Poly machine"
                                                    : "Emulator with specified ID not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto resp = HttpResponse::newHttpJsonResponse(ZXPolyStatusJson(group->GetStatus()));
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief DELETE /api/v1/emulator/{id}
/// @brief Remove an emulator
void EmulatorAPI::removeEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    bool removed = manager->RemoveEmulator(id);

    Json::Value ret;
    if (removed)
    {
        ret["status"] = "success";
        ret["message"] = "Emulator removed successfully";

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(HttpStatusCode::k200OK);
        addCorsHeaders(resp);
        callback(resp);
    }
    else
    {
        ret["status"] = "error";
        ret["message"] = "Failed to remove emulator";

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

///  @brief POST /api/v1/emulator/start
/// @brief Create and start a new emulator
/// @brief Request body (all optional):
/// @brief {
/// @brief   "symbolic_id": "my-emulator",
/// @brief   "model": "48K" | "128K" | "PENTAGON" | etc,
/// @brief   "ram_size": 128 (in KB, only valid for models that support it),
/// @brief   "ram_power_on": "random" | "zero" (RAM contents at creation; default: the model's unreal.ini)
/// @brief }
void EmulatorAPI::startEmulator(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto manager = EmulatorManager::GetInstance();

    // Parse request body
    auto json = req->getJsonObject();
    std::string symbolicId = json ? (*json)["symbolic_id"].asString() : "";
    std::string modelName = json ? (*json)["model"].asString() : "";
    uint32_t ramSize = json && json->isMember("ram_size") ? (*json)["ram_size"].asUInt() : 0;
    std::optional<RamPowerOn> ramPowerOn;
    if (!ParseRamPowerOnField(json, ramPowerOn, callback))
        return;
    std::function<void(CONFIG&)> sprinterOverride;
    if (!ParseSprinterField(json, sprinterOverride, callback))
        return;
    std::function<void(CONFIG&)> profiOverride;
    if (!ParseProfiField(json, profiOverride, callback))
        return;
    std::function<void(CONFIG&)> slotsOverride;
    if (!ParseSlotsField(json, slotsOverride, callback))
        return;
    const std::function<void(CONFIG&)> createOverride =
        Combine(Combine(Combine(RamPowerOnOverride(ramPowerOn), sprinterOverride), profiOverride), slotsOverride);

    // ZX-Poly: "zxpoly": true or {"file": "<.zxp | .prom | disk image>"}
    const bool zxpoly = json && json->isMember("zxpoly") &&
                        ((*json)["zxpoly"].isBool() ? (*json)["zxpoly"].asBool() : (*json)["zxpoly"].isObject());
    const std::string zxpolyFile = zxpoly && (*json)["zxpoly"].isObject() ? (*json)["zxpoly"]["file"].asString() : "";

    // "select": false leaves the selection alone: the front end does not adopt
    // the new machine (automation farms, scripted analysis runs)
    const bool select = !(json && json->isMember("select") && (*json)["select"].isBool() && !(*json)["select"].asBool());

    try
    {
        std::shared_ptr<Emulator> emulator;
        std::string createError;

        // Create emulator with specified parameters - strict: a requested
        // model that cannot be created on this build fails with 400 + reason
        // instead of falling back to a default machine
        if (zxpoly)
        {
            // Four synchronized instances; the master is the machine returned
            emulator = manager->CreateZXPolyMachine(symbolicId, modelName.empty() ? "PENTAGON" : modelName,
                                                    zxpolyFile, &createError, createOverride);
        }
        else if (!modelName.empty() && ramSize > 0)
        {
            emulator = manager->CreateEmulatorWithModelAndRAM(symbolicId, modelName, ramSize,
                                                              LoggerLevel::LogWarning, &createError,
                                                              createOverride);
        }
        else if (!modelName.empty())
        {
            emulator = manager->CreateEmulatorWithModel(symbolicId, modelName, LoggerLevel::LogWarning, &createError,
                                                        createOverride);
        }
        else
        {
            emulator = manager->CreateEmulator(symbolicId, LoggerLevel::LogWarning, createOverride);
        }

        if (!emulator)
        {
            Json::Value error;
            error["error"] = "Bad Request";
            error["message"] = !createError.empty() ? createError : std::string("Emulator initialization failed");
            if (!modelName.empty())
            {
                error["requested_model"] = modelName;
            }
            error["available_models_endpoint"] = "/api/v1/emulator/models";

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        // Start the emulator
        std::string emulatorId = emulator->GetId();
        bool started = manager->StartEmulatorAsync(emulatorId);

        // Explicitly select this emulator since it was explicitly started via WebAPI
        // (unless the caller asked not to)
        if (started && select)
        {
            manager->SetSelectedEmulatorId(emulatorId);
        }

        Json::Value ret;
        ret["id"] = emulatorId;
        ret["symbolic_id"] = emulator->GetSymbolicId();
        ret["state"] = stateToString(emulator->GetState());
        ret["started"] = started;
        ret["message"] = started ? "Emulator created and started" : "Emulator created but failed to start";
        if (ZXPolyGroup* group = manager->GetZXPolyGroup(emulatorId))
            ret["zxpoly"] = ZXPolyStatusJson(group->GetStatus());

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(HttpStatusCode::k201Created);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Failed to create and start emulator";
        error["message"] = e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/start
/// @brief Start an existing emulator
void EmulatorAPI::startExistingEmulator(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    try
    {
        bool success = manager->StartEmulatorAsync(id);

        // Explicitly select this emulator since it was explicitly started via WebAPI
        if (success)
        {
            manager->SetSelectedEmulatorId(id);
        }

        Json::Value ret;
        ret["status"] = success ? "success" : "error";
        ret["message"] = success ? "Emulator started" : "Failed to start emulator (already running or error)";
        ret["emulator_id"] = id;

        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            ret["state"] = stateToString(emulator->GetState());
        }

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/stop
/// @brief Stop an emulator
void EmulatorAPI::stopEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    try
    {
        bool success = manager->StopEmulator(id);

        Json::Value ret;
        ret["status"] = success ? "success" : "error";
        ret["message"] = success ? "Emulator stopped" : "Failed to stop emulator (not running or error)";
        ret["emulator_id"] = id;

        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            ret["state"] = stateToString(emulator->GetState());
        }

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/pause
/// @brief Pause an emulator
void EmulatorAPI::pauseEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    try
    {
        bool success = manager->PauseEmulator(id);

        Json::Value ret;
        ret["status"] = success ? "success" : "error";
        ret["message"] = success ? "Emulator paused" : "Failed to pause emulator (not running or error)";
        ret["emulator_id"] = id;

        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            ret["state"] = stateToString(emulator->GetState());
        }

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/resume
/// @brief Resume an emulator
void EmulatorAPI::resumeEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    // Check run-control claim (GDB TDD §3.3 / 1A.7.2)
    auto emulator = manager->GetEmulator(id);
    if (emulator)
    {
        auto* ctx = emulator->GetContext();
        if (ctx && ctx->IsRunControlClaimed())
        {
            auto state = ctx->GetRunControlState();
            Json::Value error;
            error["error"] = "Run-control held";
            error["message"] = "Run-control held by " + state.surfaceLabel + ". Use that surface to resume.";
            error["emulator_id"] = id;

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k409Conflict);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
    }

    try
    {
        bool success = manager->ResumeEmulator(id);

        Json::Value ret;
        ret["status"] = success ? "success" : "error";
        ret["message"] = success ? "Emulator resumed" : "Failed to resume emulator (not paused or error)";
        ret["emulator_id"] = id;

        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            ret["state"] = stateToString(emulator->GetState());
        }

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/reset
/// @brief Reset an emulator
void EmulatorAPI::resetEmulator(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    try
    {
        bool success = manager->ResetEmulator(id);

        Json::Value ret;
        ret["status"] = success ? "success" : "error";
        ret["message"] = success ? "Emulator reset" : "Failed to reset emulator";
        ret["emulator_id"] = id;

        auto emulator = manager->GetEmulator(id);
        if (emulator)
        {
            ret["state"] = stateToString(emulator->GetState());
        }

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief POST /api/v1/emulator/{id}/nmi
/// @brief Pulse the Z80 NMI line (accepted at the next instruction boundary)
/// @param body Optional JSON: {"magic": true|false}. true = Scorpion MNI
///        "magic button" - the Shadow Monitor is paged into #0000 before the
///        NMI so the handler at #0066 executes monitor code. Non-Scorpion models
///        fall back to a plain NMI regardless of the flag.
void EmulatorAPI::requestNmi(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    try
    {
        bool magic = false;
        auto json = req->getJsonObject();
        if (json && json->isMember("magic") && json->get("magic", false).isBool())
            magic = json->get("magic", false).asBool();

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

        if (magic)
            emulator->RequestMNI();
        else
            emulator->RequestNMI();

        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = magic ? "MNI requested (magic button)" : "NMI requested";
        ret["emulator_id"] = id;
        ret["magic"] = magic;

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

namespace
{
/// {"switches": [{"name": "turbo", "on": false}]}: the machine's switches (empty when it has none)
Json::Value SwitchesJson(Emulator& emulator, const std::string& id)
{
    Json::Value ret;
    ret["emulator_id"] = id;
    ret["switches"] = Json::Value(Json::arrayValue);
    for (FrontPanelSwitch sw : {FrontPanelSwitch::Turbo, FrontPanelSwitch::Cpm})
    {
        const int value = emulator.GetFrontPanelSwitch(sw);
        if (value < 0)
            continue;
        Json::Value entry;
        entry["name"] = FrontPanelSwitchName(sw);
        entry["on"] = value != 0;
        ret["switches"].append(entry);
    }
    return ret;
}

void SendSwitchError(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode code, const std::string& error,
                     const std::string& message)
{
    Json::Value json;
    json["error"] = error;
    json["message"] = message;
    auto resp = HttpResponse::newHttpJsonResponse(json);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}
}  // namespace

/// @brief GET /api/v1/emulator/{id}/switches
/// @brief The machine's front-panel switches and their positions
void EmulatorAPI::getSwitches([[maybe_unused]] const HttpRequestPtr& req,
                              std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        SendSwitchError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator with specified ID not found");
        return;
    }
    auto resp = HttpResponse::newHttpJsonResponse(SwitchesJson(*emulator, id));
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/switches
/// @param body {"name": "turbo", "on": true|false}. The flip is recorded by TTD like a key
void EmulatorAPI::setSwitch(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        SendSwitchError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator with specified ID not found");
        return;
    }
    auto json = req->getJsonObject();
    if (!json || !(*json)["name"].isString() || !(*json)["on"].isBool())
    {
        SendSwitchError(callback, HttpStatusCode::k400BadRequest, "Bad Request",
                        "Body must be {\"name\": \"turbo\" | \"cpm\", \"on\": true|false}");
        return;
    }
    FrontPanelSwitch sw;
    const std::string name = (*json)["name"].asString();
    if (!ParseFrontPanelSwitch(name, sw))
    {
        SendSwitchError(callback, HttpStatusCode::k400BadRequest, "Bad Request", "Unknown switch '" + name + "'");
        return;
    }
    if (!emulator->SetFrontPanelSwitch(sw, (*json)["on"].asBool()))
    {
        SendSwitchError(callback, HttpStatusCode::k400BadRequest, "Bad Request",
                        "This machine has no " + name + " switch");
        return;
    }
    auto resp = HttpResponse::newHttpJsonResponse(SwitchesJson(*emulator, id));
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/model
/// @brief Switch emulator to a different machine model
/// @details This stops the current emulator, destroys it, creates a new one with the specified model, and starts it.
///          The new emulator will have a different ID than the original.
/// @param id The ID of the emulator to switch
/// @param body JSON body with "model" field containing the model short name (e.g., "PENTAGON", "48K", "128K", "SCORPION")
///             and optional "ram_size" field for RAM size in KB
void EmulatorAPI::switchModel(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();

    if (!manager->HasEmulator(id))
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

    // Parse request body
    auto body = req->getJsonObject();
    if (!body || !body->isMember("model"))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Request body must contain 'model' field with model short name";
        error["available_models_endpoint"] = "/api/v1/emulator/models";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string modelName = (*body)["model"].asString();
    uint32_t ramSize = 0;
    if (body->isMember("ram_size"))
    {
        ramSize = (*body)["ram_size"].asUInt();
    }
    // Absent: the new machine keeps this machine's power-on RAM mode
    std::optional<RamPowerOn> ramPowerOn;
    if (!ParseRamPowerOnField(body, ramPowerOn, callback))
        return;

    // Validate BEFORE touching the current instance: a failed model switch
    // must leave the caller's emulator untouched (the old flow removed the
    // instance first and a bad model then left the caller with nothing).
    // These checks mirror the ones inside EmulatorManager create paths.
    const TMemModel* requestedModel = Config::FindModelByShortName(modelName);
    if (!requestedModel)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "unknown model '" + modelName + "'";
        error["requested_model"] = modelName;
        error["available_models_endpoint"] = "/api/v1/emulator/models";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (ramSize > 0 && (ramSize & requestedModel->AvailRAMs) == 0)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "RAM size " + std::to_string(ramSize) + "KB is not supported by model '" + modelName +
                           "' (see available_ram_sizes_kb in /api/v1/emulator/models)";
        error["requested_model"] = modelName;
        error["available_models_endpoint"] = "/api/v1/emulator/models";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    if (!Config::IsModelCreatable(*requestedModel))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "model '" + modelName +
                           "' is not creatable on this build (port decoder or config folder missing)";
        error["requested_model"] = modelName;
        error["available_models_endpoint"] = "/api/v1/emulator/models";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // What happens to unsaved writes on media the new model has no slot for
    StrandedMedia stranded = StrandedMedia::Refuse;
    if (body->isMember("stranded") && !ModelSwitch::ParseStranded((*body)["stranded"].asString(), stranded))
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "stranded '" + (*body)["stranded"].asString() + "': expected refuse, save, discard or keep";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    try
    {
        // The media follow the switch (docs/features/media.md, "Model switch")
        ModelSwitchRequest request;
        request.emulatorId = id;
        request.model = modelName;
        request.ramKb = ramSize;
        request.stranded = stranded;
        request.ramPowerOn = ramPowerOn;
        const ModelSwitchResult switched = ModelSwitch::Run(request);

        auto mediaJson = [&switched]() {
            Json::Value media;
            media["attached"] = Json::arrayValue;
            media["detached"] = Json::arrayValue;
            media["closed"] = Json::arrayValue;
            for (const std::string& slot : switched.media.attached)
                media["attached"].append(slot);
            for (const std::string& slot : switched.media.detached)
                media["detached"].append(slot);
            for (const std::string& slot : switched.media.closed)
                media["closed"].append(slot);
            return media;
        };

        if (!switched.result.Ok())
        {
            Json::Value error;
            error["error"] = switched.result.error == MediaError::Dirty ? "Conflict" : "Failed to switch model";
            error["code"] = MediaErrorCode(switched.result.error);
            error["message"] = switched.result.message;
            error["requested_model"] = modelName;
            error["stranded"] = Json::arrayValue;
            for (const SlotInfo& info : switched.stranded)
            {
                Json::Value medium;
                medium["slot"] = info.descriptor.id;
                medium["source"] = info.source;
                medium["changes"] = info.changes;
                error["stranded"].append(medium);
            }

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(switched.result.error == MediaError::Dirty ? HttpStatusCode::k409Conflict
                                                                            : HttpStatusCode::k400BadRequest);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }

        std::shared_ptr<Emulator> newEmulator = switched.emulator;
        // Start() blocks in MainLoop::Run() until Stop() is requested - calling
        // it directly on the HTTP thread never returns a response. Every other
        // lifecycle endpoint starts an emulator via StartEmulatorAsync (see
        // create()/start() above); do the same here.
        manager->StartEmulatorAsync(newEmulator->GetId());

        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Model switched successfully";
        ret["old_emulator_id"] = id;
        ret["new_emulator_id"] = newEmulator->GetId();
        ret["state"] = stateToString(newEmulator->GetState());
        ret["media"] = mediaJson();
        // The cards went along (ZX-bus slots R-OP-9): kept, moved behind an adapter, dropped with the reason
        ret["slots"] = StateNodeToJson(SlotControl::CarryValue(switched.slotCarry));
        ret["report"] = Json::arrayValue;
        for (const std::string& line : switched.result.report)
            ret["report"].append(line);
        AddIdentityFields(ret, *newEmulator);

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(HttpStatusCode::k200OK);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

/// @brief Helper method to handle emulator actions with common error handling
void EmulatorAPI::handleEmulatorAction(const HttpRequestPtr& req,
                                       std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id,
                                       std::function<std::string(std::shared_ptr<Emulator>)> action) const
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

    try
    {
        std::string message = action(emulator);

        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = message;
        ret["emulator_id"] = id;
        ret["state"] = stateToString(emulator->GetState());

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        Json::Value error;
        error["error"] = "Operation failed";
        error["message"] = e.what();
        error["emulator_id"] = id;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k500InternalServerError);
        addCorsHeaders(resp);
        callback(resp);
    }
}

} // namespace v1
} // namespace api
