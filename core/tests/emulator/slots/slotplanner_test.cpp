// ZX-bus slot plan engine (core/src/emulator/slots/slotplanner.h): rules D1-D12 of
// docs/inprogress/2026-10-03-zx-bus-slots/reference-data.md §5 and the worked plans of compatibility-matrix.md §6.
// Pure logic over the reference data; no emulator is created.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "emulator/platform.h"
#include "emulator/slots/refdata/refdata.h"
#include "emulator/slots/slotplanner.h"

using namespace slots;

namespace
{

const SlotPlanner& Planner()
{
    static const SlotPlanner planner;
    return planner;
}

CardOptions Options(const SlotPlanner& planner, const std::string& card, const std::string& text)
{
    CardOptions options;
    const CardDef* def = planner.FindCard(card);
    EXPECT_NE(def, nullptr) << card;
    if (def != nullptr)
    {
        std::string error;
        EXPECT_TRUE(ParseCardOptions(*def, text, options, &error)) << error;
    }
    return options;
}

SlotEntry Entry(const std::string& slot, const std::string& card, const std::string& options = {},
                const SlotPlanner& planner = Planner())
{
    SlotEntry entry;
    entry.slot = slot;
    entry.card = card;
    entry.options = Options(planner, card, options);
    return entry;
}

SlotRequest Plug(const std::string& slot, const std::string& card, const std::string& options = {},
                 bool replace = false, const SlotPlanner& planner = Planner())
{
    SlotRequest request;
    request.op = SlotRequest::Op::Plug;
    request.slot = slot;
    request.card = card;
    request.options = Options(planner, card, options);
    request.replaceIfIncompatible = replace;
    return request;
}

std::vector<std::string> RemovedList(const SlotPlan& plan)
{
    std::vector<std::string> result;
    for (const RemovedCard& removed : plan.removed)
    {
        result.push_back(removed.slot + " " + removed.card);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> ShadowedList(const SlotPlan& plan)
{
    std::vector<std::string> result;
    for (const ShadowedDevice& shadowed : plan.shadowed)
    {
        result.push_back(shadowed.device);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> SocketList(const SlotPlan& plan)
{
    std::vector<std::string> result;
    for (const RemovedFromSocket& removed : plan.removedFromSocket)
    {
        result.push_back(removed.builtIn);
    }
    return result;
}

bool AnyReasonContains(const SlotPlan& plan, const std::string& text)
{
    return std::any_of(plan.reasons.begin(), plan.reasons.end(),
                       [&text](const PlanReason& reason) { return reason.text.find(text) != std::string::npos; });
}

const SlotEntry* FindSlot(const SlotSet& slots, const std::string& slot)
{
    for (const SlotEntry& entry : slots)
    {
        if (entry.slot == slot)
        {
            return &entry;
        }
    }
    return nullptr;
}

// region <A test collection: one machine and a few cards that the real catalog does not need yet>

constexpr Src kTestSources[] = { Src::RepoSlotsResearchCards };

constexpr PortClaim kReaderAClaims[] = { { .mask = 0x00FF, .match = 0x0042, .dir = Dir::InOut } };
constexpr PortClaim kReaderBClaims[] = { { .mask = 0x00FE, .match = 0x0042, .dir = Dir::InOut } };
constexpr PortClaim kIdeClaims[] = { { .mask = 0x00FF, .match = 0x0090, .dir = Dir::InOut, .iorqge = Iorqge::Yes } };
constexpr PortClaim kJoyClaims[] = { { .mask = 0x00FF, .match = 0x0037, .dir = Dir::In, .iorqge = Iorqge::Yes } };
constexpr FunctionUse kIdeFunctions[] = { { .function = Function::IdeNemo } };
constexpr FunctionUse kJoyFunctions[] = { { .function = Function::KempstonJoystick } };
constexpr FunctionUse kReaderAFunctions[] = { { .function = Function::Rtc } };
constexpr FunctionUse kReaderBFunctions[] = { { .function = Function::SdZc } };

constexpr CardDef kTestCards[] = {
    { .id = "test-reader-a", .name = "passive reader A", .functions = kReaderAFunctions, .claims = kReaderAClaims,
      .sources = kTestSources },
    { .id = "test-reader-b", .name = "passive reader B", .functions = kReaderBFunctions, .claims = kReaderBClaims,
      .sources = kTestSources },
    { .id = "test-ide", .name = "IDE card", .needs = Sig(BusSignal::Iorqge), .functions = kIdeFunctions,
      .claims = kIdeClaims, .sources = kTestSources },
    { .id = "test-joy", .name = "joystick card", .needs = Sig(BusSignal::Iorqge), .functions = kJoyFunctions,
      .claims = kJoyClaims, .sources = kTestSources },
};

constexpr BusDef kTestBuses[] = {
    { .id = "zxbus", .kind = BusKind::ZxBus, .signals = BusSignal::Iorqge | BusSignal::Plus12V, .physicalSlots = 2,
      .arbitration = Arbitration::CardWins },
};
constexpr Function kIdeFunctionList[] = { Function::IdeNemo };
constexpr Function kJoyFunctionList[] = { Function::KempstonJoystick };
constexpr BuiltInDef kTestBuiltIns[] = {
    { .id = "board-ide", .name = "IDE on the board", .kind = BuiltInKind::Switchable, .functions = kIdeFunctionList },
    { .id = "board-joy", .name = "joystick on the board", .functions = kJoyFunctionList },
};
constexpr MachineDef kTestMachines[] = {
    { .model = MM_PENTAGON, .name = "TESTBOX", .variant = "test", .buses = kTestBuses, .builtIns = kTestBuiltIns,
      .sources = kTestSources },
};

const SlotPlanner& TestPlanner()
{
    static const Collection collection{ kTestCards, kTestMachines, {}, {}, refdata::Sources() };
    static const SlotPlanner planner(collection);
    return planner;
}

// endregion

} // namespace

// region <Worked plans of compatibility-matrix.md §6>

namespace
{

struct WorkedCase
{
    const char* name;
    MEM_MODEL model;
    SlotSet slots;
    SlotRequest request;
    std::vector<std::string> dirtyMedia;
    bool allowedWithoutFlag;
    bool allowedWithFlag;
    std::vector<std::string> removed;           ///< "slot card", sorted
    std::vector<std::string> shadowed;          ///< built-in ids, sorted
    std::vector<Function> lost;                 ///< Function order
    std::vector<std::string> removedFromSocket;
    Fit fit;
    std::vector<std::string> reasonFragments;   ///< each in some reason
};

SlotRequest SetOptions(const std::string& slot, const std::string& card, const std::string& options)
{
    SlotRequest request;
    request.op = SlotRequest::Op::SetOptions;
    request.slot = slot;
    request.options = Options(Planner(), card, options);
    return request;
}

std::vector<WorkedCase> WorkedCases()
{
    return {
        { "A: one card displaces several (Pentagon)", MM_PENTAGON,
          { Entry("ay-socket", "tsfm"), Entry("zxbus.1", "gs"), Entry("zxbus.2", "soundrive", "mode=2") },
          Plug("zxbus.3", "multisound"), {}, false, true,
          { "ay-socket tsfm", "zxbus.1 gs", "zxbus.2 soundrive" }, { "ay" }, { Function::CovoxFb }, {}, Fit::Real,
          { "pointless pair", "shares `gs`", "shares `soundrive`" } },
        { "B: options avoid the clash", MM_PENTAGON, { Entry("zxbus.1", "neogs") },
          Plug("zxbus.2", "multisound", "dip=ym,saa,sd"), {}, true, true, {}, { "ay" }, {}, {}, Fit::Real, {} },
        { "C: option change later; the dirty sd.ngs medium refuses it", MM_PENTAGON,
          { Entry("zxbus.1", "neogs"), Entry("zxbus.2", "multisound", "dip=ym,saa,sd") },
          SetOptions("zxbus.2", "multisound", "dip=ym,saa,gs,sd"), { "sd.ngs" }, false, false,
          { "zxbus.1 neogs" }, { "ay" }, {}, {}, Fit::Real, { "`sd.ngs`", "unsaved changes" } },
        { "C: the same with a clean medium", MM_PENTAGON,
          { Entry("zxbus.1", "neogs"), Entry("zxbus.2", "multisound", "dip=ym,saa,sd") },
          SetOptions("zxbus.2", "multisound", "dip=ym,saa,gs,sd"), {}, false, true,
          { "zxbus.1 neogs" }, { "ay" }, {}, {}, Fit::Real, { "shares `gs`" } },
        { "D: replacing with less", MM_PENTAGON, { Entry("zxbus.1", "multisound") }, Plug("ay-socket", "tsfm"), {},
          false, true, { "zxbus.1 multisound" }, {}, { Function::Gs, Function::Saa, Function::Soundrive, Function::Midi },
          {}, Fit::Real, { "shares `ay-socket`" } },
        { "E: ZX-Evo empties the YM2149 socket (Q7)", MM_ATM3, {}, Plug("zxbus.1", "multisound"), {}, false, true, {}, {},
          {}, { "ay" }, Fit::Real, { "YM2149", "`#FFFD` reads are a bus fight" } },
        { "F: TS-Conf hides #EF (ZiFi)", MM_TSL, {}, Plug("zxbus.1", "zx-wifi"), {}, false, false, {}, {}, {}, {},
          Fit::Real, { "`#EF` is a board port", "`zifi`", "`port=ee`" } },
        { "F: TS-Conf hides #FB (board Covox)", MM_TSL, {}, Plug("zxbus.1", "covox-fb"), {}, false, false, {}, {}, {}, {},
          Fit::Real, { "`#FB` is a board port", "`covox`" } },
        // With the override the card works as if the edge carried IORQGE, so it shadows the board AY
        { "G: bus fit on the 128K", MM_SPECTRUM128, {}, Plug("edge.1", "multisound"), {}, false, true, {}, { "ay" }, {}, {},
          Fit::Unrealistic, { "`zxbus` card", "`zxbus-to-sinclair-edge`" } },
        { "H: fixed built-in on the Profi", MM_PROFI, {}, Plug("profi-bus.1", "moonsound"), {}, false, false, {}, {}, {},
          {}, Fit::Unrealistic, { "palette" } },
    };
}

} // namespace

TEST(SlotPlanner_Test, WorkedPlans)
{
    for (const WorkedCase& worked : WorkedCases())
    {
        SCOPED_TRACE(worked.name);
        PlanContext context;
        context.dirtyMedia = worked.dirtyMedia;

        SlotRequest request = worked.request;
        request.replaceIfIncompatible = false;
        const SlotPlan without = Planner().Plan(worked.model, worked.slots, request, context);
        EXPECT_EQ(without.allowed, worked.allowedWithoutFlag) << ToText(without);

        request.replaceIfIncompatible = true;
        const SlotPlan with = Planner().Plan(worked.model, worked.slots, request, context);
        EXPECT_EQ(with.allowed, worked.allowedWithFlag) << ToText(with);

        EXPECT_EQ(RemovedList(with), worked.removed) << ToText(with);
        EXPECT_EQ(ShadowedList(with), worked.shadowed) << ToText(with);
        EXPECT_EQ(with.lostFunctions, worked.lost) << ToText(with);
        EXPECT_EQ(SocketList(with), worked.removedFromSocket) << ToText(with);
        EXPECT_EQ(with.fit, worked.fit) << ToText(with);
        for (const std::string& fragment : worked.reasonFragments)
        {
            EXPECT_TRUE(AnyReasonContains(with, fragment)) << "no reason mentions " << fragment << "\n" << ToText(with);
        }
    }
}

// endregion

// region <Plan tests of tdd.md §2.1>

TEST(SlotPlanner_Test, PlugIntoEmptySlot)
{
    const SlotPlan plan = Planner().Plan(MM_PENTAGON, {}, Plug("zxbus.next", "gs"));
    EXPECT_TRUE(plan.allowed) << ToText(plan);
    EXPECT_FALSE(plan.needsConfirmation);
    EXPECT_EQ(plan.slot, "zxbus.1");
    EXPECT_EQ(plan.fit, Fit::Real);
    EXPECT_TRUE(plan.removed.empty());
    EXPECT_TRUE(plan.shadowed.empty());
    ASSERT_EQ(plan.resultingSlots.size(), 1u);
    EXPECT_EQ(plan.resultingSlots[0].slot, "zxbus.1");
    EXPECT_EQ(plan.resultingSlots[0].card, "gs");
}

TEST(SlotPlanner_Test, FunctionClashRefusedByDefault)
{
    const SlotPlan plan = Planner().Plan(MM_PENTAGON, { Entry("zxbus.1", "gs") }, Plug("zxbus.2", "neogs"));
    EXPECT_FALSE(plan.allowed);
    EXPECT_FALSE(plan.hardRefusal) << "the flag would allow it";
    EXPECT_TRUE(plan.needsConfirmation);
    ASSERT_EQ(plan.removed.size(), 1u) << ToText(plan);
    EXPECT_EQ(plan.removed[0].clashing, std::vector<Function>{ Function::Gs });
    EXPECT_TRUE(AnyReasonContains(plan, "`gs`"));
}

TEST(SlotPlanner_Test, FunctionClashReplacedWithFlag)
{
    const SlotPlan plan =
        Planner().Plan(MM_PENTAGON, { Entry("zxbus.1", "gs", "ram=512k rom=1.04") }, Plug("zxbus.2", "neogs", "", true));
    EXPECT_TRUE(plan.allowed) << ToText(plan);
    ASSERT_EQ(plan.removed.size(), 1u);
    EXPECT_EQ(plan.removed[0].slot, "zxbus.1");
    EXPECT_EQ(plan.removed[0].card, "gs");
    EXPECT_EQ(plan.removed[0].optionsText, "ram=512k rom=1.04") << "the full options, enough to put the card back";
    EXPECT_TRUE(plan.lostFunctions.empty()) << "NeoGS offers gs as well";
    ASSERT_EQ(plan.resultingSlots.size(), 1u);
    EXPECT_EQ(plan.resultingSlots[0].card, "neogs");
}

TEST(SlotPlanner_Test, OneCardRemovesSeveral)
{
    // Example A (the full check is in WorkedPlans): every clash is collected at once, nothing chains
    const SlotSet slots{ Entry("ay-socket", "tsfm"), Entry("zxbus.1", "gs"), Entry("zxbus.2", "soundrive", "mode=2"),
                         Entry("zxbus.4", "zxnetusb") };
    const SlotPlan plan = Planner().Plan(MM_PENTAGON, slots, Plug("zxbus.3", "multisound", "", true));
    EXPECT_EQ(plan.removed.size(), 3u) << ToText(plan);
    EXPECT_NE(FindSlot(plan.resultingSlots, "zxbus.4"), nullptr) << "an unrelated card stays";
    EXPECT_EQ(FindSlot(plan.resultingSlots, "ay-socket"), nullptr) << "the socket returns to the machine's own AY";
}

TEST(SlotPlanner_Test, OptionsAvoidClash)
{
    const SlotPlan plan =
        Planner().Plan(MM_PENTAGON, { Entry("zxbus.1", "neogs") }, Plug("zxbus.2", "multisound", "dip=ym,saa,sd"));
    EXPECT_TRUE(plan.allowed) << ToText(plan);
    EXPECT_TRUE(plan.removed.empty());
    EXPECT_EQ(plan.resultingSlots.size(), 2u);
}

TEST(SlotPlanner_Test, SetOptionsRunsSamePlan)
{
    const SlotSet slots{ Entry("zxbus.1", "neogs"), Entry("zxbus.2", "multisound", "dip=ym,saa,sd") };
    SlotRequest setOptions = SetOptions("zxbus.2", "multisound", "dip=ym,saa,gs,sd");
    setOptions.replaceIfIncompatible = true;
    const SlotPlan viaOptions = Planner().Plan(MM_PENTAGON, slots, setOptions);

    // The same as plugging the card with its new options into that slot
    const SlotPlan viaPlug =
        Planner().Plan(MM_PENTAGON, { Entry("zxbus.1", "neogs") }, Plug("zxbus.2", "multisound", "dip=ym,saa,gs,sd", true));
    EXPECT_EQ(RemovedList(viaOptions), RemovedList(viaPlug));
    EXPECT_EQ(viaOptions.lostFunctions, viaPlug.lostFunctions);
    EXPECT_EQ(viaOptions.allowed, viaPlug.allowed);
    ASSERT_EQ(viaOptions.media.size(), 1u);
    EXPECT_EQ(viaOptions.media[0].mediaSlot, "sd.ngs");

    // A dirty medium of the displaced card refuses the change unless the request says what to do with it
    PlanContext dirty;
    dirty.dirtyMedia = { "sd.ngs" };
    const SlotPlan refused = Planner().Plan(MM_PENTAGON, slots, setOptions, dirty);
    EXPECT_FALSE(refused.allowed);
    EXPECT_TRUE(refused.hardRefusal);
    setOptions.mediaDisposition = MediaDisposition::Discard;
    const SlotPlan discarded = Planner().Plan(MM_PENTAGON, slots, setOptions, dirty);
    EXPECT_TRUE(discarded.allowed) << ToText(discarded);
    ASSERT_EQ(discarded.media.size(), 1u);
    EXPECT_TRUE(discarded.media[0].dirty);
    EXPECT_EQ(discarded.media[0].disposition, MediaDisposition::Discard);
}

TEST(SlotPlanner_Test, ReplacingWithLessReportsLostFunctions)
{
    const SlotPlan plan =
        Planner().Plan(MM_PENTAGON, { Entry("zxbus.1", "multisound") }, Plug("ay-socket", "tsfm", "", true));
    EXPECT_TRUE(plan.allowed);
    EXPECT_EQ(plan.lostFunctions,
              (std::vector<Function>{ Function::Gs, Function::Saa, Function::Soundrive, Function::Midi }));
    const SlotEntry* socket = FindSlot(plan.resultingSlots, "ay-socket");
    ASSERT_NE(socket, nullptr);
    EXPECT_EQ(socket->card, "tsfm");
}

TEST(SlotPlanner_Test, BusFitNeedsAdapterOrOverride)
{
    // 48K: the edge carries /IORQULA, so a GS behind the edge adapter really works (fit: adapter)
    const SlotPlan bare = Planner().Plan(MM_SPECTRUM48, {}, Plug("edge.1", "gs"));
    EXPECT_EQ(bare.fit, Fit::Unrealistic);
    EXPECT_FALSE(bare.allowed) << "automation refuses an unrealistic fit without the flag";
    SlotRequest withAdapter = Plug("edge.1", "gs");
    withAdapter.adapter = "zxbus-to-sinclair-edge";
    const SlotPlan adapted = Planner().Plan(MM_SPECTRUM48, {}, withAdapter);
    EXPECT_EQ(adapted.fit, Fit::Adapter) << ToText(adapted);
    EXPECT_TRUE(adapted.allowed);
    EXPECT_EQ(adapted.arbitration, Arbitration::UlaOnly);

    // 128K: no IORQGE on the edge, the adapter cannot help; the override makes it `unrealistic`
    const SlotPlan noIorqge = Planner().Plan(MM_SPECTRUM128, {}, withAdapter);
    EXPECT_EQ(noIorqge.fit, Fit::Unrealistic);
    EXPECT_EQ(noIorqge.missingSignals, Sig(BusSignal::Iorqge));
    EXPECT_FALSE(noIorqge.allowed);
    withAdapter.replaceIfIncompatible = true;
    const SlotPlan overridden = Planner().Plan(MM_SPECTRUM128, {}, withAdapter);
    EXPECT_TRUE(overridden.allowed);
    ASSERT_EQ(overridden.resultingSlots.size(), 1u);
    EXPECT_TRUE(overridden.resultingSlots[0].unrealistic);

    // A missing signal on a native bus: the Scorpion yellow board has no +12 V on its slot
    const SlotPlan noPower = Planner().Plan(MM_SCORP, {}, Plug("zxbus.1", "multisound"));
    EXPECT_EQ(noPower.fit, Fit::Unrealistic);
    EXPECT_EQ(noPower.missingSignals, Sig(BusSignal::Plus12V));

    // ATM: the CPU-socket adapter brings a card-wins chain, so the board AY is shadowed as on a Pentagon
    SlotRequest atm = Plug("cpu-socket.1", "multisound");
    atm.adapter = "atm-cpu-socket-zxbus";
    const SlotPlan viaCpuSocket = Planner().Plan(MM_ATM710, {}, atm);
    EXPECT_EQ(viaCpuSocket.fit, Fit::Adapter) << ToText(viaCpuSocket);
    EXPECT_EQ(viaCpuSocket.arbitration, Arbitration::CardWins);
    EXPECT_EQ(ShadowedList(viaCpuSocket), std::vector<std::string>{ "ay" });

    // An adapter that does not connect these buses is refused outright
    SlotRequest wrong = Plug("edge.1", "gs", "", true);
    wrong.adapter = "atm-cpu-socket-zxbus";
    const SlotPlan wrongPlan = Planner().Plan(MM_SPECTRUM48, {}, wrong);
    EXPECT_TRUE(wrongPlan.hardRefusal);
}

TEST(SlotPlanner_Test, FixedBuiltInBlocksEvenWithFlag)
{
    // Example H: a documented exception (MoonSound vs the Profi palette)
    const SlotPlan profi = Planner().Plan(MM_PROFI, {}, Plug("profi-bus.1", "moonsound", "", true));
    EXPECT_FALSE(profi.allowed);
    EXPECT_TRUE(profi.hardRefusal);
    EXPECT_TRUE(AnyReasonContains(profi, "palette")) << ToText(profi);
    // Profi v3 has no palette: only the bus fit stands in the way
    EXPECT_TRUE(Planner().Plan(MM_PROFI3, {}, Plug("profi-bus.1", "moonsound", "", true)).allowed);

    // A card holding a function of a fixed built-in (the joystick on the test board)
    const SlotPlan plan = TestPlanner().Plan(MM_PENTAGON, {}, Plug("zxbus.1", "test-joy", "", true, TestPlanner()));
    EXPECT_FALSE(plan.allowed);
    EXPECT_TRUE(plan.hardRefusal);
    EXPECT_TRUE(AnyReasonContains(plan, "`board-joy`")) << ToText(plan);
}

TEST(SlotPlanner_Test, SwitchableBuiltInSwitchedOff)
{
    const SlotPlan without = TestPlanner().Plan(MM_PENTAGON, {}, Plug("zxbus.1", "test-ide", "", false, TestPlanner()));
    EXPECT_FALSE(without.allowed) << "switching a built-in off needs the confirmation";
    EXPECT_FALSE(without.hardRefusal);
    const SlotPlan with = TestPlanner().Plan(MM_PENTAGON, {}, Plug("zxbus.1", "test-ide", "", true, TestPlanner()));
    EXPECT_TRUE(with.allowed) << ToText(with);
    ASSERT_EQ(with.builtInSwitchedOff.size(), 1u);
    EXPECT_EQ(with.builtInSwitchedOff[0].builtIn, "board-ide");
    EXPECT_EQ(with.builtInSwitchedOff[0].functions, std::vector<Function>{ Function::IdeNemo });
}

TEST(SlotPlanner_Test, AccidentalPortClashDisablesLater)
{
    const SlotPlanner& planner = TestPlanner();
    // #42 is read by both, no function and no IORQGE explains it: the later card by slot order is disabled
    const SlotPlan plan = planner.Plan(MM_PENTAGON, { Entry("zxbus.1", "test-reader-a", {}, planner) },
                                       Plug("zxbus.2", "test-reader-b", "", false, planner));
    EXPECT_TRUE(plan.allowed) << ToText(plan);
    ASSERT_EQ(plan.disabled.size(), 1u);
    EXPECT_EQ(plan.disabled[0].slot, "zxbus.2");
    EXPECT_EQ(plan.disabled[0].clashSlot, "zxbus.1");
    EXPECT_EQ(plan.disabled[0].ports, std::vector<std::string>{ "#42" });
    const SlotEntry* entry = FindSlot(plan.resultingSlots, "zxbus.2");
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->disabled);
    EXPECT_NE(entry->disabledReason.find("zxbus.1"), std::string::npos);

    // Plugged below an installed card, the installed (later) one is disabled
    const SlotPlan below = planner.Plan(MM_PENTAGON, { Entry("zxbus.2", "test-reader-a", {}, planner) },
                                        Plug("zxbus.1", "test-reader-b", "", false, planner));
    ASSERT_EQ(below.disabled.size(), 1u);
    EXPECT_EQ(below.disabled[0].slot, "zxbus.2");
    EXPECT_EQ(below.disabled[0].card, "test-reader-a");
}

TEST(SlotPlanner_Test, DryRunChangesNothing)
{
    const SlotSet slots{ Entry("zxbus.1", "gs") };
    const SlotSet before = slots;
    SlotRequest request = Plug("zxbus.2", "neogs", "", true);
    const SlotPlan real = Planner().Plan(MM_PENTAGON, slots, request);
    request.dryRun = true;
    const SlotPlan dry = Planner().Plan(MM_PENTAGON, slots, request);
    EXPECT_TRUE(dry.dryRun);
    EXPECT_TRUE(dry.allowed);
    EXPECT_FALSE(dry.WouldApply());
    EXPECT_TRUE(real.WouldApply());
    EXPECT_EQ(RemovedList(dry), RemovedList(real)) << "the same plan";
    ASSERT_EQ(slots.size(), before.size());
    EXPECT_EQ(slots[0].card, "gs");
}

// endregion

// region <Rule details>

TEST(SlotPlanner_Test, ResultDoesNotDependOnInsertionOrder)
{
    SlotSet slots{ Entry("ay-socket", "tsfm"), Entry("zxbus.1", "gs"), Entry("zxbus.2", "soundrive", "mode=2"),
                   Entry("zxbus.10", "zxnetusb") };
    const std::string reference = ToText(Planner().Plan(MM_PENTAGON, slots, Plug("zxbus.3", "multisound", "", true)));
    std::sort(slots.begin(), slots.end(), [](const SlotEntry& a, const SlotEntry& b) { return a.slot < b.slot; });
    do
    {
        EXPECT_EQ(ToText(Planner().Plan(MM_PENTAGON, slots, Plug("zxbus.3", "multisound", "", true))), reference);
    } while (std::next_permutation(slots.begin(), slots.end(),
                                   [](const SlotEntry& a, const SlotEntry& b) { return a.slot < b.slot; }));
}

TEST(SlotPlanner_Test, DisplacementIsOneStep)
{
    // GS displaces the NeoGS only; the MultiSound without its GS keeps its slot, nothing chains
    const SlotSet slots{ Entry("zxbus.1", "neogs"), Entry("zxbus.2", "multisound", "dip=ym,sd") };
    const SlotPlan plan = Planner().Plan(MM_PENTAGON, slots, Plug("zxbus.3", "gs", "", true));
    EXPECT_EQ(RemovedList(plan), std::vector<std::string>{ "zxbus.1 neogs" });
    EXPECT_NE(FindSlot(plan.resultingSlots, "zxbus.2"), nullptr);
}

TEST(SlotPlanner_Test, SocketBoardOnBoardWinsMachine)
{
    // ZX-Evo with a TSFM in the socket: the MultiSound makes the TSFM pointless and empties the socket
    const SlotPlan plan =
        Planner().Plan(MM_ATM3, { Entry("ay-socket", "tsfm") }, Plug("zxbus.1", "multisound", "", true));
    EXPECT_TRUE(plan.allowed) << ToText(plan);
    EXPECT_EQ(RemovedList(plan), std::vector<std::string>{ "ay-socket tsfm" });
    EXPECT_TRUE(plan.removed[0].pointless);
    EXPECT_EQ(SocketList(plan), std::vector<std::string>{ "ay" });
    const SlotEntry* socket = FindSlot(plan.resultingSlots, "ay-socket");
    ASSERT_NE(socket, nullptr);
    EXPECT_EQ(socket->card, kEmptySocket);
    EXPECT_TRUE(plan.deadPorts.empty()) << "an RdWr card sees the board ports (its SounDrive #1F works)";

    // A TSFM into the ZX-Evo socket replaces the YM2149 without a word
    const SlotPlan tsfm = Planner().Plan(MM_ATM3, {}, Plug("ay-socket", "tsfm"));
    EXPECT_TRUE(tsfm.allowed);
    EXPECT_FALSE(tsfm.needsConfirmation);
}

TEST(SlotPlanner_Test, BoardPortsAreDeadForIorqCards)
{
    const SlotPlan plan = Planner().Plan(MM_ATM3, {}, Plug("zxbus.1", "soundrive", "mode=1"));
    EXPECT_TRUE(plan.allowed) << "partly dead is allowed and reported";
    ASSERT_EQ(plan.deadPorts.size(), 1u) << ToText(plan);
    EXPECT_EQ(FormatPortRange(plan.deadPorts[0].mask, plan.deadPorts[0].match), "#1F");
    EXPECT_NE(std::find(plan.deadPorts[0].servedBy.begin(), plan.deadPorts[0].servedBy.end(), "kempston-joystick"),
              plan.deadPorts[0].servedBy.end());

    // The #EE build of the ZX-WiFi passes on the ZX-Evo
    EXPECT_TRUE(Planner().Plan(MM_ATM3, {}, Plug("zxbus.1", "zx-wifi", "port=ee")).allowed);
    EXPECT_FALSE(Planner().Plan(MM_ATM3, {}, Plug("zxbus.1", "zx-wifi", "port=ef", true)).allowed);
}

TEST(SlotPlanner_Test, RemoveReleasesMedia)
{
    const SlotSet slots{ Entry("zxbus.1", "neogs"), Entry("zxbus.2", "zxnetusb") };
    SlotRequest remove;
    remove.op = SlotRequest::Op::Remove;
    remove.slot = "zxbus.1";
    PlanContext dirty;
    dirty.dirtyMedia = { "sd.ngs" };
    EXPECT_FALSE(Planner().Plan(MM_PENTAGON, slots, remove, dirty).allowed);

    remove.mediaDisposition = MediaDisposition::Save;
    const SlotPlan plan = Planner().Plan(MM_PENTAGON, slots, remove, dirty);
    EXPECT_TRUE(plan.allowed) << ToText(plan);
    EXPECT_EQ(plan.lostFunctions, std::vector<Function>{ Function::Gs });
    ASSERT_EQ(plan.media.size(), 1u);
    EXPECT_EQ(plan.media[0].disposition, MediaDisposition::Save);
    ASSERT_EQ(plan.resultingSlots.size(), 1u);
    EXPECT_EQ(plan.resultingSlots[0].card, "zxnetusb");
}

TEST(SlotPlanner_Test, MalformedRequestsRefused)
{
    SlotRequest unknown;
    unknown.slot = "zxbus.1";
    unknown.card = "no-such-card";
    EXPECT_TRUE(Planner().Plan(MM_PENTAGON, {}, unknown).hardRefusal);
    EXPECT_TRUE(Planner().Plan(MM_PENTAGON, {}, Plug("isa.1", "gs", "", true)).hardRefusal) << "no such bus";
    EXPECT_TRUE(Planner().Plan(MM_PENTAGON, {}, Plug("zxbus.1", "tsfm", "", true)).hardRefusal) << "socket board";
    EXPECT_TRUE(Planner().Plan(MM_PENTAGON, {}, Plug("ay-socket", "gs", "", true)).hardRefusal) << "bus card";
    // The 48K's AY socket is retrofitted since SL-4 step 2 (the emulator's 48K decoder routes the 128K AY decode
    // to an AY interface, and the shipped 48K config fits one): a socket board is planned, not refused
    EXPECT_TRUE(Planner().Plan(MM_SPECTRUM48, {}, Plug("", "ts", "", true)).allowed) << "48K retrofitted AY socket";
    EXPECT_TRUE(Planner().Plan(MM_SPECTRUM48, {}, Plug("ay-socket", "gs", "", true)).hardRefusal) << "bus card";
    EXPECT_TRUE(Planner().Plan(MM_NEXT, {}, Plug("zxbus.1", "gs", "", true)).hardRefusal) << "not a creatable model";
}

TEST(SlotPlanner_Test, OptionsParseAndFormat)
{
    const CardDef* multisound = Planner().FindCard("multisound");
    ASSERT_NE(multisound, nullptr);
    CardOptions options;
    std::string error;
    ASSERT_TRUE(ParseCardOptions(*multisound, "dip=ym,sd gsRam=2m", options, &error)) << error;
    EXPECT_EQ(FormatCardOptions(*multisound, options), "dip=ym,sd gsRam=2m ctrlMask=pro");
    EXPECT_EQ(FormatCardOptions(*multisound, CardOptions{}), "dip=ym,saa,gs,sd gsRam=1m ctrlMask=pro");

    CardOptions none;
    ASSERT_TRUE(ParseCardOptions(*multisound, "dip=none", none, &error)) << error;
    EXPECT_EQ(FormatCardOptions(*multisound, none), "dip=none gsRam=1m ctrlMask=pro");

    CardOptions bad;
    EXPECT_FALSE(ParseCardOptions(*multisound, "dip=opl4", bad, &error));
    EXPECT_FALSE(ParseCardOptions(*multisound, "mode=2", bad, &error));
    EXPECT_FALSE(ParseCardOptions(*multisound, "gsRam=1m,2m", bad, &error)) << "an enum option takes one value";
}

// endregion
