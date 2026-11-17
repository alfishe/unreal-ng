/// @file statetransfercontrol_test.cpp
/// @brief StateTransferControl (model what-if W0b): the one transfer verb every surface calls - the WebAPI, MCP through
/// it, the CLI, Lua, Python and the Qt menu. The answers (status, body, text) for each way of naming a target, the
/// check-only mode, a refusal, and the requests that never reach a report. The mechanism itself is
/// MachineStateTransfer_Test's.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/ports/portdecoder.h"
#include "loaders/snapshot/statetransfercontrol.h"

namespace
{
const StateNode* Field(const StateTransferReply& reply, const char* key) { return reply.body.find(key); }

std::string Text(const StateTransferReply& reply, const char* key)
{
    const StateNode* node = Field(reply, key);
    return node != nullptr && node->kind == StateNode::Kind::String ? node->s : std::string();
}
}  // namespace

class StateTransferControl_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = EmulatorManager::GetInstance();

    void TearDown() override
    {
        // Every instance goes, the ones a transfer created included
        for (const std::string& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }

    std::shared_ptr<Emulator> Create(const char* model, uint32_t ramKb)
    {
        std::shared_ptr<Emulator> emulator =
            _manager->CreateEmulatorWithModelAndRAM("transfer-control", model, ramKb, LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr) << model;
        return emulator;
    }

    static StateTransferRequest To(const std::shared_ptr<Emulator>& source, const std::shared_ptr<Emulator>& target)
    {
        StateTransferRequest request;
        request.sourceId = source->GetId();
        request.to = target->GetId();
        return request;
    }
};

TEST_F(StateTransferControl_Test, ARequestWithoutExactlyOneTargetIsABadRequest)
{
    auto source = Create("48K", 48);
    ASSERT_TRUE(source);
    StateTransferRequest neither;
    neither.sourceId = source->GetId();
    StateTransferRequest both = neither;
    both.to = source->GetId();
    both.model = "128k";
    for (const StateTransferRequest& request : {neither, both})
    {
        const StateTransferReply reply = StateTransferControl::Execute(request);
        EXPECT_EQ(reply.httpStatus, 400);
        EXPECT_FALSE(reply.hasReport);
        EXPECT_EQ(Text(reply, "error"), "Bad Request");
        EXPECT_NE(Text(reply, "message").find("exactly one of 'to'"), std::string::npos) << Text(reply, "message");
        EXPECT_EQ(reply.body.size(), 2u) << "{error, message} only, as the WebAPI answered";
        EXPECT_EQ(reply.ToText().rfind("Error: ", 0), 0u) << reply.ToText();
    }

    StateTransferRequest checkNew = neither;
    checkNew.model = "128k";
    checkNew.check = true;
    const StateTransferReply reply = StateTransferControl::Execute(checkNew);
    EXPECT_EQ(reply.httpStatus, 400);
    EXPECT_NE(reply.message.find("'check' needs an existing target"), std::string::npos) << reply.message;
}

TEST_F(StateTransferControl_Test, AnUnknownSourceOrTargetIsNotFound)
{
    auto source = Create("48K", 48);
    ASSERT_TRUE(source);
    StateTransferRequest request;
    request.sourceId = "no-such-instance";
    request.to = source->GetId();
    StateTransferReply reply = StateTransferControl::Execute(request);
    EXPECT_EQ(reply.httpStatus, 404);
    EXPECT_EQ(reply.message, "Emulator not found");

    request.sourceId = source->GetId();
    request.to = "no-such-target";
    reply = StateTransferControl::Execute(request);
    EXPECT_EQ(reply.httpStatus, 404);
    EXPECT_EQ(reply.message, "Target emulator 'no-such-target' not found");
}

TEST_F(StateTransferControl_Test, ACheckReportsWhatWouldMoveAndChangesNothing)
{
    auto source = Create("128k", 128);
    auto target = Create("PENTAGON", 128);
    ASSERT_TRUE(source && target);
    source->GetContext()->pCore->GetZ80()->pc = 0x8123;
    const uint16_t pcBefore = target->GetContext()->pCore->GetZ80()->pc;

    StateTransferRequest request = To(source, target);
    request.check = true;
    const StateTransferReply reply = StateTransferControl::Execute(request);
    ASSERT_TRUE(reply.hasReport) << reply.message;
    EXPECT_EQ(reply.httpStatus, 200);
    EXPECT_TRUE(reply.Ok()) << reply.report.ToString();
    EXPECT_EQ(Text(reply, "mode"), "cross-model");
    ASSERT_NE(Field(reply, "check"), nullptr);
    EXPECT_TRUE(Field(reply, "check")->b);
    EXPECT_EQ(Text(reply, "source_id"), source->GetId());
    EXPECT_EQ(Text(reply, "target_id"), target->GetId());
    EXPECT_EQ(Field(reply, "created"), nullptr) << "a check names no creation";
    EXPECT_GT(Field(reply, "copied")->i, 0);
    EXPECT_EQ(Field(reply, "items")->size(), reply.report.items.size());
    EXPECT_EQ(Text(reply, "summary"), reply.report.ToString());
    EXPECT_EQ(target->GetContext()->pCore->GetZ80()->pc, pcBefore) << "nothing written";
    EXPECT_EQ(reply.ToText().rfind("Transfer check " + source->GetId() + " -> " + target->GetId() + ": possible", 0), 0u)
        << reply.ToText();
}

