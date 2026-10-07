/// @author rfishe
/// @date 08.01.2026
/// @brief WebAPI Snapshot control endpoints

#include <debugger/ttd/timetravelmanager.h>
#include <drogon/HttpResponse.h>
#include <emulator/emulator.h>
#include <emulator/config.h>
#include <emulator/emulatormanager.h>
#include <common/filehelper.h>
#include <json/json.h>
#include <loaders/snapshot/machinestatetransfer.h>
#include <loaders/snapshot/snapshotlauncher.h>

#include "../emulator_api.h"
#include "../common/statenode_json.h"
#include "../common/upload_helper.h"

using namespace drogon;
using namespace api::v1;

namespace api
{
namespace v1
{

// Helper to add CORS headers (declared extern in tape_disk_api.cpp)
extern void addCorsHeaders(HttpResponsePtr& resp);
extern std::string stateToString(EmulatorStateEnum state);

/// @brief POST /api/v1/emulator/:id/snapshot/load
/// @brief Load snapshot file
void EmulatorAPI::loadSnapshot(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Thread safety: reject operations on emulators being destroyed
    if (emulator->IsDestroying())
    {
        Json::Value error;
        error["error"] = "Service Unavailable";
        error["message"] = "Emulator is shutting down";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k503ServiceUnavailable);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Extract content: JSON path, multipart file, or raw body
    auto content = extractMediaContent(req, MediaType::Snapshot);
    if (!content.valid)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = content.errorMsg;

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k400BadRequest);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    std::string path;
    if (content.isEmbedded)
    {
        // Stage the uploaded content to a temp file
        std::string stageError;
        path = UploadHelper::Instance().stageUpload(
            content.data, content.filename, MediaType::Snapshot, stageError);
        if (path.empty())
        {
            Json::Value error;
            error["error"] = "Internal Error";
            error["message"] = "Failed to stage upload: " + stageError;

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k500InternalServerError);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
    }
    else
    {
        path = content.path;
    }

    // A snapshot load ENDS a TTD recording session (the one rule), it is never refused by it

    // A file that needs another model (an SPG: TS-Conf) switches the model
    // first unless switch_model is false (body field, query parameter)
    SnapshotLoadRequest request;
    request.emulatorId = emulator->GetId();
    request.path = path;
    if (auto json = req->getJsonObject(); json && json->isMember("switch_model"))
        request.switchModel = (*json)["switch_model"].asBool();
    else if (const std::string flag = req->getParameter("switch_model"); !flag.empty())
        request.switchModel = flag == "true" || flag == "1";
    // Who commits: the plan's default, "legacy", or a registered policy (body field or query parameter)
    if (auto json = req->getJsonObject(); json && json->isMember("commit"))
        request.commit = (*json)["commit"].asString();
    else
        request.commit = req->getParameter("commit");
    emulator.reset();
    const SnapshotLoadResult result = SnapshotLauncher::Load(request);
    const bool success = result.ok;

