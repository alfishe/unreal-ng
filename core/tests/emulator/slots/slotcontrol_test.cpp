// SlotControl (core/src/emulator/slots/slotcontrol.h): the one layer behind every surface's slot commands (ZX-bus slots
// SL-7, docs/inprogress/2026-10-03-zx-bus-slots/tdd.md §2.5). The parity script of §2.5 runs here against the core
// types: list, catalog, matrix, a refused plug (409 with the plan), the plug with the flag, an options change, a
// remove - each reply compared field by field with the plan the core computes for the same request. The surfaces
// (WebAPI, CLI, Lua, Python, MCP through the WebAPI) pass their input through to this layer unchanged.
//
// A restart builds a second machine next to the first (config, ROMs): ~20-40 ms per applied change by nature

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/slots/slotchange.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/slots/slotmatrix.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"
#include "emulator/state/statenodejson.h"

namespace
{

using Lines = std::vector<std::pair<std::string, std::string>>;

std::function<void(CONFIG&)> SlotsOverride(const Lines& lines)
{
    return [lines](CONFIG& config) {
        SlotConfig slotConfig;
        ParseSlotsSection(lines, slotConfig);
        SlotManager::UseSlots(slotConfig, config);
    };
}

std::vector<std::string> FittedOf(Emulator& emulator)
{
    std::vector<std::string> fitted;
    for (const SlotManager::Slot& slot : emulator.GetContext()->pSlotManager->Current().entries)
    {
        if (!slot.entry.disabled)
            fitted.push_back(slot.entry.slot + " = " + slot.entry.card);
    }
    return fitted;
}

const StateNode& Field(const StateNode& node, const std::string& key)
{
    static const StateNode missing;
    const StateNode* found = node.find(key);
    return found != nullptr ? *found : missing;
}

class SlotControl_Test : public ::testing::Test
{
protected:
    SoundCardScope _everySound;
    std::vector<std::string> _ids;

    void TearDown() override
    {
        for (const std::string& id : _ids)
            EmulatorManager::GetInstance()->RemoveEmulator(id);
        MessageCenter::DisposeDefaultMessageCenter();
    }

    std::shared_ptr<Emulator> Create(const char* model, const Lines& lines)
    {
        std::string error;
        std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel(
            "", model, LoggerLevel::LogError, &error, SlotsOverride(lines));
        EXPECT_NE(emulator, nullptr) << error;
        if (emulator)
            _ids.push_back(emulator->GetId());
        return emulator;
    }

    SlotControlReply Run(SlotControlRequest request)
    {
        request.startWhenRunning = false;
        SlotControlReply reply = SlotControl::Execute(request);
        if (reply.emulator)
            _ids.push_back(reply.emulator->GetId());
        return reply;
    }

    static SlotControlRequest Request(const std::string& verb, const std::string& id, const std::string& slot = "",
                                      const std::string& card = "", const std::string& options = "")
    {
        SlotControlRequest request;
        request.verb = verb;
        request.emulatorId = id;
        request.slot = slot;
        request.card = card;
        request.options = options;
        return request;
    }
};

} // namespace

/// list = DeviceState::Slots of the instance, with its id; catalog = every card of the reference data with how it
/// fits this machine (the plan engine's dry run of a plug into the suggested slot); matrix = the generated tables
TEST_F(SlotControl_Test, QueriesServeTheCoreReports)
{
    std::shared_ptr<Emulator> emulator = Create("PENTAGON", {{"ay-socket", "tsfm"}, {"zxbus.1", "gs"}});
    ASSERT_NE(emulator, nullptr);
    const std::string id = emulator->GetId();

    const SlotControlReply list = Run(Request("list", id));
    ASSERT_TRUE(list.Ok()) << list.message;
    EXPECT_EQ(list.httpStatus, 200);
    const StateNode report = DeviceState::Slots(emulator->GetContext());
    EXPECT_EQ(StateNodeToJsonText(Field(list.body, "slots")), StateNodeToJsonText(Field(report, "slots")))
        << "the list is the core report, field by field";
    EXPECT_EQ(Field(list.body, "emulatorId").s, id);
    EXPECT_EQ(Field(list.ToValue(), "status").s, "ok");

    const SlotControlReply catalog = Run(Request("catalog", id));
    ASSERT_TRUE(catalog.Ok()) << catalog.message;
    const StateNode* multisound = nullptr;
    const StateNode* neogs = nullptr;
    for (const StateNode& card : Field(catalog.body, "cards").items)
    {
        if (Field(card, "id").s == "multisound")
            multisound = &card;
        if (Field(card, "id").s == "neogs")
            neogs = &card;
    }
    ASSERT_NE(multisound, nullptr);
    ASSERT_NE(neogs, nullptr);
    const StateNode& here = Field(*multisound, "thisMachine");
    EXPECT_EQ(Field(here, "outcome").s, "needs-replace") << "it would push out the TSFM and the GS";
    EXPECT_EQ(Field(here, "removes").size(), 2u);
    EXPECT_EQ(Field(here, "fit").s, "real");
    EXPECT_TRUE(Field(*multisound, "emulated").b);
    std::vector<std::string> options;
    for (const StateNode& option : Field(*multisound, "options").items)
        options.push_back(Field(option, "name").s + "=" + Field(option, "default").s);
    EXPECT_EQ(options, (std::vector<std::string>{"dip=ym,saa,gs,sd", "gsRam=1m", "ctrlMask=pro"}));
    EXPECT_EQ(Field(Field(*neogs, "thisMachine"), "outcome").s, "needs-replace") << "one GS card per machine (D1)";

    SlotControlRequest one = Request("matrix", "");
    one.table = "cards";
    const SlotControlReply matrix = Run(one);
    ASSERT_TRUE(matrix.Ok()) << matrix.message;
    ASSERT_EQ(Field(matrix.body, "tables").size(), 1u);
    EXPECT_EQ(Field(Field(matrix.body, "tables").items[0], "markdown").s, slots::RenderMatrixTable("cards"));
    one.table = "nope";
    EXPECT_EQ(Run(one).httpStatus, 400);
}

