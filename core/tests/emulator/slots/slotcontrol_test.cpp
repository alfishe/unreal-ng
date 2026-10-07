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
#include "debugger/ttd/engine/ttdconfigfingerprint.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdconfigcapture.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/atm2kbc.h"
#include "emulator/io/sprinter/isa/isaslotconfig.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/comport.h"
#include "emulator/io/serial/uart16550.h"
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

// region <Network settings (owner decision Q11)>

namespace
{

SlotControlRequest NetworkRequest(const std::string& id, const Lines& settings)
{
    SlotControlRequest request;
    request.verb = "network";
    request.emulatorId = id;
    request.settings = settings;
    return request;
}

/// The slot fields of a machine's TTD configuration fingerprint (`slots.*`)
std::vector<std::string> SlotFingerprintDiff(const EmulatorContext& a, const EmulatorContext& b)
{
    std::vector<std::string> names;
    for (const ttd::TTDFingerprintDiff& d :
         ttd::Compare(ttd::CaptureConfigFingerprint(a, 0), ttd::CaptureConfigFingerprint(b, 0)))
    {
        if (d.field.rfind("slots.", 0) == 0)
            names.push_back(d.field);
    }
    return names;
}

} // namespace

/// A network `card` change is a slot change applied by a restart: the removes and plugs planned as one change, the
/// other keys applied to the restarted machine; afterwards the slot report, the plan, the live devices and the TTD
/// fingerprint agree with each other and with a machine created with that slot set
TEST_F(SlotControl_Test, NetworkCardChangeIsASlotChange)
{
    std::shared_ptr<Emulator> emulator = Create("PENTAGON", {{"ay-socket", "ay"}});
    ASSERT_NE(emulator, nullptr);
    const std::string id = emulator->GetId();
    emulator.reset();

    const SlotControlReply both = Run(NetworkRequest(
        id, {{"card", "zxnetusb,zxwifi"}, {"host_access", "off"}, {"hosts", "a.test=10.0.2.77"}, {"zx_wifi", "loopback"}}));
    ASSERT_EQ(both.status, "applied") << both.message;
    ASSERT_NE(both.emulator, nullptr);
    EXPECT_NE(both.emulator->GetId(), id) << "a restart: a new emulator id";
    EXPECT_EQ(Field(both.body, "op").s, "network");
    EXPECT_TRUE(Field(Field(both.body, "network"), "cardChange").b);
    EXPECT_TRUE(Field(Field(both.body, "network"), "settingsApplied").b);
    EXPECT_TRUE(Field(Field(both.body, "restart"), "restarted").b);

    EmulatorContext* context = both.emulator->GetContext();
    const std::vector<std::string> fitted{"ay-socket = ay", "zxbus.1 = zxnetusb", "zxbus.2 = zx-wifi"};
    EXPECT_EQ(FittedOf(*both.emulator), fitted) << "the plan";
    std::vector<std::string> reported;
    const StateNode report = DeviceState::Slots(context);
    for (const StateNode& slot : Field(report, "slots").items)
        reported.push_back(Field(slot, "slot").s + " = " + Field(slot, "card").s);
    EXPECT_EQ(reported, fitted) << "the slot report";
    EXPECT_EQ(context->config.network.card & NetworkManager::kZxBusCards, networkspec::kCardZxNetUsb | networkspec::kCardZxWifi);
    ASSERT_NE(context->pZxNetUsb, nullptr) << "the live devices";
    ASSERT_NE(context->pComPort, nullptr);
    EXPECT_EQ(context->pComPort->Uart().GetParams().flavor, Uart16550::Flavor::Chip16550);
    ASSERT_NE(context->pVirtualNetwork, nullptr);
    EXPECT_EQ(context->pVirtualNetwork->Host(), nullptr) << "host_access=off went to the restarted machine";
    EXPECT_EQ(context->pVirtualNetwork->Config().hosts.at("a.test"), NetIp(10, 0, 2, 77));

    // The TTD fingerprint's slot fields equal those of a machine created with this slot set
    const SlotConfig set = SlotManager::ConfigOf(context->pSlotManager->Snapshot());
    std::string error;
    std::shared_ptr<Emulator> fresh = EmulatorManager::GetInstance()->CreateEmulatorWithModel(
        "", "PENTAGON", LoggerLevel::LogError, &error, [set](CONFIG& config) { SlotManager::UseSlots(set, config); });
    ASSERT_NE(fresh, nullptr) << error;
    _ids.push_back(fresh->GetId());
    EXPECT_TRUE(SlotFingerprintDiff(*context, *fresh->GetContext()).empty());
    EXPECT_NE(ttd::CaptureConfigFingerprint(*context, 0).Find("slots.zxbus.2"), nullptr);

    // One card out, the other stays: a remove; then the swap (a remove and a plug, one restart)
    const SlotControlReply wifi = Run(NetworkRequest(both.emulator->GetId(), {{"card", "zxwifi"}}));
    ASSERT_EQ(wifi.status, "applied") << wifi.message;
    EXPECT_EQ(FittedOf(*wifi.emulator), (std::vector<std::string>{"ay-socket = ay", "zxbus.2 = zx-wifi"}));
    EXPECT_FALSE(Field(Field(wifi.body, "network"), "settingsApplied").b) << "the card was the only setting";
    const SlotControlReply swap = Run(NetworkRequest(wifi.emulator->GetId(), {{"card", "zxnetusb"}}));
    ASSERT_EQ(swap.status, "applied") << swap.message;
    EXPECT_EQ(FittedOf(*swap.emulator), (std::vector<std::string>{"ay-socket = ay", "zxbus.1 = zxnetusb"}));
    EXPECT_EQ(Field(swap.body, "card").s, "ZXNETUSB");
    const StateNode& removed = Field(Field(swap.body, "plan"), "removed");
    ASSERT_EQ(removed.size(), 1u) << swap.ToText();
    EXPECT_EQ(Field(removed.items[0], "card").s, "zx-wifi");
    EXPECT_EQ(swap.emulator->GetContext()->pComPort, nullptr);
    EXPECT_NE(swap.emulator->GetContext()->pZxNetUsb, nullptr);
}

