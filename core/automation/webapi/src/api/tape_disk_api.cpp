// WebAPI Tape and Disk Control Implementation
// Generated: 2026-01-08

#include "../emulator_api.h"

#include <base/featuremanager.h>
#include <drogon/HttpResponse.h>
#include <drogon/utils/Utilities.h>
#include <cstdint>
#include <cstdio>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/tape/tapecatalog.h>
#include <emulator/io/tape/tape.h>
#include <emulator/io/fdc/wd1793.h>
#include <emulator/io/fdc/diskimage.h>
#include <emulator/io/fdc/fdd.h>
#include <common/stringhelper.h>
#include <json/json.h>

using namespace drogon;
using namespace api::v1;

// Helper to parse drive parameter (A-D or 0-3)
static uint8_t parseDriveParameter(const std::string& driveStr, std::string& errorMsg)
{
    if (driveStr.empty()) {
        errorMsg = "Missing drive parameter";
        return 0xFF;
    }
    if (driveStr == "A" || driveStr == "a" || driveStr == "0") return 0;
    if (driveStr == "B" || driveStr == "b" || driveStr == "1") return 1;
    if (driveStr == "C" || driveStr == "c" || driveStr == "2") return 2;
    if (driveStr == "D" || driveStr == "d" || driveStr == "3") return 3;
    
    errorMsg = "Invalid drive: " + driveStr + " (use A-D or 0-3)";
    return 0xFF;
}

