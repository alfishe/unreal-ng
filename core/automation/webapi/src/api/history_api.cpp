// Device history API: the histories of the machine's devices over HTTP (debugger/history/devicehistory.h). Every
// route is a thin adapter over HistoryControl (core/src/debugger/history/historycontrol.h): the request becomes a
// HistoryRequest, the reply is the response body and its status.

#include "../emulator_api.h"
#include "../common/statenode_json.h"

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include <debugger/history/historycontrol.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper function defined in emulator_api.cpp
void addCorsHeaders(drogon::HttpResponsePtr& resp);

namespace
{
    void Respond(std::function<void(const HttpResponsePtr&)>& callback, const HistoryReply& reply)
    {
        auto resp = HttpResponse::newHttpJsonResponse(StateNodeToJson(reply.body));
        resp->setStatusCode(static_cast<HttpStatusCode>(reply.httpStatus));
        addCorsHeaders(resp);
        callback(resp);
    }

    void Run(const std::string& id, HistoryRequest& request, std::function<void(const HttpResponsePtr&)>& callback)
    {
        auto emulator = getEmulatorByIdOrIndex(id);
        HistoryReply reply = HistoryControl::Execute(emulator ? emulator->GetContext() : nullptr, request);
        if (!emulator)
            reply.body["error"] = "Emulator not found with ID: " + id;
        Respond(callback, reply);
    }

    std::optional<uint64_t> Number(const Json::Value& value)
    {
        if (value.isNumeric())
            return value.asUInt64();
        try
        {
            return std::stoull(value.asString());
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
}  // namespace

/// @brief GET /api/v1/emulator/{id}/history
void EmulatorAPI::getHistories(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    (void)req;
    HistoryRequest request;
    request.verb = "list";
    Run(id, request, callback);
}

/// @brief GET /api/v1/emulator/{id}/history/{device}?last=N
void EmulatorAPI::getHistory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                             const std::string& id, const std::string& device) const
{
    HistoryRequest request;
    request.verb = "read";
    request.device = device;
    if (const std::string last = req->getParameter("last"); !last.empty())
        request.last = static_cast<size_t>(Number(Json::Value(last)).value_or(0));
    Run(id, request, callback);
}

/// @brief POST /api/v1/emulator/{id}/history/{device}/config {"mode", "limit", "stream", "stream_limit"}
void EmulatorAPI::postHistoryConfig(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id, const std::string& device) const
{
    HistoryRequest request;
    request.verb = "config";
    request.device = device;
    auto take = [&request](const std::string& name, const Json::Value& value) {
        if (name == "mode")
            request.mode = value.asString();
        else if (name == "limit")
            request.limit = Number(value);
        else if (name == "stream")
            request.stream = value.asString();
        else if (name == "stream_limit" || name == "streamLimit")
            request.streamLimit = Number(value);
    };
    for (const auto& [name, value] : req->getParameters())
        take(name, Json::Value(value));
    if (auto json = req->getJsonObject())
    {
        for (const std::string& name : json->getMemberNames())
            take(name, (*json)[name]);
    }
    Run(id, request, callback);
}

/// @brief POST /api/v1/emulator/{id}/history/{device}/clear
void EmulatorAPI::postHistoryClear(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id, const std::string& device) const
{
    (void)req;
    HistoryRequest request;
    request.verb = "clear";
    request.device = device;
    Run(id, request, callback);
}

/// @brief POST /api/v1/emulator/{id}/history/{device}/pause and /resume
void EmulatorAPI::postHistoryPause(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                   const std::string& id, const std::string& device) const
{
    (void)req;
    HistoryRequest request;
    request.verb = "pause";
    request.device = device;
    Run(id, request, callback);
}

void EmulatorAPI::postHistoryResume(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                    const std::string& id, const std::string& device) const
{
    (void)req;
    HistoryRequest request;
    request.verb = "resume";
    request.device = device;
    Run(id, request, callback);
}

}  // namespace v1
}  // namespace api