/// The §2.5 script: a displacing plug refused (409, the plan lists every card it would remove with its undo), the
/// same plug with replaceIfIncompatible applied by a restart (new id, the reply's plan equal to the core's), an
/// options change, a remove; each reply's plan compared with SlotManager::PlanChange for the same request
TEST_F(SlotControl_Test, ParityScriptPlugRefusedReplacedOptionsRemove)
{
    std::shared_ptr<Emulator> emulator = Create("PENTAGON", {{"ay-socket", "tsfm"}, {"zxbus.1", "gs"}});
    ASSERT_NE(emulator, nullptr);
    const std::string firstId = emulator->GetId();

    // The core's own plan for the request
    slots::SlotRequest core;
    core.op = slots::SlotRequest::Op::Plug;
    core.slot = "zxbus.next";
    core.card = "multisound";
    const SlotManager::ChangePlan expected = emulator->GetContext()->pSlotManager->PlanChange(core);
    emulator.reset();

    const SlotControlReply refused = Run(Request("plug", firstId, "zxbus.next", "multisound"));
    EXPECT_EQ(refused.status, "refused");
    EXPECT_EQ(refused.httpStatus, 409);
    EXPECT_EQ(refused.message, expected.refusal);
    const StateNode& plan = Field(refused.body, "plan");
    EXPECT_EQ(StateNodeToJsonText(plan), StateNodeToJsonText(SlotControl::PlanValue(expected)))
        << "the reply's plan is the core plan, field by field";
    EXPECT_TRUE(Field(plan, "needsReplaceIfIncompatible").b);
    std::vector<std::string> undo;
    for (const StateNode& removed : Field(plan, "removed").items)
        undo.push_back(Field(removed, "undo").s);
    EXPECT_EQ(undo, (std::vector<std::string>{"plug zxbus.1 gs ram=128k rom=1.05", "plug ay-socket tsfm"}));
    EXPECT_FALSE(Field(Field(refused.body, "restart"), "restarted").b);
    EXPECT_TRUE(EmulatorManager::GetInstance()->HasEmulator(firstId)) << "refused: nothing changed";

    SlotControlRequest replace = Request("plug", firstId, "zxbus.next", "multisound", "gsRam=2m");
    replace.replaceIfIncompatible = true;
    const SlotControlReply applied = Run(replace);
    ASSERT_EQ(applied.status, "applied") << applied.message;
    ASSERT_NE(applied.emulator, nullptr);
    const std::string secondId = applied.emulator->GetId();
    EXPECT_NE(secondId, firstId);
    const StateNode& restart = Field(applied.body, "restart");
    EXPECT_TRUE(Field(restart, "restarted").b);
    EXPECT_EQ(Field(restart, "previousEmulatorId").s, firstId);
    EXPECT_EQ(Field(restart, "emulatorId").s, secondId);
    EXPECT_EQ(Field(applied.body, "emulatorId").s, secondId);
    EXPECT_EQ(Field(Field(applied.body, "plan"), "options").s, "dip=ym,saa,gs,sd gsRam=2m ctrlMask=pro");
    EXPECT_FALSE(EmulatorManager::GetInstance()->HasEmulator(firstId));
    EXPECT_EQ(FittedOf(*applied.emulator), (std::vector<std::string>{"zxbus.2 = multisound"}));

    // Options: merged over the slot's, the plan as the core computes it
    const SlotControlReply options = Run(Request("set", secondId, "zxbus.2", "", "ctrlMask=classic"));
    ASSERT_EQ(options.status, "applied") << options.message;
    const std::string thirdId = options.emulator->GetId();
    const SlotManager::Result after = options.emulator->GetContext()->pSlotManager->Snapshot();
    ASSERT_NE(after.FindSlot("zxbus.2"), nullptr);
    static const slots::SlotPlanner planner;
    EXPECT_EQ(slots::FormatCardOptions(*planner.FindCard("multisound"), after.FindSlot("zxbus.2")->entry.options),
              "dip=ym,saa,gs,sd gsRam=2m ctrlMask=classic");

    // Remove, dry run first: the plan only
    SlotControlRequest remove = Request("remove", thirdId, "zxbus.2");
    remove.dryRun = true;
    const SlotControlReply dry = Run(remove);
    EXPECT_EQ(dry.status, "dry-run") << dry.message;
    EXPECT_EQ(dry.emulator, nullptr);
    remove.dryRun = false;
    const SlotControlReply removed = Run(remove);
    ASSERT_EQ(removed.status, "applied") << removed.message;
    EXPECT_TRUE(FittedOf(*removed.emulator).empty());
    EXPECT_NE(removed.ToText().find("restarted: emulator " + thirdId + " -> " + removed.emulator->GetId()),
              std::string::npos)
        << removed.ToText();
}

