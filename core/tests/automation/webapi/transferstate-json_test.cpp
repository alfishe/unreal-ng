/// @file transferstate-json_test.cpp
/// @brief POST /api/v1/emulator/{id}/snapshot/transfer answers byte for byte what it answered before the verb moved
/// into StateTransferControl (model what-if W0b): the reference below is the handler's former JSON (TransferReportJson
/// and the fields it added), built from the same report; the route now serializes StateTransferReply::body.

#include <gtest/gtest.h>
#include <json/json.h>

#include <memory>
#include <string>

#include "../../../automation/webapi/src/common/statenode_json.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/runcontrol.h"
#include "loaders/snapshot/statetransfercontrol.h"

namespace
{
/// The WebAPI's report JSON before W0b, verbatim
Json::Value FormerReportJson(const MachineStateTransfer::Report& report)
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
        entry["status"] = StateTransferControl::ItemStatusName(item.status);
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

std::string Wire(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;   // drogon's newHttpJsonResponse writes with the default builder
    return Json::writeString(builder, value);
}

class TransferStateJson_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = EmulatorManager::GetInstance();

    void TearDown() override
    {
        for (const std::string& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }
};
}  // namespace

TEST_F(TransferStateJson_Test, TheCheckTheTransferAndTheNewInstanceAnswerAsBefore)
{
    auto source = _manager->CreateEmulatorWithModelAndRAM("transfer-json", "128k", 128, LoggerLevel::LogError);
    auto target = _manager->CreateEmulatorWithModelAndRAM("transfer-json", "PENTAGON", 128, LoggerLevel::LogError);
    ASSERT_TRUE(source && target);

    StateTransferRequest request;
    request.sourceId = source->GetId();
    request.to = target->GetId();
    request.check = true;
    StateTransferReply reply = StateTransferControl::Execute(request);
    ASSERT_TRUE(reply.hasReport);
    Json::Value former = FormerReportJson(reply.report);
    former["check"] = true;
    former["source_id"] = source->GetId();
    former["target_id"] = target->GetId();
    EXPECT_EQ(Wire(StateNodeToJson(reply.body)), Wire(former));

    request.check = false;
    reply = StateTransferControl::Execute(request);
    ASSERT_TRUE(reply.Ok()) << reply.ToText();
    former = FormerReportJson(reply.report);
    former["source_id"] = source->GetId();
    former["target_id"] = target->GetId();
    former["created"] = false;
    former["state"] = execution::RunControl::StateName(target->GetState());
    EXPECT_EQ(Wire(StateNodeToJson(reply.body)), Wire(former));

    StateTransferRequest create;
    create.sourceId = source->GetId();
    create.model = "PENTAGON";
    create.ramSizeKB = 512;
    reply = StateTransferControl::Execute(create);
    ASSERT_TRUE(reply.Ok() && reply.target) << reply.ToText();
    former = FormerReportJson(reply.report);
    former["source_id"] = source->GetId();
    former["created"] = true;
    former["target_id"] = reply.target->GetId();
    former["state"] = execution::RunControl::StateName(reply.target->GetState());
    former["model"] = "PENTAGON";
    former["ram_kb"] = static_cast<Json::UInt>(512);
    EXPECT_EQ(Wire(StateNodeToJson(reply.body)), Wire(former));

    create.model = "";
    reply = StateTransferControl::Execute(create);
    Json::Value error;
    error["error"] = "Bad Request";
    error["message"] = "Request body needs exactly one of 'to' (an existing emulator id) or 'model' (a new instance)";
    EXPECT_EQ(reply.httpStatus, 400);
    EXPECT_EQ(Wire(StateNodeToJson(reply.body)), Wire(error));
}
