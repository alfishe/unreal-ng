/// @file operations_api.cpp
/// @brief The operation log endpoint (the machine state coordinator's log: reset, snapshot load, media changes,
/// refusals included). Thin: the report is OperationsStatus::Report, shared with CLI, Lua and Python (MCP
/// emulator_manage operations calls this route).

#include "../../../operationsstatus.h"
#include "../common/statenode_json.h"
#include "../emulator_api.h"

#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <json/json.h>

#include <cstdint>
#include <string>

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{
// CORS helper (defined in emulator_api.cpp)
extern void addCorsHeaders(HttpResponsePtr& resp);
} // namespace v1
} // namespace api

namespace
{
void Respond(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body, HttpStatusCode code)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

void RespondError(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode code, const char* error,
                  const std::string& message)
{
    Json::Value body;
    body["error"] = error;
    body["message"] = message;
    Respond(callback, body, code);
}

/// An optional non-negative integer query parameter; false when it is there and not a number
bool ParseCount(const std::string& text, uint64_t& value)
{
    if (text.empty())
        return true;
    if (text.find_first_not_of("0123456789") != std::string::npos || text.size() > 19)
        return false;
    value = std::stoull(text);
    return true;
}
} // namespace

namespace api
{
namespace v1
{
/// @brief GET /api/v1/emulator/{id}/operations[?since=<seq>&limit=<n>]
void EmulatorAPI::getOperations(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        RespondError(callback, k404NotFound, "Not Found", "Emulator with specified ID not found");
        return;
    }
    uint64_t since = 0;
    uint64_t limit = 0;
    if (!ParseCount(req->getParameter("since"), since) || !ParseCount(req->getParameter("limit"), limit))
    {
        RespondError(callback, k400BadRequest, "Bad Request",
                     "since and limit are non-negative integers (since: a seq from last_seq; limit: newest N, 0 = all)");
        return;
    }
    Respond(callback, StateNodeToJson(OperationsStatus::Report(emulator.get(), since, static_cast<size_t>(limit))),
            k200OK);
}
} // namespace v1
} // namespace api