/// Q10: the General Sound personality on every surface is a slot replace applied by a restart; the GS names of the
/// old switch (z80 / lw / ngs) and the card ids are accepted; a machine without a GS card refuses it
TEST_F(SlotControl_Test, GsPersonalityIsASlotReplace)
{
    std::shared_ptr<Emulator> emulator = Create("PENTAGON", {{"zxbus.1", "gs"}});
    ASSERT_NE(emulator, nullptr);
    const std::string id = emulator->GetId();
    emulator.reset();

    const SlotControlReply switched = Run(Request("gs", id, "", "ngs"));
    ASSERT_EQ(switched.status, "applied") << switched.message;
    EXPECT_EQ(FittedOf(*switched.emulator), (std::vector<std::string>{"zxbus.1 = neogs"}));
    EXPECT_EQ(switched.emulator->GetContext()->config.sound.gsTypeKind, GSTypeKind::NGS);

    SlotControlRequest dry = Request("gs", switched.emulator->GetId(), "", "gs-lw");
    dry.dryRun = true;
    EXPECT_EQ(Run(dry).status, "dry-run");
    EXPECT_EQ(Run(Request("gs", switched.emulator->GetId(), "", "bass-guitar")).httpStatus, 400);

    std::shared_ptr<Emulator> none = Create("PENTAGON", {{"ay-socket", "ay"}});
    ASSERT_NE(none, nullptr);
    const SlotControlReply refused = Run(Request("gs", none->GetId(), "", "gs"));
    EXPECT_EQ(refused.status, "refused");
    EXPECT_NE(refused.message.find("no General Sound card is fitted"), std::string::npos) << refused.message;
}

/// R-OP-7 through the surfaces' layer: status recording, 409, the session named
TEST_F(SlotControl_Test, RecordingRefusesChanges)
{
    std::shared_ptr<Emulator> emulator = Create("PENTAGON", {{"zxbus.1", "gs"}});
    ASSERT_NE(emulator, nullptr);
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    emulator->GetContext()->pMemory->UpdateFeatureCache();
    ttd::TimeTravelManager* ttd = emulator->GetContext()->pTimeTravelManager;
    ASSERT_NE(ttd, nullptr);
    ASSERT_TRUE(ttd->StartRecording());

    const SlotControlReply refused = Run(Request("plug", emulator->GetId(), "zxbus.next", "zxnetusb"));
    EXPECT_EQ(refused.status, "recording");
    EXPECT_EQ(refused.httpStatus, 409);
    EXPECT_NE(refused.message.find("TTD is recording session #1"), std::string::npos) << refused.message;
    EXPECT_TRUE(Field(Field(refused.body, "plan"), "recording").b);
    ttd->StopRecording();
}

