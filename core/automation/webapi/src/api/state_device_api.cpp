/// Device state endpoints backed by the core DeviceState reports
/// (core/src/emulator/state/devicestate.h): TurboSound FM (2 x YM2203) and
/// the Beta Disk WD1793. The AY endpoints live in state_audio_api.cpp and
/// use the same reports. Every interface (Python, Lua, CLI, MCP) renders
/// the same trees, so the JSON here is the reference shape.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/state/devicestate.h>
#include <json/json.h>

#include "../emulator_api.h"
#include "statenode_json.h"

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

}  // namespace v1
}  // namespace api
