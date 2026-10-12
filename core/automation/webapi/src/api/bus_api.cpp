/// Bus analyzer endpoints (docs/inprogress/2026-10-11-bus-analyzer/technical-design.md §7), backed by the core
/// DeviceState::Bus* reports: every interface (MCP, CLI, Lua, Python) renders the same trees.

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/bus/bustrace.h>
#include <emulator/io/sdcard/sdcardspi.h>
#include <emulator/state/devicestate.h>
#include <json/json.h>

#include <fstream>

#include "../common/statenode_json.h"
#include "../emulator_api.h"

using namespace drogon;

namespace api
{
namespace v1
{

void addCorsHeaders(HttpResponsePtr& resp);

namespace
{
void ReplyError(const std::string& message, std::function<void(const HttpResponsePtr&)>& callback,
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
void ReplyBus(const StateNode& node, std::function<void(const HttpResponsePtr&)>& callback)
{
    const StateNode* available = node.find("available");
    if (available && available->kind == StateNode::Kind::Bool && !available->b)
    {
        const StateNode* description = node.find("description");
        ReplyError(description ? description->s : "bus not available", callback);
        return;
    }
    auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(node));
    addCorsHeaders(resp);
    callback(resp);
}

/// -1 keep, 0 off, 1 on
int Switch(const std::shared_ptr<Json::Value>& body, const char* key)
{
    if (!body || !body->isMember(key))
        return -1;
    return (*body)[key].asBool() ? 1 : 0;
}
}  // namespace

/// @brief GET /api/v1/emulator/{id}/bus - every bus of the machine and its cards (DeviceState::BusList)
void EmulatorAPI::getBuses(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                           const std::string& id) const
{
    (void)req;
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError("Emulator not found with ID: " + id, callback);
    ReplyBus(DeviceState::BusList(emulator->GetContext()), callback);
}

/// @brief GET /api/v1/emulator/{id}/bus/{bus}/events?kinds=&slot=&since=&from=&to=&limit= - the bus's trace (DeviceState::BusEvents)
void EmulatorAPI::getBusEvents(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id, const std::string& bus) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError("Emulator not found with ID: " + id, callback);
    BusEventQuery query;
    std::string error;
    if (!BusEventQueryFromStrings(req->getParameter("kinds"), req->getParameter("slot"), req->getParameter("since"),
                                  req->getParameter("from"), req->getParameter("to"), req->getParameter("limit"), query, error))
        return ReplyError(error, callback, HttpStatusCode::k400BadRequest);
    ReplyBus(DeviceState::BusEvents(emulator->GetContext(), bus, query), callback);
}

/// @brief GET /api/v1/emulator/{id}/bus/{bus}/findings?rules=&since=&limit= - what the checks found (DeviceState::BusFindings)
void EmulatorAPI::getBusFindings(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                 const std::string& id, const std::string& bus) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError("Emulator not found with ID: " + id, callback);
    BusFindingQuery query;
    std::string error;
    if (!BusFindingQueryFromStrings(req->getParameter("rules"), req->getParameter("since"), req->getParameter("limit"), query, error))
        return ReplyError(error, callback, HttpStatusCode::k400BadRequest);
    ReplyBus(DeviceState::BusFindings(emulator->GetContext(), bus, query), callback);
}

/// @brief POST /api/v1/emulator/{id}/bus/{bus}/trace-journal {"enabled", "checks", "clear", "capacity", "protocol_only"} - switch the trace and the
/// checks, clear, resize; `bus` "all" for every bus (DeviceState::BusControl). Observes only
void EmulatorAPI::postBusTraceJournal(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                      const std::string& id, const std::string& bus) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError("Emulator not found with ID: " + id, callback);
    auto body = req->getJsonObject();
    const bool clear = body && body->isMember("clear") && (*body)["clear"].asBool();
    const size_t capacity = body && body->isMember("capacity") ? static_cast<size_t>((*body)["capacity"].asUInt64()) : 0;
    ReplyBus(DeviceState::BusControl(emulator->GetContext(), bus, Switch(body, "enabled"), Switch(body, "checks"), clear, capacity,
                                     Switch(body, "protocol_only")),
             callback);
}

