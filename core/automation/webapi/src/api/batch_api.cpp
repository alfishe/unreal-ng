/// @file batch_api.cpp
/// @brief Batch command execution WebAPI endpoint
/// @date 2026-01-13
///
/// Implements POST /api/v1/batch/execute for parallel command execution

#include <drogon/drogon.h>

#include "../emulator_api.h"
#include "batch-command-processor.h"
#include "emulator/emulatormanager.h"

namespace api
{
namespace v1
{

/// @brief Execute batch commands in parallel
/// POST /api/v1/batch/execute
///
/// Request body:
/// {
///     "commands": [
///         {"emulator": "emu-001", "command": "load-snapshot", "arg1": "/path/to/game.sna"},
///         {"emulator": "emu-002", "command": "load-snapshot", "arg1": "/path/to/game.sna"},
///         {"emulator": "emu-003", "command": "reset"},
///         {"emulator": "0", "command": "feature", "arg1": "sound", "arg2": "off"}
///     ]
/// }
///
/// Response:
/// {
///     "success": true,
///     "total": 4,
///     "succeeded": 4,
///     "failed": 0,
///     "duration_ms": 2.34,
///     "results": [
///         {"emulator": "emu-001", "command": "load-snapshot", "success": true},
///         {"emulator": "emu-002", "command": "load-snapshot", "success": true},
///         {"emulator": "emu-003", "command": "reset", "success": true},
///         {"emulator": "0", "command": "feature", "success": true}
///     ]
/// }
void EmulatorAPI::executeBatch(const drogon::HttpRequestPtr& req,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    auto jsonPtr = req->getJsonObject();
    if (!jsonPtr)
    {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(Json::Value(Json::objectValue));
        resp->setStatusCode(drogon::k400BadRequest);
        (*resp->getJsonObject())["error"] = "Invalid JSON body";
        callback(resp);
        return;
    }

    const Json::Value& json = *jsonPtr;

    // Validate commands array exists
    if (!json.isMember("commands") || !json["commands"].isArray())
    {
        auto resp = drogon::HttpResponse::newHttpJsonResponse(Json::Value(Json::objectValue));
        resp->setStatusCode(drogon::k400BadRequest);
        (*resp->getJsonObject())["error"] = "Missing 'commands' array";
        callback(resp);
        return;
    }

    // Parse commands
    std::vector<BatchCommand> commands;
    const Json::Value& cmdsJson = json["commands"];

    for (const auto& cmdJson : cmdsJson)
    {
        BatchCommand cmd;
        cmd.emulatorId = cmdJson.get("emulator", "").asString();
        cmd.command = cmdJson.get("command", "").asString();
        cmd.arg1 = cmdJson.get("arg1", "").asString();
        cmd.arg2 = cmdJson.get("arg2", "").asString();

        // Validate required fields
        if (cmd.emulatorId.empty())
        {
            auto resp = drogon::HttpResponse::newHttpJsonResponse(Json::Value(Json::objectValue));
            resp->setStatusCode(drogon::k400BadRequest);
            (*resp->getJsonObject())["error"] = "Command missing 'emulator' field";
            callback(resp);
            return;
        }

        if (cmd.command.empty())
        {
            auto resp = drogon::HttpResponse::newHttpJsonResponse(Json::Value(Json::objectValue));
            resp->setStatusCode(drogon::k400BadRequest);
            (*resp->getJsonObject())["error"] = "Command missing 'command' field";
            callback(resp);
            return;
        }

        // Validate command is batchable
        if (!BatchCommandProcessor::IsBatchable(cmd.command))
        {
            auto resp = drogon::HttpResponse::newHttpJsonResponse(Json::Value(Json::objectValue));
            resp->setStatusCode(drogon::k400BadRequest);
            (*resp->getJsonObject())["error"] = "Command not batchable: " + cmd.command;
            callback(resp);
            return;
        }

        commands.push_back(cmd);
    }

    // Execute batch
    EmulatorManager* manager = EmulatorManager::GetInstance();
    BatchCommandProcessor processor(manager);
    BatchResult result = processor.Execute(commands);

    // Build response
    Json::Value response;
    response["success"] = result.success;
    response["total"] = result.total;
    response["succeeded"] = result.succeeded;
    response["failed"] = result.failed;
    response["duration_ms"] = result.durationMs;

    Json::Value resultsJson(Json::arrayValue);
    for (const auto& r : result.results)
    {
        Json::Value resultJson;
        resultJson["emulator"] = r.emulatorId;
        resultJson["command"] = r.command;
        resultJson["success"] = r.success;
        if (!r.error.empty())
            resultJson["error"] = r.error;
        resultsJson.append(resultJson);
    }
    response["results"] = resultsJson;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
    resp->setStatusCode(result.success ? drogon::k200OK : drogon::k207MultiStatus);
    callback(resp);
}

/// @brief Get list of batchable commands
/// GET /api/v1/batch/commands
void EmulatorAPI::getBatchableCommands(const drogon::HttpRequestPtr& req,
                                       std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    Json::Value response;
    Json::Value commands(Json::arrayValue);

    for (const auto& cmd : BatchCommandProcessor::GetBatchableCommands())
    {
        commands.append(cmd);
    }

    response["commands"] = commands;
    response["count"] = static_cast<int>(BatchCommandProcessor::GetBatchableCommands().size());

    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
    callback(resp);
}

}  // namespace v1
}  // namespace api