/// No card change: the settings apply to the running machine (status accepted, no restart); dry runs, bad keys, the
/// replace flag for an unrealistic fit and the TTD recording behave as for every slot change
TEST_F(SlotControl_Test, NetworkSettingsWithoutCardChangeStayInPlace)
{
    std::shared_ptr<Emulator> emulator = Create("PENTAGON", {{"zxbus.1", "zxnetusb"}});
    ASSERT_NE(emulator, nullptr);
    const std::string id = emulator->GetId();

    const SlotControlReply same = Run(NetworkRequest(id, {{"card", "zxnetusb"}, {"host_access", "off"}}));
    EXPECT_EQ(same.status, "accepted") << same.message;
    EXPECT_TRUE(same.Ok());
    EXPECT_EQ(same.emulator, nullptr) << "no restart";
    EXPECT_FALSE(Field(Field(same.body, "network"), "cardChange").b);
    EXPECT_EQ(emulator->GetContext()->config.network.hostAccess, 0);

    SlotControlRequest dry = NetworkRequest(id, {{"card", "none"}});
    dry.dryRun = true;
    const SlotControlReply plan = Run(dry);
    EXPECT_EQ(plan.status, "dry-run") << plan.message;
    EXPECT_EQ(Field(Field(Field(plan.body, "plan"), "removed").items.at(0), "card").s, "zxnetusb");
    EXPECT_NE(emulator->GetContext()->pZxNetUsb, nullptr) << "a dry run changes nothing";

    EXPECT_EQ(Run(NetworkRequest(id, {{"card", "wifi"}})).httpStatus, 400);
    EXPECT_EQ(Run(NetworkRequest(id, {{"connect_timeout_ms", "5"}})).httpStatus, 400) << "checked before any restart";
    EXPECT_EQ(Run(NetworkRequest(id, {})).httpStatus, 400);

    // A ZX-bus card on the 48K edge needs the adapter: an unrealistic fit, refused without the flag (Q1 / Q5)
    std::shared_ptr<Emulator> small = Create("48K", {});
    ASSERT_NE(small, nullptr);
    SlotControlRequest edge = NetworkRequest(small->GetId(), {{"card", "zxnetusb"}});
    edge.dryRun = true;
    const SlotControlReply refused = Run(edge);
    EXPECT_EQ(refused.status, "refused") << refused.message;
    EXPECT_NE(refused.message.find("needs replaceIfIncompatible"), std::string::npos) << refused.message;
    edge.replaceIfIncompatible = true;
    EXPECT_EQ(Run(edge).status, "dry-run");

    // R-OP-7: refused while TTD records, the card change and the in-place settings alike
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    emulator->GetContext()->pMemory->UpdateFeatureCache();
    ttd::TimeTravelManager* ttd = emulator->GetContext()->pTimeTravelManager;
    ASSERT_NE(ttd, nullptr);
    ASSERT_TRUE(ttd->StartRecording());
    const SlotControlReply card = Run(NetworkRequest(id, {{"card", "none"}}));
    EXPECT_EQ(card.status, "recording") << card.message;
    EXPECT_EQ(card.httpStatus, 409);
    const SlotControlReply hosts = Run(NetworkRequest(id, {{"hosts", "b.test=10.0.2.9"}}));
    EXPECT_EQ(hosts.status, "recording") << hosts.message;
    ttd->StopRecording();
}