/// @brief GET / POST /api/v1/emulator/{id}/bus/{bus}/sd-timing {"slot", "profile", "seed"} - an SD card's timing profile
/// (DeviceState::BusSdTiming); GET (or no profile) reports it
void EmulatorAPI::busSdTiming(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                              const std::string& id, const std::string& bus) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError("Emulator not found with ID: " + id, callback);
    auto body = req->getJsonObject();
    int slot = -1;
    std::string profile;
    int64_t seed = 0;
    if (req->method() == drogon::Post && body)
    {
        if (body->isMember("slot"))
            slot = (*body)["slot"].asInt();
        if (body->isMember("profile"))
            profile = (*body)["profile"].asString();
        if (body->isMember("seed"))
            seed = (*body)["seed"].asInt64();
    }
    else if (!req->getParameter("slot").empty())
        slot = std::atoi(req->getParameter("slot").c_str());
    SdCardSpi::Timing timing;
    if (!profile.empty() && !SdCardSpi::TimingFromName(profile, timing))
        return ReplyError("profile: '" + profile + "' is not one of ideal, realistic, slow", callback, HttpStatusCode::k400BadRequest);
    ReplyBus(DeviceState::BusSdTiming(emulator->GetContext(), bus, slot, profile, seed), callback);
}

/// @brief GET /api/v1/emulator/{id}/bus/{bus}/export?format=vcd|jsonl&path=&kinds=&slot=&since=&from=&to=&limit= - the trace as VCD
/// (wires synthesized, for GTKWave / PulseView) or JSON lines (DeviceState::BusExport); limit defaults to 0 (all). With path the
/// text goes to that file on the emulator host and the reply is {ok, bus, format, path, bytes}
void EmulatorAPI::getBusExport(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id, const std::string& bus) const
{
    auto emulator = getEmulatorByIdOrIndex(id);
    if (!emulator)
        return ReplyError("Emulator not found with ID: " + id, callback);
    BusEventQuery query;
    std::string error;
    const std::string limit = req->getParameter("limit").empty() ? "0" : req->getParameter("limit");
    if (!BusEventQueryFromStrings(req->getParameter("kinds"), req->getParameter("slot"), req->getParameter("since"),
                                  req->getParameter("from"), req->getParameter("to"), limit, query, error))
        return ReplyError(error, callback, HttpStatusCode::k400BadRequest);
    const std::string format = req->getParameter("format").empty() ? "vcd" : req->getParameter("format");
    const std::string text = DeviceState::BusExport(emulator->GetContext(), bus, format, query, error);
    if (!error.empty())
        return ReplyError(error, callback, error.rfind("format", 0) == 0 ? HttpStatusCode::k400BadRequest : HttpStatusCode::k404NotFound);
    // path=: written to a file on the emulator host (an agent's scratch/), the reply says where and how much
    const std::string path = req->getParameter("path");
    if (!path.empty())
    {
        std::ofstream file(path, std::ios::binary);
        if (!file)
            return ReplyError("path: '" + path + "' cannot be written", callback, HttpStatusCode::k400BadRequest);
        file << text;
        Json::Value reply;
        reply["ok"] = true;
        reply["bus"] = bus;
        reply["format"] = format;
        reply["path"] = path;
        reply["bytes"] = static_cast<Json::UInt64>(text.size());
        auto resp = HttpResponse::newHttpJsonResponse(reply);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }
    auto resp = HttpResponse::newHttpResponse();
    resp->setContentTypeCode(CT_TEXT_PLAIN);
    resp->setBody(text);
    addCorsHeaders(resp);
    callback(resp);
}

}  // namespace v1
}  // namespace api