TEST_F(StateTransferControl_Test, ATransferToAnOpenInstanceOfTheSameModelIsAClone)
{
    auto source = Create("128k", 128);
    auto target = Create("128k", 128);
    ASSERT_TRUE(source && target);
    source->GetContext()->pCore->GetZ80()->pc = 0x8123;

    const StateTransferReply reply = StateTransferControl::Execute(To(source, target));
    ASSERT_TRUE(reply.Ok()) << reply.ToText();
    EXPECT_EQ(reply.httpStatus, 200);
    EXPECT_EQ(Text(reply, "mode"), "clone");
    EXPECT_FALSE(Field(reply, "created")->b);
    EXPECT_EQ(Text(reply, "state"), "initialized");
    EXPECT_EQ(reply.target, target);
    EXPECT_EQ(target->GetContext()->pCore->GetZ80()->pc, 0x8123);
    EXPECT_NE(reply.ToText().find("mode: clone; copied "), std::string::npos) << reply.ToText();
}

TEST_F(StateTransferControl_Test, ANewInstanceOfAnotherModelGetsTheStateAndItsIdentity)
{
    auto source = Create("128k", 128);
    ASSERT_TRUE(source);
    source->GetContext()->pCore->GetZ80()->pc = 0x8123;

    StateTransferRequest request;
    request.sourceId = source->GetId();
    request.model = "PENTAGON";
    request.ramSizeKB = 512;
    request.symbolicId = "w0b-fork";
    const StateTransferReply reply = StateTransferControl::Execute(request);
    ASSERT_TRUE(reply.Ok()) << reply.ToText();
    EXPECT_EQ(reply.httpStatus, 201);
    ASSERT_TRUE(reply.target);
    EXPECT_TRUE(Field(reply, "created")->b);
    EXPECT_EQ(Text(reply, "target_id"), reply.target->GetId());
    EXPECT_EQ(Text(reply, "model"), "PENTAGON");
    EXPECT_EQ(Field(reply, "ram_kb")->i, 512);
    EXPECT_EQ(Text(reply, "state"), "initialized") << "the source is not running: the new one is not started";
    EXPECT_EQ(reply.target->GetSymbolicId(), "w0b-fork");
    EXPECT_EQ(reply.target->GetContext()->pCore->GetZ80()->pc, 0x8123);
    EXPECT_NE(reply.ToText().find("(new instance, PENTAGON 512K)"), std::string::npos) << reply.ToText();
}

TEST_F(StateTransferControl_Test, AStateTheTargetCannotHoldIsRefusedWithTheReportAndLeavesNothingBehind)
{
    auto source = Create("PLUS3", 128);
    auto target = Create("128k", 128);
    ASSERT_TRUE(source && target);
    source->GetContext()->pPortDecoder->DecodePortOut(0x1FFD, 0x01, 0);   // +3 all-RAM: no 128K equivalent

    StateTransferReply reply = StateTransferControl::Execute(To(source, target));
    ASSERT_TRUE(reply.hasReport);
    EXPECT_EQ(reply.httpStatus, 422);
    EXPECT_FALSE(reply.Ok());
    EXPECT_FALSE(Text(reply, "reason").empty());
    EXPECT_GT(Field(reply, "refused")->i, 0);
    EXPECT_EQ(reply.ToText().rfind("Transfer refused ", 0), 0u) << reply.ToText();

    const size_t instances = _manager->GetEmulatorIds().size();
    StateTransferRequest request;
    request.sourceId = source->GetId();
    request.model = "128k";
    reply = StateTransferControl::Execute(request);
    EXPECT_EQ(reply.httpStatus, 422) << reply.ToText();
    EXPECT_FALSE(Field(reply, "created")->b);
    EXPECT_EQ(Field(reply, "target_id"), nullptr);
    EXPECT_EQ(_manager->GetEmulatorIds().size(), instances) << "the refused new instance is gone";

    request.model = "NO-SUCH-MODEL";
    reply = StateTransferControl::Execute(request);
    EXPECT_EQ(reply.httpStatus, 400) << "refused before any item: the request is wrong";
    EXPECT_TRUE(reply.hasReport);
}