/// The running [NETWORK] settings are configuration, not machine state (owner decision 2026-10-05): a slot restart
/// carries them, also the plug of a card that has nothing to do with the network; the keys of a network request win
/// over the carried ones, and the slot set decides the ZX-bus card bits
TEST_F(SlotControl_Test, SlotRestartCarriesTheRunningNetworkSettings)
{
    std::shared_ptr<Emulator> emulator = Create("PENTAGON", {{"zxbus.1", "zxnetusb"}});
    ASSERT_NE(emulator, nullptr);
    const std::string id = emulator->GetId();
    const uint8_t hostAccess = emulator->GetContext()->config.network.hostAccess;
    const std::string flipped = hostAccess != 0 ? "off" : "on";
    emulator.reset();

    // Changed at run time, in place (no card change)
    const SlotControlReply inPlace = Run(NetworkRequest(
        id, {{"hosts", "carry.test=10.0.2.55"}, {"connect_timeout_ms", "7777"}, {"host_access", flipped}}));
    ASSERT_EQ(inPlace.status, "accepted") << inPlace.message;

    // An unrelated card: the restart keeps the settings
    const SlotControlReply plug = Run(Request("plug", id, "zxbus.next", "gs"));
    ASSERT_EQ(plug.status, "applied") << plug.message;
    ASSERT_NE(plug.emulator, nullptr);
    EXPECT_NE(plug.emulator->GetId(), id) << "a restart";
    const auto& net = plug.emulator->GetContext()->config.network;
    EXPECT_STREQ(net.hosts, "carry.test=10.0.2.55");
    EXPECT_EQ(net.connectTimeoutMs, 7777u);
    EXPECT_EQ(net.hostAccess, hostAccess != 0 ? 0 : 1);
    EXPECT_EQ(net.card & NetworkManager::kZxBusCards, networkspec::kCardZxNetUsb) << "the slot set's card";
    ASSERT_NE(plug.emulator->GetContext()->pVirtualNetwork, nullptr) << "the live network follows the settings";
    EXPECT_EQ(plug.emulator->GetContext()->pVirtualNetwork->Config().hosts.at("carry.test"), NetIp(10, 0, 2, 55));

    // A network request with a card change: its own keys win, the others are carried, the card bits follow the plan
    const SlotControlReply swap =
        Run(NetworkRequest(plug.emulator->GetId(), {{"card", "zxwifi"}, {"connect_timeout_ms", "9000"}}));
    ASSERT_EQ(swap.status, "applied") << swap.message;
    ASSERT_NE(swap.emulator, nullptr);
    const auto& after = swap.emulator->GetContext()->config.network;
    EXPECT_EQ(after.connectTimeoutMs, 9000u) << "the request's value wins";
    EXPECT_STREQ(after.hosts, "carry.test=10.0.2.55") << "carried";
    EXPECT_EQ(after.hostAccess, hostAccess != 0 ? 0 : 1) << "carried";
    EXPECT_EQ(after.card & NetworkManager::kZxBusCards, networkspec::kCardZxWifi);

    // A remove is a slot restart too: the network card goes, the settings stay
    std::string wifiSlot;
    for (const SlotManager::Slot& slot : swap.emulator->GetContext()->pSlotManager->Current().entries)
    {
        if (slot.entry.card == "zx-wifi")
            wifiSlot = slot.entry.slot;
    }
    ASSERT_FALSE(wifiSlot.empty());
    const SlotControlReply undone = Run(Request("remove", swap.emulator->GetId(), wifiSlot));
    ASSERT_EQ(undone.status, "applied") << undone.message;
    EXPECT_STREQ(undone.emulator->GetContext()->config.network.hosts, "carry.test=10.0.2.55");
    EXPECT_EQ(undone.emulator->GetContext()->config.network.card & NetworkManager::kZxBusCards, 0);
}