    Json::Value ret;
    ret["status"] = success ? "success" : "error";
    ret["message"] = success ? (result.modelSwitched ? "Model switched to " + result.requiredModel + "; snapshot loaded"
                                                     : std::string("Snapshot loaded successfully"))
                             : result.message;
    ret["path"] = path;
    if (content.isEmbedded)
        ret["uploaded"] = true;
    if (result.emulator)
        ret["emulator_id"] = result.emulator->GetId();
    ret["model_switched"] = result.modelSwitched;
    if (result.modelSwitched)
    {
        ret["previous_emulator_id"] = result.previousEmulatorId;
        ret["model"] = result.requiredModel;
    }
    if (result.modelMismatch)
    {
        ret["required_model"] = result.requiredModel;
        ret["required_ram_kb"] = result.requiredRamKb;
    }
    // What the snapshot pipeline did (empty when the load never reached a loader)
    if (!result.report.format.empty())
        ret["report"] = StateNodeToJson(result.report.ToStateNode());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(success               ? HttpStatusCode::k200OK
                        : result.modelMismatch ? HttpStatusCode::k409Conflict
                                               : HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/snapshot/inspect
/// @brief What loading a snapshot file would do on this machine: the image and the plan; nothing is written
void EmulatorAPI::inspectSnapshot(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    auto reply = [&callback](HttpStatusCode code, const Json::Value& body) {
        auto resp = HttpResponse::newHttpJsonResponse(body);
        resp->setStatusCode(code);
        addCorsHeaders(resp);
        callback(resp);
    };

    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        reply(k404NotFound, error);
        return;
    }

    // The same sources as a load: a path in JSON, an upload, or a raw body
    auto content = extractMediaContent(req, MediaType::Snapshot);
    if (!content.valid)
    {
        Json::Value error;
        error["error"] = "Bad Request";
        error["message"] = content.errorMsg;
        reply(k400BadRequest, error);
        return;
    }
    std::string path = content.path;
    if (content.isEmbedded)
    {
        std::string stageError;
        path = UploadHelper::Instance().stageUpload(content.data, content.filename, MediaType::Snapshot, stageError);
        if (path.empty())
        {
            Json::Value error;
            error["error"] = "Internal Error";
            error["message"] = "Failed to stage upload: " + stageError;
            reply(k500InternalServerError, error);
            return;
        }
    }

    std::string commit;
    if (auto json = req->getJsonObject(); json && json->isMember("commit"))
        commit = (*json)["commit"].asString();
    else
        commit = req->getParameter("commit");

    StateNode result;
    std::string error;
    if (!SnapshotLauncher::Inspect(emulator->GetId(), path, commit, result, error))
    {
        Json::Value body;
        body["error"] = "Bad Request";
        body["message"] = error;
        reply(k400BadRequest, body);
        return;
    }
    Json::Value ret = StateNodeToJson(result);
    ret["status"] = "success";
    reply(k200OK, ret);
}

/// @brief GET /api/v1/emulator/:id/snapshot/info
void EmulatorAPI::getSnapshotInfo(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                  const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
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

    std::string snapshotPath = context->coreState.snapshotFilePath;
    bool isLoaded = !snapshotPath.empty();

    ret["status"] = isLoaded ? "loaded" : "empty";
    ret["file"] = snapshotPath;
    // The snapshot pipeline's report of the last load: the commit that ran (or the refusal), verdicts, blocks
    if (!emulator->LastSnapshotReport().format.empty())
        ret["report"] = StateNodeToJson(emulator->LastSnapshotReport().ToStateNode());

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief POST /api/v1/emulator/:id/snapshot/save
/// @brief Save snapshot file
void EmulatorAPI::saveSnapshot(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                               const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto emulator = manager->GetEmulator(id);

    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    // Thread safety: reject operations on emulators being destroyed
    if (emulator->IsDestroying())
    {
        Json::Value error;
        error["error"] = "Service Unavailable";
        error["message"] = "Emulator is shutting down";

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k503ServiceUnavailable);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    auto json = req->getJsonObject();
    if (!json || !json->isMember("path"))
    {
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
    bool force = json->isMember("force") ? (*json)["force"].asBool() : false;
    
    // Expand path (tilde, etc.) for file existence check
    std::string expandedPath = FileHelper::AbsolutePath(path, false);
    
    // Check if file exists and force wasn't specified
    if (!force)
    {
        FILE* testFile = fopen(expandedPath.c_str(), "r");
        if (testFile)
        {
            fclose(testFile);
            Json::Value error;
            error["error"] = "Conflict";
            error["message"] = "File already exists. Use 'force: true' to overwrite.";
            error["path"] = path;

            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(HttpStatusCode::k409Conflict);
            addCorsHeaders(resp);
            callback(resp);
            return;
        }
    }
    
    const bool success = emulator->SaveSnapshot(path);
    const snapshot::SaveResult& saved = emulator->LastSaveResult();

    Json::Value ret;
    ret["status"] = success ? "success" : "error";
    ret["message"] = success ? "Snapshot saved successfully" : saved.text;
    ret["path"] = path;
    if (!saved.format.empty())
        ret["format"] = saved.format;
    if (!saved.machine.empty())
        ret["machine"] = saved.machine;
    if (!success)
    {
        ret["reason"] = saved.reason;
        if (!saved.needs.empty())
            ret["needs"] = saved.needs;
        // What the machine can be saved as now: the way out of a refusal (e.g. "save as .szx")
        ret["formats"] = StateNodeToJson(emulator->SnapshotSaveFormats().ToStateNode());
    }
    if (!saved.warnings.empty())
    {
        ret["warnings"] = Json::arrayValue;
        for (const std::string& warning : saved.warnings)
            ret["warnings"].append(warning);
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(success ? HttpStatusCode::k200OK : HttpStatusCode::k400BadRequest);
    addCorsHeaders(resp);
    callback(resp);
}

/// @brief GET /api/v1/emulator/:id/snapshot/formats
/// @brief Which snapshot formats this machine can be saved in right now, and why not
void EmulatorAPI::getSnapshotFormats(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                     const std::string& id) const
{
    auto emulator = EmulatorManager::GetInstance()->GetEmulator(id);
    if (!emulator)
    {
        Json::Value error;
        error["error"] = "Not Found";
        error["message"] = "Emulator not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        addCorsHeaders(resp);
        callback(resp);
        return;
    }

    Json::Value ret = StateNodeToJson(emulator->SnapshotSaveFormats().ToStateNode());
    ret["status"] = "success";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    addCorsHeaders(resp);
    callback(resp);
}

namespace
{

const char* TransferItemStatusName(MachineStateTransfer::ItemStatus status)
{
    switch (status)
    {
        case MachineStateTransfer::ItemStatus::Copied:
            return "copied";
        case MachineStateTransfer::ItemStatus::Dropped:
            return "dropped";
        case MachineStateTransfer::ItemStatus::Refused:
            return "refused";
        case MachineStateTransfer::ItemStatus::Note:
            return "note";
    }
    return "note";
}

/// The transfer report as the WebAPI (and through it MCP) returns it
Json::Value TransferReportJson(const MachineStateTransfer::Report& report)
{
    Json::Value json;
    json["ok"] = report.ok;
    json["mode"] = report.clone ? "clone" : "cross-model";
    if (!report.reason.empty())
        json["reason"] = report.reason;
    json["items"] = Json::arrayValue;
    for (const MachineStateTransfer::Item& item : report.items)
    {
        Json::Value entry;
        entry["name"] = item.name;
        entry["status"] = TransferItemStatusName(item.status);
        if (!item.detail.empty())
            entry["detail"] = item.detail;
        json["items"].append(entry);
    }
    json["copied"] = static_cast<Json::UInt64>(report.Count(MachineStateTransfer::ItemStatus::Copied));
    json["dropped"] = static_cast<Json::UInt64>(report.Count(MachineStateTransfer::ItemStatus::Dropped));
    json["refused"] = static_cast<Json::UInt64>(report.Count(MachineStateTransfer::ItemStatus::Refused));
    json["summary"] = report.ToString();
    return json;
}

void SendJson(std::function<void(const HttpResponsePtr&)>& callback, const Json::Value& body, HttpStatusCode code)
{
    auto resp = HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    addCorsHeaders(resp);
    callback(resp);
}

void SendError(std::function<void(const HttpResponsePtr&)>& callback, HttpStatusCode code, const std::string& error,
               const std::string& message)
{
    Json::Value body;
    body["error"] = error;
    body["message"] = message;
    SendJson(callback, body, code);
}

}  // namespace

/// @brief POST /api/v1/emulator/:id/snapshot/transfer
/// @brief Move this instance's running state into another instance, in memory (MachineStateTransfer)
/// @details Body: {"to": "<id>"} for an existing instance, or {"model": "<short name>", "ram_size": KB,
///          "symbolic_id": "..."} for a new one (it gets the source's sound cards). Optional: "check": true
///          (existing target only; decide, change nothing), "keep_frame_position" (default true), "start"
///          (new instance; default: start when the source is running)
void EmulatorAPI::transferState(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback,
                                const std::string& id) const
{
    auto manager = EmulatorManager::GetInstance();
    auto source = manager->GetEmulator(id);
    if (!source)
    {
        SendError(callback, HttpStatusCode::k404NotFound, "Not Found", "Emulator not found");
        return;
    }
    if (source->IsDestroying())
    {
        SendError(callback, HttpStatusCode::k503ServiceUnavailable, "Service Unavailable", "Emulator is shutting down");
        return;
    }

    auto body = req->getJsonObject();
    const bool hasTo = body && body->isMember("to") && (*body)["to"].isString() && !(*body)["to"].asString().empty();
    const bool hasModel =
        body && body->isMember("model") && (*body)["model"].isString() && !(*body)["model"].asString().empty();
    if (hasTo == hasModel)
    {
        SendError(callback, HttpStatusCode::k400BadRequest, "Bad Request",
                  "Request body needs exactly one of 'to' (an existing emulator id) or 'model' (a new instance)");
        return;
    }

    MachineStateTransfer::Options options;
    if (body->isMember("keep_frame_position"))
        options.keepFramePositionWhenTimingMatches = (*body)["keep_frame_position"].asBool();
    const bool checkOnly = body->isMember("check") && (*body)["check"].asBool();

    if (hasTo)
    {
        const std::string targetId = (*body)["to"].asString();
        auto target = manager->GetEmulator(targetId);
        if (!target)
        {
            SendError(callback, HttpStatusCode::k404NotFound, "Not Found", "Target emulator '" + targetId + "' not found");
            return;
        }
        if (target->IsDestroying())
        {
            SendError(callback, HttpStatusCode::k503ServiceUnavailable, "Service Unavailable",
                      "Target emulator is shutting down");
            return;
        }

        if (checkOnly)
        {
            if (!source->GetContext() || !target->GetContext())
            {
                SendError(callback, HttpStatusCode::k500InternalServerError, "Internal Error", "an instance has no context");
                return;
            }
            Json::Value ret = TransferReportJson(MachineStateTransfer::Check(*source->GetContext(), *target->GetContext()));
            ret["check"] = true;
            ret["source_id"] = id;
            ret["target_id"] = targetId;
            SendJson(callback, ret, HttpStatusCode::k200OK);
            return;
        }

        // TTD refuses this while recording: answer why instead of a bare failure
        if (const std::string refusal = target->RecordingGuard(ttd::TTDGuardedAction::SwitchModel); !refusal.empty())
        {
            SendError(callback, HttpStatusCode::k409Conflict, "Conflict", refusal);
            return;
        }

        const MachineStateTransfer::Report report = MachineStateTransfer::Transfer(*source, *target, options);
        Json::Value ret = TransferReportJson(report);
        ret["source_id"] = id;
        ret["target_id"] = targetId;
        ret["created"] = false;
        ret["state"] = stateToString(target->GetState());
        SendJson(callback, ret, report.ok ? HttpStatusCode::k200OK : HttpStatusCode::k422UnprocessableEntity);
        return;
    }

    if (checkOnly)
    {
        SendError(callback, HttpStatusCode::k400BadRequest, "Bad Request",
                  "'check' needs an existing target ('to'); a new instance is checked by creating it");
        return;
    }

    const std::string modelName = (*body)["model"].asString();
    const TMemModel* model = Config::FindModelByShortName(modelName);
    if (model && !Config::IsModelCreatable(*model))
    {
        SendError(callback, HttpStatusCode::k400BadRequest, "Bad Request",
                  "model '" + modelName + "' is not creatable on this build (port decoder or config folder missing)");
        return;
    }
    const uint32_t ramSize = body->isMember("ram_size") ? (*body)["ram_size"].asUInt() : 0;
    const std::string symbolicId = body->isMember("symbolic_id") ? (*body)["symbolic_id"].asString() : std::string();
    const bool start = body->isMember("start") ? (*body)["start"].asBool() : (source->IsRunning() && !source->IsPaused());

    MachineStateTransfer::Report report;
    std::shared_ptr<Emulator> target =
        MachineStateTransfer::TransferToNewInstance(*source, modelName, ramSize, report, options, symbolicId);
    Json::Value ret = TransferReportJson(report);
    ret["source_id"] = id;
    ret["created"] = static_cast<bool>(target);
    if (!target)
    {
        // Nothing was created. Refused before any item was examined (unknown model, RAM size the model
        // lacks, instance not creatable): the request is wrong. Otherwise the target cannot hold the state
        const bool badRequest = !model || report.items.empty();
        SendJson(callback, ret, badRequest ? HttpStatusCode::k400BadRequest : HttpStatusCode::k422UnprocessableEntity);
        return;
    }

    // Emulator::Start() runs the main loop on the calling thread; the server thread must return
    if (start)
        manager->StartEmulatorAsync(target->GetId());
    ret["target_id"] = target->GetId();
    ret["state"] = stateToString(target->GetState());
    const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*target);
    if (identity.Valid)
    {
        ret["model"] = identity.Model;
        ret["ram_kb"] = static_cast<Json::UInt>(identity.RamKb);
    }
    SendJson(callback, ret, HttpStatusCode::k201Created);
}

}  // namespace v1
}  // namespace api