/// Malformed requests are 400 / 404 with the reason; nothing is planned
TEST_F(SlotControl_Test, BadRequestsAreRefusedBeforeAnyPlan)
{
    std::shared_ptr<Emulator> emulator = Create("PENTAGON", {{"zxbus.1", "gs"}});
    ASSERT_NE(emulator, nullptr);
    const std::string id = emulator->GetId();

    EXPECT_EQ(Run(Request("dance", id)).httpStatus, 400);
    EXPECT_EQ(Run(Request("plug", id, "zxbus.next", "")).httpStatus, 400);
    EXPECT_EQ(Run(Request("plug", id, "zxbus.next", "no-such-card")).httpStatus, 400);
    const SlotControlReply option = Run(Request("plug", id, "zxbus.next", "multisound", "dip=piano"));
    EXPECT_EQ(option.httpStatus, 400);
    EXPECT_NE(option.message.find("dip=piano"), std::string::npos) << option.message;
    EXPECT_EQ(Run(Request("set", id, "zxbus.7", "", "ram=128k")).httpStatus, 400);
    SlotControlRequest media = Request("remove", id, "zxbus.1");
    media.media = "shred";
    EXPECT_EQ(Run(media).httpStatus, 400);
    EXPECT_EQ(Run(Request("list", "no-such-emulator")).httpStatus, 404);
    EXPECT_EQ(Run(Request("list", "no-such-emulator")).status, "no-machine");
}

/// The create-time `"slots": {...}` form: the [SLOTS] key / value pairs become the new machine's slot set, replacing
/// its INI's; a malformed line is an error with the line
TEST_F(SlotControl_Test, CreateOverrideFitsTheGivenSet)
{
    std::function<void(CONFIG&)> override;
    std::string error;
    ASSERT_TRUE(SlotControl::CreateOverride({{"ay-socket", "none"}, {"zxbus.1", "multisound"}, {"zxbus.1.dip", "ym,saa"}},
                                            override, error))
        << error;
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("", "PENTAGON", LoggerLevel::LogError, &error, override);
    ASSERT_NE(emulator, nullptr) << error;
    _ids.push_back(emulator->GetId());
    EXPECT_EQ(FittedOf(*emulator), (std::vector<std::string>{"ay-socket = none", "zxbus.1 = multisound"}));
    EXPECT_EQ(emulator->GetContext()->pSlotManager->Current().FindSlot("zxbus.1")->source, "create slots zxbus.1");

    EXPECT_FALSE(SlotControl::CreateOverride({{"zxbus", "gs"}}, override, error));
    EXPECT_NE(error.find("zxbus"), std::string::npos) << error;
}

/// Undo (the Qt window, SlotChangeRequest::slotSet): the set from before a change put back by a restart
TEST_F(SlotControl_Test, UndoPutsThePreviousSetBack)
{
    std::shared_ptr<Emulator> emulator = Create("PENTAGON", {{"ay-socket", "tsfm"}, {"zxbus.1", "gs"}});
    ASSERT_NE(emulator, nullptr);
    const SlotConfig before = SlotManager::ConfigOf(emulator->GetContext()->pSlotManager->Snapshot());
    const std::string id = emulator->GetId();
    emulator.reset();

    SlotControlRequest replace = Request("plug", id, "zxbus.next", "multisound");
    replace.replaceIfIncompatible = true;
    const SlotControlReply applied = Run(replace);
    ASSERT_EQ(applied.status, "applied") << applied.message;

    SlotChangeRequest undo;
    undo.emulatorId = applied.emulator->GetId();
    undo.slotSet = before;
    const SlotChangeResult undone = SlotChange::Run(undo);
    ASSERT_TRUE(undone.Applied()) << undone.message;
    _ids.push_back(undone.emulator->GetId());
    EXPECT_EQ(FittedOf(*undone.emulator), (std::vector<std::string>{"ay-socket = tsfm", "zxbus.1 = gs"}));

    // A set the machine would not be created with is refused before anything restarts (Q8)
    SlotConfig conflict = before;
    conflict.entries.push_back({"zxbus.2", "neogs", "", "", false, "test"});
    SlotChangeRequest bad;
    bad.emulatorId = undone.emulator->GetId();
    bad.slotSet = conflict;
    const SlotChangeResult refused = SlotChange::Run(bad);
    EXPECT_EQ(refused.status, SlotChangeStatus::Refused);
    EXPECT_NE(refused.message.find("D1"), std::string::npos) << refused.message;
}

/// The model switch's slot carry as every surface shows it
TEST_F(SlotControl_Test, CarryValueListsKeptAndDropped)
{
    SlotManager::CarryReport carry;
    carry.carried = true;
    carry.kept = {"zxbus.1 = gs"};
    carry.dropped.push_back({"zxbus.2", "multisound", "the bus has no IORQGE"});
    carry.lines = {"zxbus.1 = gs", "zxbus.2 = multisound not carried: the bus has no IORQGE"};
    const StateNode value = SlotControl::CarryValue(carry);
    EXPECT_EQ(StateNodeToJsonText(value),
              R"({"carried":true,"kept":["zxbus.1 = gs"],"dropped":[{"slot":"zxbus.2","card":"multisound",)"
              R"("reason":"the bus has no IORQGE"}],"lines":["zxbus.1 = gs","zxbus.2 = multisound not carried: )"
              R"(the bus has no IORQGE"]})");
}