/// The firmware choices of the network settings are configuration too (owner decision 2026-10-06): `avr_firmware`
/// ([EVO] Avr=, the ZX-Evo's AVR) and `kbc_firmware` ([ATM] Kbc=, the ATM Turbo 2+ keyboard controller, with its
/// [ROM] ATM2KBC= image) survive a slot restart as the [NETWORK] settings do. Two machines, two restarts (~60 ms)
TEST_F(SlotControl_Test, SlotRestartCarriesTheFirmwareChoices)
{
    std::shared_ptr<Emulator> evo = Create("ATM3", {});
    ASSERT_NE(evo, nullptr);
    const std::string evoId = evo->GetId();
    ASSERT_NE(evo->GetContext()->config.atm.evo_avr, static_cast<uint8_t>(Uart16550::AvrFirmware::Base2011Apr));
    evo.reset();
    const SlotControlReply avr = Run(NetworkRequest(evoId, {{"avr_firmware", "base2011-04"}}));
    ASSERT_EQ(avr.status, "accepted") << avr.message;
    const SlotControlReply evoPlug = Run(Request("plug", evoId, "zxbus.next", "gs"));
    ASSERT_EQ(evoPlug.status, "applied") << evoPlug.message;
    ASSERT_NE(evoPlug.emulator, nullptr);
    EXPECT_NE(evoPlug.emulator->GetId(), evoId) << "a restart";
    EXPECT_EQ(evoPlug.emulator->GetContext()->config.atm.evo_avr,
              static_cast<uint8_t>(Uart16550::AvrFirmware::Base2011Apr));

    std::shared_ptr<Emulator> atm = Create("ATM710", {});
    ASSERT_NE(atm, nullptr);
    const std::string atmId = atm->GetId();
    ASSERT_NE(atm->GetContext()->config.atm.kbc_firmware, static_cast<uint8_t>(Atm2Kbc::Firmware::V22At7));
    atm.reset();
    const SlotControlReply kbc = Run(NetworkRequest(atmId, {{"kbc_firmware", "v22-7"}}));
    ASSERT_EQ(kbc.status, "accepted") << kbc.message;
    SlotControlRequest atmRequest = Request("plug", atmId, "ay-socket", "ts");
    atmRequest.replaceIfIncompatible = true;   // the TurboSound board in place of the AY
    const SlotControlReply atmPlug = Run(atmRequest);
    ASSERT_EQ(atmPlug.status, "applied") << atmPlug.message;
    ASSERT_NE(atmPlug.emulator, nullptr);
    EXPECT_EQ(atmPlug.emulator->GetContext()->config.atm.kbc_firmware,
              static_cast<uint8_t>(Atm2Kbc::Firmware::V22At7));
    EXPECT_STREQ(atmPlug.emulator->GetContext()->config.atm.kbc_rom_path, "") << "the preset's image, as chosen";
}