namespace api
{
namespace v1
{

// Helper to add CORS headers (already exists in emulator_api.cpp, declaring here for consistency)
extern void addCorsHeaders(HttpResponsePtr& resp);

/// region <Tape snapshot helpers (tape-manager design §7.2)>

static const char* tapePlaybackStateName(TapePlaybackState state)
{
    switch (state)
    {
        case TapePlaybackState::Idle:
            return "idle";
        case TapePlaybackState::Playing:
            return "playing";
        case TapePlaybackState::Paused:
            return "paused";
        case TapePlaybackState::Ended:
            return "ended";
        default:
            break;
    }
    return "unknown";
}

/// jsoncpp-safe size_t -> JSON number
static Json::Value jsonSize(size_t value)
{
    return Json::Value(static_cast<Json::UInt64>(value));
}

/// One catalog block as JSON (design §7.2 `blocks[]` element)
static Json::Value blockDescriptorJson(const TapeBlockDescriptor& descriptor, const TapeFastLoadPlan& plan)
{
    Json::Value block;
    block["index"] = jsonSize(descriptor.index);
    block["kind"] = getTapeBlockKindName(descriptor.kind);

    if (descriptor.kind == TapeBlockKindEnum::Header || descriptor.kind == TapeBlockKindEnum::Data ||
        descriptor.kind == TapeBlockKindEnum::Custom)
    {
        block["headerless"] = descriptor.headerless;
    }

    if (descriptor.headerValid)
    {
        block["name"] = descriptor.name;
        block["type"] = getTapeBlockTypeName(descriptor.headerType);
        block["declared_length"] = descriptor.declaredLength;
        block["param1"] = descriptor.param1;
        block["param2"] = descriptor.param2;
    }

    if (descriptor.pairedDataIndex != SIZE_MAX)
        block["paired_data_index"] = jsonSize(descriptor.pairedDataIndex);
    if (descriptor.pairedHeaderIndex != SIZE_MAX)
        block["paired_header_index"] = jsonSize(descriptor.pairedHeaderIndex);
    if (!descriptor.groupLabel.empty())
        block["group"] = descriptor.groupLabel;

    Json::Value& speed = block["speed"];
    speed["profile"] = getTapeSpeedProfileName(descriptor.timing.profile);
    if (descriptor.baudEstimate > 0)
        speed["baud"] = descriptor.baudEstimate;
    if (descriptor.timing.profile == TapeSpeedProfileEnum::Custom)
    {
        // Full turbo timing — exactly what generateBitstream() would consume
        speed["pilot_pulses"] = descriptor.timing.pilotPulses;
        speed["pilot_half"] = descriptor.timing.pilotHalfPeriod;
        speed["sync1"] = descriptor.timing.sync1;
        speed["sync2"] = descriptor.timing.sync2;
        speed["zero_half"] = descriptor.timing.zeroHalfPeriod;
        speed["one_half"] = descriptor.timing.oneHalfPeriod;
        speed["pause_ms"] = descriptor.timing.pauseMs;
        speed["bits_in_last_byte"] = descriptor.timing.bitsInLastByte;
    }

    block["checksum_valid"] = descriptor.checksumValid;
    block["checksum_applicable"] = descriptor.rawSize > 0;
    block["seconds"] = descriptor.estimatedSeconds;
    if (descriptor.rawSize > 0)
        block["raw_size"] = jsonSize(descriptor.rawSize);
    block["playable"] = descriptor.playable;

    if (descriptor.kind != TapeBlockKindEnum::Control && plan.perBlock.size() > descriptor.index)
    {
        block["fast_load"] = plan.perBlock[descriptor.index] == FastLoadRejectEnum::None
                                 ? "yes"
                                 : getFastLoadRejectName(plan.perBlock[descriptor.index]);
    }

    return block;
}

/// Full GET /tape snapshot (design §7.2). Calls EnsureImageLoaded()
/// (idempotent, path-keyed) — handlers invoke this under the pause bracket.
static Json::Value buildTapeSnapshot(EmulatorContext* context)
{
    Json::Value ret;
    Tape* tape = context->pTape;
    const std::string& path = context->coreState.tapeFilePath;

    const bool loaded = !path.empty() && tape->EnsureImageLoaded();
    ret["status"] = loaded ? "loaded" : (path.empty() ? "empty" : "error");
    ret["file"] = path;
    if (!loaded)
    {
        ret["state"] = "idle";
        return ret;
    }

    const std::vector<TapeBlockDescriptor>& catalog = tape->GetBlockCatalog();
    const TapeFastLoadPlan& plan = tape->GetFastLoadPlan();

    ret["format"] = tape->GetLoadedFormatId();
    ret["state"] = tapePlaybackStateName(tape->GetPlaybackState());

    std::optional<TapePosition> position = tape->GetPosition();
    if (position.has_value())
    {
        Json::Value& pos = ret["position"];
        pos["block"] = jsonSize(position->blockIndex);
        pos["pulse"] = jsonSize(position->pulseIndex);
        pos["seconds_into_block"] = position->secondsIntoBlock;
        pos["block_total_seconds"] = position->blockTotalSeconds;
    }

    ret["cursor"] = jsonSize(tape->GetConsumptionCursor());
    ret["block_count"] = jsonSize(catalog.size());
    ret["total_seconds"] = plan.totalSeconds;

    FeatureManager* featureManager = context->pFeatureManager;
    ret["fast_tape"] = featureManager && featureManager->isEnabled(Features::kFastTape);

    Json::Value& fastLoad = ret["fast_load"];
    fastLoad["verdict"] = getFastLoadVerdictName(plan.verdict);
    fastLoad["horizon"] = jsonSize(plan.stickinessHorizon);
    fastLoad["eligible_blocks"] = jsonSize(plan.eligibleBlocks);
    fastLoad["accelerated_seconds"] = plan.acceleratedSeconds;
    fastLoad["total_seconds"] = plan.totalSeconds;
    if (plan.firstRejectIndex != SIZE_MAX)
    {
        fastLoad["first_reject"]["index"] = jsonSize(plan.firstRejectIndex);
        fastLoad["first_reject"]["reason"] = getFastLoadRejectName(plan.firstRejectReason);
    }
    fastLoad["advisory"] = true;  // the plan never gates the runtime trap (§5.8)
    fastLoad["summary"] = plan.summary;

    Json::Value& blocks = ret["blocks"];
    blocks = Json::arrayValue;
    for (const TapeBlockDescriptor& descriptor : catalog)
    {
        blocks.append(blockDescriptorJson(descriptor, plan));
    }

    return ret;
}

/// endregion </Tape snapshot helpers>

/// @brief POST /api/v1/emulator/:id/tape/load
/// @brief Load tape image
void EmulatorAPI::loadTape(const HttpRequestPtr& req,
                           std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator with specified ID not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto json = req->getJsonObject();
    if (!json || !json->isMember("path")) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'path' parameter in request body";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::string path = (*json)["path"].asString();
    bool success = emulator->LoadTape(path);
    
    Json::Value ret;
    ret["status"] = success ? "success" : "error";
    ret["message"] = success ? "Tape loaded successfully" : "Failed to load tape (check logs for details)";
    ret["path"] = path;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/tape/eject
void EmulatorAPI::ejectTape(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Thread-safe eject
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    context->pTape->stopTape();
    context->coreState.tapeFilePath.clear();
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Tape ejected";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/tape/play
void EmulatorAPI::playTape(const HttpRequestPtr& req,
                           std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Thread-safe play
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Parse-once (idempotent); paused -> resume the frozen position IN PLACE
    // (FR-6), otherwise start at the consumption cursor
    bool played = context->pTape->EnsureImageLoaded();
    bool resumed = false;
    if (played) {
        if (context->pTape->GetPlaybackState() == TapePlaybackState::Paused) {
            context->pTape->ResumePlaybackFromPause();
            resumed = true;
        } else {
            context->pTape->StartPlaybackAtCursor();
        }
    }
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    Json::Value ret;
    ret["status"] = played ? "success" : "error";
    ret["message"] = !played    ? "Tape image has no loadable blocks"
                     : resumed ? "Tape playback resumed (in place)"
                               : "Tape playback started";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(played ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/tape/stop
void EmulatorAPI::stopTape(const HttpRequestPtr& req,
                           std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Thread-safe stop
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    context->pTape->stopTape();
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Tape playback stopped";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/tape/rewind
void EmulatorAPI::rewindTape(const HttpRequestPtr& req,
                             std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Thread-safe rewind
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Rewind keeps the image and catalog (FR-5) — unlike stop/eject
    context->pTape->EnsureImageLoaded();
    context->pTape->RewindToStart();
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Tape rewound to beginning (image kept)";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/tape
/// Full tape snapshot: state, position, cursor, fast-load plan, catalog (design §7.2)
void EmulatorAPI::getTape(const HttpRequestPtr& req,
                          std::function<void(const HttpResponsePtr&)>&& callback,
                          const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Thread-safe snapshot: the first EnsureImageLoaded() mutates tape state
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    Json::Value ret = buildTapeSnapshot(context);
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/tape/info — alias of GET /tape (kept for compatibility)
void EmulatorAPI::getTapeInfo(const HttpRequestPtr& req,
                              std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    getTape(req, std::move(callback), id);
}

/// @brief GET /api/v1/emulator/:id/tape/blocks/:index
/// One catalog descriptor plus a 64-byte hex preview of the raw payload
void EmulatorAPI::getTapeBlock(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id,
                               const std::string& index) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    size_t blockIndex = 0;
    try {
        blockIndex = static_cast<size_t>(std::stoul(index));
    } catch (const std::exception&) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Invalid block index: " + index;
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    Json::Value ret;
    HttpStatusCode code = HttpStatusCode::k200OK;
    
    if (!context->pTape->EnsureImageLoaded() || blockIndex >= context->pTape->GetBlockCatalog().size() ||
        blockIndex >= context->pTape->GetBlocks().size()) {
        ret["error"] = "Not Found";
        ret["message"] = "Block index " + std::to_string(blockIndex) + " out of range";
        code = HttpStatusCode::k404NotFound;
    } else {
        ret = blockDescriptorJson(context->pTape->GetBlockCatalog()[blockIndex], context->pTape->GetFastLoadPlan());
        
        // First 64 bytes of the raw payload (flag + payload + checksum) as hex
        const std::vector<uint8_t>& data = context->pTape->GetBlocks()[blockIndex].data;
        const size_t previewLength = data.size() < 64 ? data.size() : 64;
        std::string hex;
        hex.reserve(previewLength * 2);
        char byte[3];
        for (size_t i = 0; i < previewLength; i++) {
            snprintf(byte, sizeof(byte), "%02x", data[i]);
            hex += byte;
        }
        ret["payload_bytes"] = jsonSize(data.size());
        ret["preview_hex"] = hex;
    }
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/tape/seek — body { "block": 9 }
void EmulatorAPI::seekTape(const HttpRequestPtr& req,
                           std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto json = req->getJsonObject();
    if (!json || !json->isMember("block")) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'block' parameter in request body";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    const size_t blockIndex = static_cast<size_t>((*json)["block"].asUInt64());
    
    auto context = emulator->GetContext();
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    Json::Value ret;
    HttpStatusCode code = HttpStatusCode::k200OK;
    
    if (!context->pTape->EnsureImageLoaded() || !context->pTape->SeekToBlock(blockIndex)) {
        ret["status"] = "error";
        ret["message"] = "Block index " + std::to_string(blockIndex) + " out of range";
        code = HttpStatusCode::k400BadRequest;
    } else {
        // Resulting position — the leading fields of the full snapshot
        Json::Value snapshot = buildTapeSnapshot(context);
        ret["status"] = "success";
        ret["message"] = "Seeked to block " + std::to_string(blockIndex);
        ret["state"] = snapshot["state"];
        ret["position"] = snapshot["position"];
        ret["cursor"] = snapshot["cursor"];
    }
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/tape/pause — manual pause; play resumes in place
void EmulatorAPI::pauseTape(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || !context->pTape) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Tape subsystem not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    Json::Value ret;
    HttpStatusCode code = HttpStatusCode::k200OK;
    const TapePlaybackState state = context->pTape->GetPlaybackState();
    
    if (state == TapePlaybackState::Paused) {
        ret["status"] = "success";
        ret["message"] = "Tape already paused";
    } else if (state != TapePlaybackState::Playing) {
        ret["status"] = "error";
        ret["message"] = "Tape is not playing";
        code = HttpStatusCode::k400BadRequest;
    } else {
        context->pTape->pausePlayback();
        ret["status"] = "success";
        ret["message"] = "Tape paused (play resumes in place)";
    }
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/disk/:drive/insert
void EmulatorAPI::insertDisk(const HttpRequestPtr& req,
                             std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id,
                             const std::string& drive) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Parse drive parameter
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto json = req->getJsonObject();
    if (!json || !json->isMember("path")) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Missing 'path' parameter in request body";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::string path = (*json)["path"].asString();
    bool success = emulator->LoadDisk(path);
    
    Json::Value ret;
    ret["status"] = success ? "success" : "error";
    ret["message"] = success ? "Disk inserted successfully" : "Failed to insert disk (check logs for details)";
    ret["path"] = path;
    ret["drive"] = drive;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/disk/:drive/create - Create blank disk
void EmulatorAPI::createDisk(const HttpRequestPtr& req,
                             std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id,
                             const std::string& drive) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || !context->coreState.diskDrives[driveNum]) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Disk drive not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Parse optional parameters from JSON body
    uint8_t cylinders = 80;
    uint8_t sides = 2;
    
    auto json = req->getJsonObject();
    if (json) {
        if (json->isMember("cylinders")) {
            int c = (*json)["cylinders"].asInt();
            if (c == 40 || c == 80) {
                cylinders = static_cast<uint8_t>(c);
            } else {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "cylinders must be 40 or 80";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
        }
        if (json->isMember("sides")) {
            int s = (*json)["sides"].asInt();
            if (s == 1 || s == 2) {
                sides = static_cast<uint8_t>(s);
            } else {
                Json::Value error;
                error["error"] = "Bad Request";
                error["message"] = "sides must be 1 or 2";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(HttpStatusCode::k400BadRequest);
                addCorsHeaders(resp);
                callback(resp);
                return;
            }
        }
    }
    
    // Create blank disk image
    DiskImage* diskImage = new DiskImage(cylinders, sides);
    
    // Insert into drive
    FDD* fdd = context->coreState.diskDrives[driveNum];
    fdd->insertDisk(diskImage);
    
    // Update path tracking for API queries
    context->coreState.diskFilePaths[driveNum] = "<blank>";
    
    Json::Value ret;
    ret["success"] = true;
    ret["drive"] = drive;
    ret["cylinders"] = cylinders;
    ret["sides"] = sides;
    ret["message"] = "Blank disk created and inserted";
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/disk/:drive/eject
void EmulatorAPI::ejectDisk(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback,
                            const std::string& id,
                            const std::string& drive) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Parse drive parameter
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || driveNum >= 4) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Disk drive not available";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Thread-safe eject
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning) {
        emulator->Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    if (context->pBetaDisk) {
        context->pBetaDisk->ejectDisk();
    }
    if (context->coreState.diskDrives[driveNum]) {
        context->coreState.diskDrives[driveNum]->ejectDisk();
    }
    context->coreState.diskFilePaths[driveNum].clear();
    
    if (wasRunning) {
        emulator->Resume();
    }
    
    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Disk ejected";
    ret["drive"] = drive;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/disk/:drive/info
void EmulatorAPI::getDiskInfo(const HttpRequestPtr& req,
                              std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id,
                              const std::string& drive) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Parse drive parameter
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    Json::Value ret;
    
    if (!context || driveNum >= 4) {
        ret["status"] = "unavailable";
        ret["message"] = "Disk drive not available";
    } else {
        std::string diskPath = context->coreState.diskFilePaths[driveNum];
        bool isInserted = !diskPath.empty();
        
        ret["status"] = isInserted ? "inserted" : "empty";
        ret["file"] = diskPath;
        ret["drive"] = drive;
        
        if (context->coreState.diskDrives[driveNum]) {
            ret["write_protected"] = context->coreState.diskDrives[driveNum]->isWriteProtect();
        }
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

// ============================================================================
// Disk Inspection Endpoints
// ============================================================================

/// @brief GET /api/v1/emulator/:id/disk - List all drives
void EmulatorAPI::getDiskDrives(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    Json::Value ret;
    ret["drives"] = Json::arrayValue;
    
    const char* driveLetters[] = {"A", "B", "C", "D"};
    int mountedCount = 0;
    int lastMounted = -1;
    
    for (int i = 0; i < 4; i++) {
        Json::Value driveInfo;
        driveInfo["id"] = i;
        driveInfo["letter"] = driveLetters[i];
        
        if (context && !context->coreState.diskFilePaths[i].empty()) {
            driveInfo["mounted"] = true;
            driveInfo["file"] = context->coreState.diskFilePaths[i];
            mountedCount++;
            lastMounted = i;
            
            if (context->coreState.diskDrives[i]) {
                auto* fdd = context->coreState.diskDrives[i];
                driveInfo["write_protected"] = fdd->isWriteProtect();
                
                // Get disk image info if available
                auto* diskImage = fdd->getDiskImage();
                if (diskImage) {
                    driveInfo["cylinders"] = diskImage->getCylinders();
                    driveInfo["sides"] = diskImage->getSides();
                }
            }
        } else {
            driveInfo["mounted"] = false;
        }
        
        ret["drives"].append(driveInfo);
    }
    
    // FDC state
    if (context && context->pBetaDisk) {
        Json::Value fdcState;
        fdcState["track_reg"] = context->pBetaDisk->getTrackRegister();
        fdcState["sector_reg"] = context->pBetaDisk->getSectorRegister();
        // Note: status register would require public accessor in WD1793
        ret["fdc_state"] = fdcState;
    }
    
    // Auto-selection info
    ret["mounted_count"] = mountedCount;
    if (mountedCount == 1) {
        ret["auto_selected"] = driveLetters[lastMounted];
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/disk/:drive - Get drive info
void EmulatorAPI::getDiskDrive(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id,
                               const std::string& drive) const
{
    // Delegate to getDiskInfo for now (similar functionality)
    getDiskInfo(req, std::move(callback), id, drive);
}

/// @brief GET /api/v1/emulator/:id/disk/:drive/sector/:cyl/:side/:sec - Read logical sector
void EmulatorAPI::getDiskSector(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id,
                                const std::string& drive,
                                const std::string& cylStr,
                                const std::string& sideStr,
                                const std::string& secStr) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Parse parameters
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    int cylinder = std::stoi(cylStr);
    int side = std::stoi(sideStr);
    int sector = std::stoi(secStr);
    
    auto context = emulator->GetContext();
    if (!context || !context->coreState.diskDrives[driveNum]) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Disk drive not available or no disk inserted";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* fdd = context->coreState.diskDrives[driveNum];
    auto* diskImage = fdd->getDiskImage();
    if (!diskImage) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "No disk image loaded";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Get track
    auto* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
    if (!track) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Track not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // Convert to 0-indexed (TR-DOS uses 1-based sectors, but custom disks may vary)
    int sectorIdx = sector - 1;
    if (sectorIdx < 0) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = "Sector must be >= 1";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* rawSector = track->getSector(sectorIdx);
    if (!rawSector) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Sector not found (may need reindex)";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    Json::Value ret;
    ret["drive"] = drive;
    ret["cylinder"] = cylinder;
    ret["side"] = side;
    ret["sector"] = sector;
    
    // Address Mark info
    Json::Value addrMark;
    addrMark["id_mark"] = StringHelper::ToHex(rawSector->address_record.id_address_mark);
    addrMark["cylinder"] = rawSector->address_record.cylinder;
    addrMark["head"] = rawSector->address_record.head;
    addrMark["sector"] = rawSector->address_record.sector;
    addrMark["sector_size"] = rawSector->address_record.sector_size;
    addrMark["crc"] = StringHelper::ToHex(rawSector->address_record.id_crc);
    addrMark["crc_valid"] = rawSector->address_record.isCRCValid();
    ret["address_mark"] = addrMark;
    
    // Data info
    ret["data_mark"] = StringHelper::ToHex(rawSector->data_address_mark);
    ret["data_crc"] = StringHelper::ToHex(rawSector->data_crc);
    ret["data_crc_valid"] = rawSector->isDataCRCValid();
    
    // Hex dump of first 64 bytes
    std::string hexDump;
    for (int i = 0; i < 64 && i < 256; i++) {
        if (i > 0 && i % 16 == 0) hexDump += "\n";
        else if (i > 0) hexDump += " ";
        hexDump += StringHelper::ToHex(rawSector->data[i]);
    }
    ret["data_preview"] = hexDump;
    
    // Base64 encode full data
    std::string data64 = drogon::utils::base64Encode(rawSector->data, 256);
    ret["data_base64"] = data64;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/disk/:drive/sector/:cyl/:side/:sec/raw - Read raw sector bytes
void EmulatorAPI::getDiskSectorRaw(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id,
                                   const std::string& drive,
                                   const std::string& cylStr,
                                   const std::string& sideStr,
                                   const std::string& secStr) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    int cylinder = std::stoi(cylStr);
    int side = std::stoi(sideStr);
    int sector = std::stoi(secStr);
    
    auto context = emulator->GetContext();
    if (!context || !context->coreState.diskDrives[driveNum]) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Disk drive not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* fdd = context->coreState.diskDrives[driveNum];
    auto* diskImage = fdd->getDiskImage();
    if (!diskImage) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "No disk image";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
    if (!track) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Track not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    int sectorIdx = sector - 1;
    auto* rawSector = track->getRawSector(sectorIdx);
    if (!rawSector) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Sector not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    Json::Value ret;
    ret["drive"] = drive;
    ret["cylinder"] = cylinder;
    ret["side"] = side;
    ret["sector"] = sector;
    ret["raw_size"] = static_cast<int>(sizeof(DiskImage::RawSectorBytes));
    
    // Full raw sector as base64
    std::string raw64 = drogon::utils::base64Encode(
        reinterpret_cast<const unsigned char*>(rawSector), 
        sizeof(DiskImage::RawSectorBytes));
    ret["raw_base64"] = raw64;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/disk/:drive/track/:cyl/:side - Track summary
void EmulatorAPI::getDiskTrack(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id,
                               const std::string& drive,
                               const std::string& cylStr,
                               const std::string& sideStr) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    int cylinder = std::stoi(cylStr);
    int side = std::stoi(sideStr);
    
    auto context = emulator->GetContext();
    if (!context || !context->coreState.diskDrives[driveNum]) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Disk drive not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* fdd = context->coreState.diskDrives[driveNum];
    auto* diskImage = fdd->getDiskImage();
    if (!diskImage) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "No disk image";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
    if (!track) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Track not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    Json::Value ret;
    ret["drive"] = drive;
    ret["cylinder"] = cylinder;
    ret["side"] = side;
    ret["raw_size"] = 6250;
    ret["sectors"] = Json::arrayValue;
    
    for (int i = 0; i < 16; i++) {
        auto* rawSector = track->getSector(i);
        Json::Value sec;
        sec["index"] = i;
        sec["logical_number"] = i + 1;
        
        if (rawSector) {
            sec["id_cyl"] = rawSector->address_record.cylinder;
            sec["id_head"] = rawSector->address_record.head;
            sec["id_sector"] = rawSector->address_record.sector;
            sec["id_crc_valid"] = rawSector->address_record.isCRCValid();
            sec["data_crc_valid"] = rawSector->isDataCRCValid();
        } else {
            sec["error"] = "sector not indexed";
        }
        
        ret["sectors"].append(sec);
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/disk/:drive/track/:cyl/:side/raw - Raw track bytes
void EmulatorAPI::getDiskTrackRaw(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id,
                                  const std::string& drive,
                                  const std::string& cylStr,
                                  const std::string& sideStr) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    int cylinder = std::stoi(cylStr);
    int side = std::stoi(sideStr);
    
    auto context = emulator->GetContext();
    if (!context || !context->coreState.diskDrives[driveNum]) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Disk drive not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* fdd = context->coreState.diskDrives[driveNum];
    auto* diskImage = fdd->getDiskImage();
    if (!diskImage) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "No disk image";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
    if (!track) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Track not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    Json::Value ret;
    ret["drive"] = drive;
    ret["cylinder"] = cylinder;
    ret["side"] = side;
    ret["raw_size"] = 6250;
    
    // RawTrack is 6250 bytes (16 sectors * 388 bytes + 42 byte end gap)
    std::string raw64 = drogon::utils::base64Encode(
        reinterpret_cast<const unsigned char*>(track), 
        DiskImage::RawTrack::RAW_TRACK_SIZE);
    ret["raw_base64"] = raw64;
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/disk/:drive/image - Whole image dump
void EmulatorAPI::getDiskImage(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id,
                               const std::string& drive) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || !context->coreState.diskDrives[driveNum]) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Disk drive not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* fdd = context->coreState.diskDrives[driveNum];
    auto* diskImage = fdd->getDiskImage();
    if (!diskImage) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "No disk image";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    uint8_t cylinders = diskImage->getCylinders();
    uint8_t sides = diskImage->getSides();
    size_t totalTracks = cylinders * sides;
    size_t trackSize = DiskImage::RawTrack::RAW_TRACK_SIZE;
    size_t imageSize = totalTracks * trackSize;
    
    // Collect all track data
    std::vector<uint8_t> imageData;
    imageData.reserve(imageSize);
    
    for (uint8_t cyl = 0; cyl < cylinders; cyl++) {
        for (uint8_t side = 0; side < sides; side++) {
            auto* track = diskImage->getTrackForCylinderAndSide(cyl, side);
            if (track) {
                const uint8_t* trackData = reinterpret_cast<const uint8_t*>(track);
                imageData.insert(imageData.end(), trackData, trackData + trackSize);
            } else {
                // Fill with zeros if track doesn't exist
                imageData.insert(imageData.end(), trackSize, 0);
            }
        }
    }
    
    Json::Value ret;
    ret["drive"] = drive;
    ret["cylinders"] = cylinders;
    ret["sides"] = sides;
    ret["total_tracks"] = static_cast<int>(totalTracks);
    ret["track_size"] = static_cast<int>(trackSize);
    ret["image_size"] = static_cast<int>(imageData.size());
    ret["image_base64"] = drogon::utils::base64Encode(imageData.data(), imageData.size());
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/disk/:drive/sysinfo - TR-DOS system sector
void EmulatorAPI::getDiskSysinfo(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id,
                                 const std::string& drive) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || !context->coreState.diskDrives[driveNum]) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Disk drive not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* fdd = context->coreState.diskDrives[driveNum];
    auto* diskImage = fdd->getDiskImage();
    if (!diskImage) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "No disk image";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    // TR-DOS system info is in Track 0, Side 0, Sector 9 (logical index 8)
    auto* track = diskImage->getTrackForCylinderAndSide(0, 0);
    if (!track) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Track 0 not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* sector9 = track->getSector(8); // Index 8 = Sector 9
    if (!sector9) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "System sector (9) not found - may need reindex";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    uint8_t* data = sector9->data;
    
    Json::Value ret;
    ret["drive"] = drive;
    ret["dos_type"] = "TR-DOS";
    
    // Parse TR-DOS system sector fields
    // Offset $E1: First free sector (0-15)
    // Offset $E2: First free track (1-159)
    // Offset $E3: Disk type ($16=DS80, $17=DS40, $18=SS80, $19=SS40)
    // Offset $E4: File count
    // Offset $E5-$E6: Free sectors (16-bit LE)
    // Offset $E7: TR-DOS signature (must be $10)
    
    uint8_t firstFreeSector = data[0xE1];
    uint8_t firstFreeTrack = data[0xE2];
    uint8_t diskType = data[0xE3];
    uint8_t fileCount = data[0xE4];
    uint16_t freeSectors = data[0xE5] | (data[0xE6] << 8);
    uint8_t signature = data[0xE7];
    
    ret["disk_type"] = StringHelper::ToHex(diskType);
    
    // Decode disk type
    std::string diskTypeStr;
    switch (diskType) {
        case 0x16: diskTypeStr = "80T Double-Sided"; break;
        case 0x17: diskTypeStr = "40T Double-Sided"; break;
        case 0x18: diskTypeStr = "80T Single-Sided"; break;
        case 0x19: diskTypeStr = "40T Single-Sided"; break;
        default: diskTypeStr = "Unknown"; break;
    }
    ret["disk_type_decoded"] = diskTypeStr;
    
    // Extract label (bytes $F5-$FC)
    std::string label;
    for (int i = 0xF5; i <= 0xFC && i < 256; i++) {
        if (data[i] >= 32 && data[i] < 128) {
            label += static_cast<char>(data[i]);
        }
    }
    ret["label"] = label;
    
    ret["file_count"] = fileCount;
    ret["free_sectors"] = freeSectors;
    ret["first_free_track"] = firstFreeTrack;
    ret["first_free_sector"] = firstFreeSector;
    ret["trdos_signature"] = StringHelper::ToHex(signature);
    ret["signature_valid"] = (signature == 0x10);
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/disk/:drive/catalog - Disk catalog
void EmulatorAPI::getDiskCatalog(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id,
                                 const std::string& drive) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);
    
    if (!emulator) {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    std::string errorMsg;
    uint8_t driveNum = parseDriveParameter(drive, errorMsg);
    if (driveNum == 0xFF) {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = errorMsg;
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto context = emulator->GetContext();
    if (!context || !context->coreState.diskDrives[driveNum]) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "Disk drive not available";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    auto* fdd = context->coreState.diskDrives[driveNum];
    auto* diskImage = fdd->getDiskImage();
    if (!diskImage) {
        Json::Value error;
        error["error"] = "Not Available";
        error["message"] = "No disk image";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    Json::Value ret;
    ret["drive"] = drive;
    ret["dos_type"] = "TR-DOS";
    ret["files"] = Json::arrayValue;
    
    // TR-DOS catalog is in Track 0, Side 0, Sectors 1-8 (16 entries per sector)
    auto* track = diskImage->getTrackForCylinderAndSide(0, 0);
    if (!track) {
        ret["error"] = "Track 0 not found";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    
    for (int sec = 0; sec < 8; sec++) {
        auto* sector = track->getSector(sec);
        if (!sector) continue;
        
        uint8_t* data = sector->data;
        
        // 16 entries per sector, 16 bytes each
        for (int entry = 0; entry < 16; entry++) {
            int offset = entry * 16;
            
            // First byte = 0x00 means end of catalog
            if (data[offset] == 0x00) {
                goto done;
            }
            
            // First byte = 0x01 means deleted file
            if (data[offset] == 0x01) continue;
            
            Json::Value file;
            
            // Filename: bytes 0-7
            std::string name;
            for (int i = 0; i < 8; i++) {
                uint8_t c = data[offset + i];
                if (c >= 32 && c < 128) name += (char)c;
            }
            file["name"] = name;
            
            // Type: byte 8
            char type = data[offset + 8];
            file["type"] = std::string(1, type);
            
            // Start address: bytes 9-10 (LE)
            uint16_t start = data[offset + 9] | (data[offset + 10] << 8);
            file["start"] = start;
            
            // Length: bytes 11-12 (LE)
            uint16_t length = data[offset + 11] | (data[offset + 12] << 8);
            file["length"] = length;
            
            // Sectors: byte 13
            file["sectors"] = data[offset + 13];
            
            // Start sector: byte 14
            file["first_sector"] = data[offset + 14];
            
            // Start track: byte 15
            file["first_track"] = data[offset + 15];
            
            ret["files"].append(file);
        }
    }
    
done:
    ret["file_count"] = ret["files"].size();
    
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

} // namespace v1
} // namespace api

