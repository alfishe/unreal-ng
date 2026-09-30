/// Device state endpoints backed by the core DeviceState reports
/// (core/src/emulator/state/devicestate.h): TurboSound FM (2 x YM2203) and
/// the Beta Disk WD1793. The AY endpoints live in state_audio_api.cpp and
/// use the same reports. Every interface (Python, Lua, CLI, MCP) renders
/// the same trees, so the JSON here is the reference shape.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/rtc/ds12887.h>
#include <emulator/io/rtc/rtcaccess.h>
#include <emulator/state/devicestate.h>
#include <json/json.h>

#include <cstdio>

#include "../emulator_api.h"
#include "../common/statenode_json.h"

using namespace drogon;

namespace api
{
namespace v1
{

void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
void ReplyNotFound(const std::string& message, std::function<void(const HttpResponsePtr&)>& callback,
                   HttpStatusCode code = HttpStatusCode::k404NotFound)
{
    Json::Value error;
    error["error"] = code == HttpStatusCode::k404NotFound ? "Not Found" : "Bad Request";
    error["message"] = message;
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

/// A report whose `available` is false is answered as 404 with its description
void ReplyState(const StateNode& node, std::function<void(const HttpResponsePtr&)>& callback)
{
    const StateNode* available = node.find("available");
    if (available && available->kind == StateNode::Kind::Bool && !available->b)
    {
        const StateNode* description = node.find("description");
        ReplyNotFound(description ? description->s : "state not available", callback);
        return;
    }
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(node));
    addCorsHeaders(resp);
    callback(resp);
}

bool ParseIndex(const std::string& text, int& out)
{
    try
    {
        out = std::stoi(text);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

std::string MultipleEmulatorsMessage(size_t count, const char* path)
{
    return count == 0 ? std::string("No emulator available (none running)")
                      : std::string("Multiple emulators running. Please specify emulator ID in path: ") + path;
}
}  // namespace

/// Shared with the audio endpoints (state_audio_api.cpp): a DeviceState
/// report as JSON, or 404 with its description when `available` is false
void ReplyDeviceState(const StateNode& node, std::function<void(const HttpResponsePtr&)>& callback)
{
    ReplyState(node, callback);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/fm
void EmulatorAPI::getStateAudioFM(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyNotFound("Emulator not found with ID: " + id, callback);
    ReplyState(DeviceState::Fm(emulator->GetContext()), callback);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/fm/{chip}
void EmulatorAPI::getStateAudioFMIndex(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                       const std::string& id, const std::string& chip) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyNotFound("Emulator not found with ID: " + id, callback);
    int index = -1;
    if (!ParseIndex(chip, index))
        return ReplyNotFound("Invalid chip index (must be integer)", callback, HttpStatusCode::k400BadRequest);
    ReplyState(DeviceState::FmChip(emulator->GetContext(), index), callback);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/moonsound
void EmulatorAPI::getStateAudioMoonSound(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback,
                                         const std::string& id) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyNotFound("Emulator not found with ID: " + id, callback);
    ReplyState(DeviceState::MoonSound(emulator->GetContext()), callback);
}

/// @brief GET /api/v1/emulator/{id}/state/audio/moonsound/{fm|pcm}
void EmulatorAPI::getStateAudioMoonSoundPart(const HttpRequestPtr& req,
                                             std::function<void(const HttpResponsePtr&)>&& callback,
                                             const std::string& id, const std::string& part) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyNotFound("Emulator not found with ID: " + id, callback);
    if (part == "fm")
        return ReplyState(DeviceState::MoonSoundFm(emulator->GetContext()), callback);
    if (part == "pcm")
        return ReplyState(DeviceState::MoonSoundPcm(emulator->GetContext()), callback);
    ReplyNotFound("Unknown MoonSound part '" + part + "' (fm or pcm)", callback, HttpStatusCode::k400BadRequest);
}

void EmulatorAPI::getStateAudioMoonSoundActive(const HttpRequestPtr& req,
                                               std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();
    if (!emulator)
    {
        const size_t count = EmulatorManager::GetInstance()->GetEmulatorIds().size();
        return ReplyNotFound(MultipleEmulatorsMessage(count, "/api/v1/emulator/{id}/state/audio/moonsound"), callback,
                             count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
    }
    getStateAudioMoonSound(req, std::move(callback), emulator->GetId());
}

void EmulatorAPI::getStateAudioMoonSoundPartActive(const HttpRequestPtr& req,
                                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                                   const std::string& part) const
{
    auto emulator = getEmulatorWithGlobalSelection();
    if (!emulator)
    {
        const size_t count = EmulatorManager::GetInstance()->GetEmulatorIds().size();
        return ReplyNotFound(MultipleEmulatorsMessage(count, "/api/v1/emulator/{id}/state/audio/moonsound/{part}"),
                             callback, count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
    }
    getStateAudioMoonSoundPart(req, std::move(callback), emulator->GetId(), part);
}

/// @brief GET /api/v1/emulator/{id}/state/fdc
void EmulatorAPI::getStateFdc(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyNotFound("Emulator not found with ID: " + id, callback);
    ReplyState(DeviceState::Fdc(emulator->GetContext()), callback);
}

void EmulatorAPI::getStateAudioFMActive(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();
    if (!emulator)
    {
        const size_t count = EmulatorManager::GetInstance()->GetEmulatorIds().size();
        return ReplyNotFound(MultipleEmulatorsMessage(count, "/api/v1/emulator/{id}/state/audio/fm"), callback,
                             count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
    }
    getStateAudioFM(req, std::move(callback), emulator->GetId());
}

void EmulatorAPI::getStateAudioFMIndexActive(const HttpRequestPtr& req,
                                             std::function<void(const HttpResponsePtr&)>&& callback,
                                             const std::string& chip) const
{
    auto emulator = getEmulatorWithGlobalSelection();
    if (!emulator)
    {
        const size_t count = EmulatorManager::GetInstance()->GetEmulatorIds().size();
        return ReplyNotFound(MultipleEmulatorsMessage(count, "/api/v1/emulator/{id}/state/audio/fm/{chip}"), callback,
                             count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
    }
    getStateAudioFMIndex(req, std::move(callback), emulator->GetId(), chip);
}

void EmulatorAPI::getStateFdcActive(const HttpRequestPtr& req,
                                    std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();
    if (!emulator)
    {
        const size_t count = EmulatorManager::GetInstance()->GetEmulatorIds().size();
        return ReplyNotFound(MultipleEmulatorsMessage(count, "/api/v1/emulator/{id}/state/fdc"), callback,
                             count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
    }
    getStateFdc(req, std::move(callback), emulator->GetId());
}

/// @brief GET /api/v1/emulator/{id}/state/contention
void EmulatorAPI::getStateContention(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyNotFound("Emulator not found with ID: " + id, callback);
    ReplyState(DeviceState::Contention(emulator->GetContext()), callback);
}

void EmulatorAPI::getStateContentionActive(const HttpRequestPtr& req,
                                           std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();
    if (!emulator)
    {
        const size_t count = EmulatorManager::GetInstance()->GetEmulatorIds().size();
        return ReplyNotFound(MultipleEmulatorsMessage(count, "/api/v1/emulator/{id}/state/contention"), callback,
                             count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
    }
    getStateContention(req, std::move(callback), emulator->GetId());
}

/// @brief GET /api/v1/emulator/{id}/state/ide - the IDE board (DeviceState::Ide); 404 without one
void EmulatorAPI::getStateIde(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyNotFound("Emulator not found with ID: " + id, callback);
    ReplyState(DeviceState::Ide(emulator->GetContext()), callback);
}

void EmulatorAPI::getStateIdeActive(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();
    if (!emulator)
    {
        const size_t count = EmulatorManager::GetInstance()->GetEmulatorIds().size();
        return ReplyNotFound(MultipleEmulatorsMessage(count, "/api/v1/emulator/{id}/state/ide"), callback,
                             count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
    }
    getStateIde(req, std::move(callback), emulator->GetId());
}

/// @brief GET /api/v1/emulator/{id}/state/tsconf - the TS-Conf machine (DeviceState::TsConf); 404 on other machines
void EmulatorAPI::getStateTsConf(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyNotFound("Emulator not found with ID: " + id, callback);
    ReplyState(DeviceState::TsConf(emulator->GetContext()), callback);
}

void EmulatorAPI::getStateTsConfActive(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();
    if (!emulator)
    {
        const size_t count = EmulatorManager::GetInstance()->GetEmulatorIds().size();
        return ReplyNotFound(MultipleEmulatorsMessage(count, "/api/v1/emulator/{id}/state/tsconf"), callback,
                             count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
    }
    getStateTsConf(req, std::move(callback), emulator->GetId());
}


/// region <CMOS clock>

namespace
{
/// Decimal or 0x-prefixed hex, in [0, max]
bool ParseRtcNumber(const Json::Value& value, unsigned max, unsigned& out)
{
    try
    {
        unsigned long parsed = 0;
        if (value.isUInt())
            parsed = value.asUInt();
        else if (value.isString())
        {
            const std::string text = value.asString();
            size_t used = 0;
            const bool hex = text.size() > 2 && (text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0);
            parsed = std::stoul(hex ? text.substr(2) : text, &used, hex ? 16 : 10);
            if (used != (hex ? text.size() - 2 : text.size()))
                return false;
        }
        else
            return false;
        if (parsed > max)
            return false;
        out = static_cast<unsigned>(parsed);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

/// No clock is 404, a bad range 400
void ReplyRtcError(EmulatorContext* context, const std::string& error,
                   std::function<void(const HttpResponsePtr&)>& callback)
{
    const bool noClock = RtcAccess::Find(context) == nullptr;
    ReplyNotFound(error, callback, noClock ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
}

Json::Value CellsJson(unsigned start, const std::vector<uint8_t>& bytes)
{
    Json::Value ret;
    ret["start"] = start;
    ret["count"] = static_cast<Json::UInt>(bytes.size());
    Json::Value values(Json::arrayValue);
    std::string hex;
    char cell[4];
    for (uint8_t b : bytes)
    {
        values.append(b);
        std::snprintf(cell, sizeof(cell), "%02X", b);
        if (!hex.empty())
            hex += ' ';
        hex += cell;
    }
    ret["bytes"] = values;
    ret["hex"] = hex;
    return ret;
}
}  // namespace

/// @brief GET /api/v1/emulator/{id}/state/rtc - the CMOS clock (DeviceState::Rtc); 404 without one
void EmulatorAPI::getStateRtc(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyNotFound("Emulator not found with ID: " + id, callback);
    ReplyState(DeviceState::Rtc(emulator->GetContext()), callback);
}

void EmulatorAPI::getStateRtcActive(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) const
{
    auto emulator = getEmulatorWithGlobalSelection();
    if (!emulator)
    {
        const size_t count = EmulatorManager::GetInstance()->GetEmulatorIds().size();
        return ReplyNotFound(MultipleEmulatorsMessage(count, "/api/v1/emulator/{id}/state/rtc"), callback,
                             count == 0 ? HttpStatusCode::k404NotFound : HttpStatusCode::k400BadRequest);
    }
    getStateRtc(req, std::move(callback), emulator->GetId());
}

/// @brief GET /api/v1/emulator/{id}/rtc/cells?start=&count= - cells as the guest reads them (peeked)
void EmulatorAPI::getRtcCells(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyNotFound("Emulator not found with ID: " + id, callback);
    EmulatorContext* context = emulator->GetContext();

    std::string error;
    Ds12887* chip = RtcAccess::Find(context, &error);
    if (!chip)
        return ReplyNotFound(error, callback);

    unsigned start = 0;
    unsigned count = 0;
    const std::string startText = req->getParameter("start");
    const std::string countText = req->getParameter("count");
    if ((!startText.empty() && !ParseRtcNumber(Json::Value(startText), 255, start)) ||
        (!countText.empty() && !ParseRtcNumber(Json::Value(countText), 256, count)))
        return ReplyNotFound("start and count must be numbers (decimal or 0x hex)", callback,
                             HttpStatusCode::k400BadRequest);
    if (countText.empty())
    {
        // Default: from start to the last cell
        const unsigned cells = static_cast<unsigned>(chip->GetCellCount());
        count = start < cells ? cells - start : 1;
    }

    std::vector<uint8_t> bytes;
    if (!RtcAccess::Read(context, start, count, bytes, error))
        return ReplyRtcError(context, error, callback);

    auto resp = HttpResponse::newHttpJsonResponse(CellsJson(start, bytes));
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/{id}/rtc/cells {"start": n, "bytes": [..]} - write like the guest
void EmulatorAPI::postRtcCells(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyNotFound("Emulator not found with ID: " + id, callback);
    EmulatorContext* context = emulator->GetContext();

    auto body = req->getJsonObject();
    unsigned start = 0;
    if (!body || !body->isMember("start") || !body->isMember("bytes") || !(*body)["bytes"].isArray() ||
        !ParseRtcNumber((*body)["start"], 255, start))
        return ReplyNotFound("Body must be {\"start\": <cell>, \"bytes\": [<byte>, ...]}", callback,
                             HttpStatusCode::k400BadRequest);

    std::vector<uint8_t> bytes;
    for (const Json::Value& item : (*body)["bytes"])
    {
        unsigned value = 0;
        if (!ParseRtcNumber(item, 255, value))
            return ReplyNotFound("Every byte must be 0-255 (decimal or 0x hex)", callback,
                                 HttpStatusCode::k400BadRequest);
        bytes.push_back(static_cast<uint8_t>(value));
    }

    std::string error;
    if (!RtcAccess::Write(context, start, bytes, "WebAPI rtc write", error))
        return ReplyRtcError(context, error, callback);

    std::vector<uint8_t> after;
    RtcAccess::Read(context, start, static_cast<unsigned>(bytes.size()), after, error);
    Json::Value ret = CellsJson(start, after);
    ret["success"] = true;
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// endregion </CMOS clock>

}  // namespace v1
}  // namespace api