/// SL-8: the Sprinter's ISA slots are the machine's own slots, filled by [ISA]: the slot report lists them
/// (machineSlots), the ZX-bus adapter in one hosts a ZX-bus (a bus with a host), and the General Sound behind it is
/// listed on that ZX-bus. A ZX-bus card needs the adapter in its ISA slot: a plug into the NE2000's slot is refused,
/// and the GS sits on the adapter of the slot the slot set names. ~100 ms: three Sprinter machines are built (their
/// creation dominates; the refusals cost nothing)
TEST_F(SlotControl_Test, SprinterIsaSlotsInTheReport)
{
    const Lines gsOnIsa1 = {{"isa.1", "neogs"}, {"isa.1.adapter", "sprinter-isa-zxbus"}, {"isa.1.fit", "unrealistic"}};
    std::shared_ptr<Emulator> emulator = Create("SPRINTER", gsOnIsa1);
    ASSERT_NE(emulator, nullptr);
    const StateNode report = DeviceState::Slots(emulator->GetContext());

    std::vector<std::string> machineSlots;
    for (const StateNode& slot : Field(report, "machineSlots").items)
        machineSlots.push_back(Field(slot, "slot").s + " = " + Field(slot, "card").s + " (" + Field(slot, "source").s +
                               ")" + (slot.find("hostsBus") ? " hosts " + Field(slot, "hostsBus").s + ": " +
                                                                  Field(slot, "hostedCard").s
                                                            : std::string()));
    EXPECT_EQ(machineSlots, (std::vector<std::string>{"isa.1 = zxbus ([ISA] Slot1) hosts isa.1.zxbus: neogs",
                                                      "isa.2 = ne2000 ([ISA] Slot2)"}));
    EXPECT_EQ(Field(Field(report, "machineSlots").items[1], "details").s, "RTL8019AS, #300, IRQ 3");

    bool hostedBus = false;
    for (const StateNode& bus : Field(report, "buses").items)
        if (Field(bus, "id").s == "isa.1.zxbus")
        {
            hostedBus = true;
            EXPECT_EQ(Field(bus, "kind").s, "zxbus");
            EXPECT_EQ(Field(bus, "host").s, "isa.1");
        }
    EXPECT_TRUE(hostedBus) << "the adapter's ZX-bus is a bus of the report";
    const StateNode& gs = Field(report, "slots").items.at(0);
    EXPECT_EQ(Field(gs, "slot").s, "isa.1");
    EXPECT_EQ(Field(gs, "card").s, "neogs");
    EXPECT_EQ(Field(gs, "bus").s, "isa.1.zxbus");
    EXPECT_EQ(Field(gs, "busKind").s, "zxbus");
    EXPECT_EQ(Field(gs, "host").s, "isa.1");

    // isa.2 holds the NE2000: a ZX-bus card cannot go there
    SlotControlRequest plug = Request("plug", emulator->GetId(), "isa.2", "gs");
    plug.adapter = "sprinter-isa-zxbus";
    plug.replaceIfIncompatible = true;
    const SlotControlReply refused = Run(plug);
    EXPECT_NE(refused.status, "applied") << refused.message;
    EXPECT_NE(refused.message.find("isa.2 holds NE2000 Ethernet ([ISA] Slot2), not the ZX-bus adapter"),
              std::string::npos)
        << refused.message;

    // The adapter passes the General Sound's ports only: another ZX-bus card behind it waits for ISA phase I5
    SlotControlRequest moon = Request("plug", emulator->GetId(), "isa.1", "moonsound");
    moon.adapter = "sprinter-isa-zxbus";
    moon.replaceIfIncompatible = true;
    const SlotControlReply notPassed = Run(moon);
    EXPECT_NE(notPassed.status, "applied") << notPassed.message;
    EXPECT_NE(notPassed.message.find("passes the General Sound's ports only"), std::string::npos) << notPassed.message;

    // Both slots hold the adapter, the slot set puts the GS in isa.2: it sits on the second adapter
    std::string error;
    std::shared_ptr<Emulator> both = EmulatorManager::GetInstance()->CreateEmulatorWithModel(
        "", "SPRINTER", LoggerLevel::LogError, &error, [](CONFIG& config) {
            config.sprinter.isa.slot[0].kind = static_cast<uint8_t>(sprinterisa::CardKind::ZxBus);
            config.sprinter.isa.slot[1].kind = static_cast<uint8_t>(sprinterisa::CardKind::ZxBus);
            SlotConfig slotConfig;
            ParseSlotsSection({{"isa.2", "gs"}, {"isa.2.adapter", "sprinter-isa-zxbus"}, {"isa.2.fit", "unrealistic"}},
                              slotConfig);
            SlotManager::UseSlots(slotConfig, config);
        });
    ASSERT_NE(both, nullptr) << error;
    _ids.push_back(both->GetId());
    const StateNode isa = DeviceState::Isa(both->GetContext());
    const StateNode& slots = Field(isa, "slots");
    ASSERT_EQ(slots.items.size(), 2u);
    EXPECT_NE(Field(Field(slots.items[0], "zx_bus"), "empty").s.find("ISA slot 2 ([SLOTS] isa.2)"), std::string::npos)
        << "the GS is not on the first adapter";
    EXPECT_EQ(Field(Field(slots.items[1], "zx_bus"), "cards").items.size(), 1u) << "the GS sits on the second";
}

