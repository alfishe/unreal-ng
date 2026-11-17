#include "loaders/snapshot/statetransfercontrol.h"

#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/runcontrol.h"

namespace
{

StateTransferReply Fail(int httpStatus, const std::string& error, const std::string& message)
{
    StateTransferReply reply;
    reply.httpStatus = httpStatus;
    reply.error = error;
    reply.message = message;
    reply.body["error"] = error;
    reply.body["message"] = message;
    return reply;
}

StateTransferReply WithReport(const MachineStateTransfer::Report& report)
{
    StateTransferReply reply;
    reply.hasReport = true;
    reply.report = report;
    reply.body = StateTransferControl::ReportValue(report);
    return reply;
}

}  // namespace

const char* StateTransferControl::ItemStatusName(MachineStateTransfer::ItemStatus status)
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

StateNode StateTransferControl::ReportValue(const MachineStateTransfer::Report& report)
{
    StateNode value = StateNode::Object();
    value["ok"] = report.ok;
    value["mode"] = report.clone ? "clone" : "cross-model";
    if (!report.reason.empty())
        value["reason"] = report.reason;
    StateNode items = StateNode::Array();
    for (const MachineStateTransfer::Item& item : report.items)
    {
        StateNode entry = StateNode::Object();
        entry["name"] = item.name;
        entry["status"] = ItemStatusName(item.status);
        if (!item.detail.empty())
            entry["detail"] = item.detail;
        items.push(std::move(entry));
    }
    value["items"] = std::move(items);
    value["copied"] = static_cast<uint64_t>(report.Count(MachineStateTransfer::ItemStatus::Copied));
    value["dropped"] = static_cast<uint64_t>(report.Count(MachineStateTransfer::ItemStatus::Dropped));
    value["refused"] = static_cast<uint64_t>(report.Count(MachineStateTransfer::ItemStatus::Refused));
    value["summary"] = report.ToString();
    return value;
}

StateTransferReply StateTransferControl::Execute(const StateTransferRequest& request)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    const std::string id = request.sourceId.empty() ? manager->GetSelectedEmulatorId() : request.sourceId;
    std::shared_ptr<Emulator> source = id.empty() ? nullptr : manager->GetEmulator(id);
    if (!source)
        return Fail(404, "Not Found", "Emulator not found");
    if (source->IsDestroying())
        return Fail(503, "Service Unavailable", "Emulator is shutting down");

    const bool hasTo = !request.to.empty();
    const bool hasModel = !request.model.empty();
    if (hasTo == hasModel)
        return Fail(400, "Bad Request",
                    "Request body needs exactly one of 'to' (an existing emulator id) or 'model' (a new instance)");

    MachineStateTransfer::Options options;
    if (request.keepFramePosition)
        options.keepFramePositionWhenTimingMatches = *request.keepFramePosition;

    if (hasTo)
    {
        const std::string& targetId = request.to;
        std::shared_ptr<Emulator> target = manager->GetEmulator(targetId);
        if (!target)
            return Fail(404, "Not Found", "Target emulator '" + targetId + "' not found");
        if (target->IsDestroying())
            return Fail(503, "Service Unavailable", "Target emulator is shutting down");

        if (request.check)
        {
            if (!source->GetContext() || !target->GetContext())
                return Fail(500, "Internal Error", "an instance has no context");
            StateTransferReply reply = WithReport(MachineStateTransfer::Check(*source->GetContext(), *target->GetContext()));
            reply.body["check"] = true;
            reply.body["source_id"] = id;
            reply.body["target_id"] = targetId;
            reply.target = target;
            return reply;
        }

        // The coordinator may refuse this: answer why instead of a bare failure, logged
        if (const std::string refusal = target->AskModelSwitch("state from " + id); !refusal.empty())
            return Fail(409, "Conflict", refusal);

        StateTransferReply reply = WithReport(MachineStateTransfer::Transfer(*source, *target, options));
        reply.httpStatus = reply.report.ok ? 200 : 422;
        reply.body["source_id"] = id;
        reply.body["target_id"] = targetId;
        reply.body["created"] = false;
        reply.body["state"] = execution::RunControl::StateName(target->GetState());
        reply.target = target;
        return reply;
    }

    if (request.check)
        return Fail(400, "Bad Request", "'check' needs an existing target ('to'); a new instance is checked by creating it");

    const TMemModel* model = Config::FindModelByShortName(request.model);
    if (model && !Config::IsModelCreatable(*model))
        return Fail(400, "Bad Request",
                    "model '" + request.model + "' is not creatable on this build (port decoder or config folder missing)");
    const bool start = request.start.value_or(source->IsRunning() && !source->IsPaused());

    MachineStateTransfer::Report report;
    std::shared_ptr<Emulator> target = MachineStateTransfer::TransferToNewInstance(
        *source, request.model, request.ramSizeKB, report, options, request.symbolicId);
    StateTransferReply reply = WithReport(report);
    reply.body["source_id"] = id;
    reply.body["created"] = static_cast<bool>(target);
    if (!target)
    {
        // Nothing was created. Refused before any item was examined (unknown model, RAM size the model
        // lacks, instance not creatable): the request is wrong. Otherwise the target cannot hold the state
        const bool badRequest = !model || report.items.empty();
        reply.httpStatus = badRequest ? 400 : 422;
        return reply;
    }

    // Emulator::Start() runs the main loop on the calling thread; the caller must return
    if (start)
        manager->StartEmulatorAsync(target->GetId());
    reply.httpStatus = 201;
    reply.target = target;
    reply.body["target_id"] = target->GetId();
    reply.body["state"] = execution::RunControl::StateName(target->GetState());
    const MachineIdentity identity = EmulatorManager::GetMachineIdentity(*target);
    if (identity.Valid)
    {
        reply.body["model"] = identity.Model;
        reply.body["ram_kb"] = identity.RamKb;
    }
    return reply;
}

std::string StateTransferReply::ToText() const
{
    if (!hasReport)
        return "Error: " + message + "\n";

    auto text = [this](const char* key) {
        const StateNode* node = body.find(key);
        return node != nullptr && node->kind == StateNode::Kind::String ? node->s : std::string();
    };
    const StateNode* created = body.find("created");
    const bool isNew = created != nullptr && created->kind == StateNode::Kind::Bool && created->b;
    const StateNode* checkNode = body.find("check");
    const bool isCheck = checkNode != nullptr && checkNode->kind == StateNode::Kind::Bool && checkNode->b;

    std::string out;
    const std::string route = text("source_id") + " -> " + (text("target_id").empty() ? "(none)" : text("target_id"));
    if (isCheck)
        out += std::string("Transfer check ") + route + (report.ok ? ": possible" : ": refused") + "\n";
    else if (!report.ok)
        out += "Transfer refused " + route + "\n";
    else
    {
        out += "Transferred " + route;
        if (isNew)
        {
            const StateNode* ramKb = body.find("ram_kb");
            out += " (new instance";
            if (!text("model").empty())
                out += ", " + text("model") + (ramKb != nullptr ? " " + std::to_string(ramKb->i) + "K" : std::string());
            out += ")";
        }
        out += "\n";
    }
    out += "mode: " + text("mode") + "; copied " + std::to_string(report.Count(MachineStateTransfer::ItemStatus::Copied)) +
           ", dropped " + std::to_string(report.Count(MachineStateTransfer::ItemStatus::Dropped)) + ", refused " +
           std::to_string(report.Count(MachineStateTransfer::ItemStatus::Refused));
    if (!text("state").empty())
        out += "; target " + text("state");
    out += "\n";
    out += report.ToString();
    return out;
}