/// The runtime feature `network` is a power switch of the network devices (owner decision 2026-10-05): with it off a
/// fitted ZX-bus network card stays in its slot and the slot report's state says "feature network off"
TEST_F(SlotControl_Test, NetworkFeatureOffShowsInTheSlotState)
{
    std::shared_ptr<Emulator> emulator = Create("PENTAGON", {{"zxbus.1", "zxnetusb"}, {"zxbus.2", "gs"}});
    ASSERT_NE(emulator, nullptr);
    auto states = [&emulator]() {
        std::vector<std::string> out;
        const StateNode report = DeviceState::Slots(emulator->GetContext());
        for (const StateNode& slot : Field(report, "slots").items)
            out.push_back(Field(slot, "card").s + ": " + Field(slot, "state").s);
        return out;
    };
    EXPECT_EQ(states(), (std::vector<std::string>{"zxnetusb: active", "gs: active"}));

    emulator->GetFeatureManager()->setFeature(Features::kNetwork, false);
    EXPECT_EQ(states(), (std::vector<std::string>{"zxnetusb: feature network off", "gs: active"}));
    EXPECT_EQ(FittedOf(*emulator), (std::vector<std::string>{"zxbus.1 = zxnetusb", "zxbus.2 = gs"}))
        << "the card stays in its slot";

    emulator->GetFeatureManager()->setFeature(Features::kNetwork, true);
    EXPECT_EQ(states(), (std::vector<std::string>{"zxnetusb: active", "gs: active"}));
}

/// Several slot requests planned into one change: each against the set the previous one leaves
TEST_F(SlotControl_Test, PlanChangesChainsTheRequests)
{
    std::shared_ptr<Emulator> emulator = Create("PENTAGON", {{"zxbus.1", "zx-wifi"}, {"zxbus.2", "gs"}});
    ASSERT_NE(emulator, nullptr);
    const SlotManager::Result current = emulator->GetContext()->pSlotManager->Snapshot();
    std::vector<slots::SlotRequest> requests;
    ASSERT_TRUE(SlotManager::NetworkRequests(current, networkspec::kCardZxNetUsb, requests));
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_EQ(requests[0].op, slots::SlotRequest::Op::Remove);
    EXPECT_EQ(requests[0].slot, "zxbus.1");
    EXPECT_EQ(requests[1].op, slots::SlotRequest::Op::Plug);
    EXPECT_EQ(requests[1].card, "zxnetusb");

    const SlotManager::ChangePlan plan = emulator->GetContext()->pSlotManager->PlanChanges(requests);
    ASSERT_TRUE(plan.Allowed()) << plan.refusal;
    EXPECT_EQ(plan.plan.card, "zxnetusb") << "the merged plan names the last plug";
    ASSERT_EQ(plan.plan.removed.size(), 1u);
    EXPECT_EQ(plan.plan.removed[0].card, "zx-wifi");
    std::vector<std::string> lines;
    for (const SlotConfigEntry& entry : plan.config.entries)
        lines.push_back(entry.slot + " = " + entry.card);
    EXPECT_EQ(lines, (std::vector<std::string>{"zxbus.1 = zxnetusb", "zxbus.2 = gs"})) << "the freed slot is reused";

    ASSERT_TRUE(SlotManager::NetworkRequests(current, networkspec::kCardZxWifi, requests));
    EXPECT_TRUE(requests.empty()) << "nothing to change";
}

// endregion
