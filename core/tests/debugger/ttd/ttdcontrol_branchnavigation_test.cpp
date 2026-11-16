#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdsessiontypes.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/branchactions.h"
#include "emulator/actions/readouts.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// The surfaces' way around the branches (TTD branches surfaces.md §2, P6): branch-switch to=previous walks the trail
/// of the branches left, to=next_sibling / prev_sibling cycles the branches at the nearest fork by creation, and the
/// ttd.branch_* actions and the ttd.branch / ttd.branch_list readouts read the published table only.
///
/// Runtime justification: each test records 30 frames of a small loop and runs two 3-frame branches (~0.1 s).
class TTDControl_BranchNavigation_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    uint64_t _start = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        static const uint8_t program[] = {0xF3, 0x21, 0x00, 0x60, 0x34, 0x23, 0x18, 0xFC};  // DI; LD HL; INC (HL)...
        _emulator->RunNFrames(5);
        Z80* z80 = _context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(program); i++)
            z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
        z80->pc = 0x8000;
        ASSERT_TRUE(_ttd->StartRecording());
        _start = _context->emulatorState.frame_counter;
        _emulator->RunNFrames(30);
        _ttd->StopRecording();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    ttd::TTDReply Run(const std::string& verb, std::map<std::string, std::string> options = {})
    {
        return ttd::TTDControl(_context).Execute({verb, std::move(options)});
    }

    /// A branch named @p name forking on @p on at start + @p at, run 3 frames; the machine stays on it
    void Fork(const std::string& name, const std::string& on, uint64_t at)
    {
        const ttd::TTDReply reply = Run("branch-create", {{"name", name},
                                                          {"branch", on},
                                                          {"frame", std::to_string(_start + at)},
                                                          {"run_frames", "3"}});
        ASSERT_TRUE(reply.Ok()) << reply.message;
    }

    std::string Active() const
    {
        const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
        const ttd::TTDBranchInfo* b = info.branches ? info.branches->Find(info.activeBranch) : nullptr;
        return b ? b->meta.name : std::string("trunk");
    }

    static std::string SwitchedTo(const ttd::TTDReply& reply)
    {
        const StateNode* branch = reply.body.find("branch");
        const StateNode* name = branch ? branch->find("name") : nullptr;
        return name ? name->s : std::string();
    }
};

/// Every change of the active branch leaves the old one on a trail; to=previous walks it back one step a call, and
/// says so once it is empty. A deleted branch leaves the trail
TEST_F(TTDControl_BranchNavigation_Test, PreviousWalksTheTrailBack)
{
    EXPECT_EQ(Run("branch-switch", {{"to", "previous"}}).error, ttd::TTDControlError::NotFound)
        << "the trunk alone: no branch was left";
    Fork("a", "0", 10);
    Fork("b", "0", 20);
    ASSERT_EQ(Active(), "b");
    EXPECT_EQ(Run("branches").body.find("previous")->i, static_cast<int64_t>(_ttd->ReadSessionInfo().branches->branches[1].id))
        << "a was left last";

    ttd::TTDReply back = Run("branch-switch", {{"to", "previous"}});
    ASSERT_TRUE(back.Ok()) << back.message;
    EXPECT_EQ(SwitchedTo(back), "a");
    back = Run("branch-switch", {{"to", "previous"}});
    ASSERT_TRUE(back.Ok()) << back.message;
    EXPECT_EQ(Active(), "trunk") << "again: one step further back, not back and forth";
    EXPECT_EQ(Run("branch-switch", {{"to", "previous"}}).error, ttd::TTDControlError::NotFound);
    EXPECT_TRUE(Run("branches").body.find("previous")->kind == StateNode::Kind::Null);

    // A switch leaves the trunk on the trail; deleting the branch left removes it there
    ASSERT_TRUE(Run("branch-switch", {{"branch", "a"}}).Ok());
    ASSERT_TRUE(Run("branch-switch", {{"branch", "b"}}).Ok());
    ASSERT_TRUE(Run("branch-delete", {{"branch", "a"}}).Ok());
    back = Run("branch-switch", {{"to", "previous"}});
    ASSERT_TRUE(back.Ok()) << back.message;
    EXPECT_EQ(Active(), "trunk") << "a went: the step before it";

    EXPECT_EQ(Run("branch-switch", {{"to", "previous"}, {"branch", "b"}}).error, ttd::TTDControlError::BadRequest);
    EXPECT_EQ(Run("branch-switch", {{"to", "sideways"}}).error, ttd::TTDControlError::BadRequest);
}

/// The branches at the nearest fork, the lineage's own included, in creation order: next / prev cycle them
TEST_F(TTDControl_BranchNavigation_Test, SiblingsCycleAtTheNearestFork)
{
    EXPECT_EQ(Run("branch-switch", {{"to", "next_sibling"}}).error, ttd::TTDControlError::NotFound);
    Fork("a", "0", 10);
    Fork("b", "0", 10);
    ASSERT_EQ(Active(), "b");

    // On b past the fork at 10: trunk (created first), a, b
    ttd::TTDReply step = Run("branch-switch", {{"to", "next_sibling"}});
    ASSERT_TRUE(step.Ok()) << step.message;
    EXPECT_EQ(SwitchedTo(step), "trunk") << "after the newest comes the oldest";
    EXPECT_EQ(step.body.find("siblings")->i, 3);
    EXPECT_EQ(step.body.find("fork_frame")->i, static_cast<int64_t>(_start + 10));
    step = Run("branch-switch", {{"to", "next_sibling"}});
    ASSERT_TRUE(step.Ok()) << step.message;
    EXPECT_EQ(SwitchedTo(step), "a");
    step = Run("branch-switch", {{"to", "prev_sibling"}});
    ASSERT_TRUE(step.Ok()) << step.message;
    EXPECT_EQ(SwitchedTo(step), "trunk");
}

/// The readouts read the published table: the active branch, how deep it is, where ttd.branch_prev goes, how many
/// branches the sibling keys cycle; the list puts the active lineage first, newest first
TEST_F(TTDControl_BranchNavigation_Test, ReadoutsShowTheActiveBranchAndTheList)
{
    StateNode one = actions::ReadReadout(*_emulator, "ttd.branch");
    EXPECT_TRUE(one.find("available")->b);
    EXPECT_EQ(one.find("name")->s, "trunk");
    EXPECT_EQ(one.find("count")->i, 1);
    EXPECT_EQ(one.find("depth")->i, 0);
    EXPECT_EQ(one.find("previous")->kind, StateNode::Kind::Null);

    Fork("a", "0", 10);
    Fork("deep", "a", 11);
    Fork("b", "0", 20);
    ASSERT_TRUE(Run("branch-switch", {{"branch", "deep"}}).Ok());
    one = actions::ReadReadout(*_emulator, "ttd.branch");
    EXPECT_EQ(one.find("name")->s, "deep");
    EXPECT_EQ(one.find("count")->i, 4);
    EXPECT_EQ(one.find("depth")->i, 2) << "trunk -> a -> deep";
    EXPECT_FALSE(one.find("pinned")->b);
    EXPECT_EQ(one.find("previous")->find("name")->s, "b");
    EXPECT_EQ(one.find("siblings")->i, 2) << "deep forks alone at 11: deep and a's own future";
    EXPECT_FALSE(one.find("age")->s.empty());
    const std::vector<StateNode>& forks = one.find("forks")->items;
    ASSERT_EQ(forks.size(), 2u) << "the trunk goes on past 10, a past 11; b leaves the trunk off this lineage";
    EXPECT_EQ(forks[0].i, static_cast<int64_t>(_start + 10));
    EXPECT_EQ(forks[1].i, static_cast<int64_t>(_start + 11));

    const StateNode list = actions::ReadReadout(*_emulator, "ttd.branch_list");
    const std::vector<StateNode>& items = list.find("items")->items;
    ASSERT_EQ(items.size(), 4u);
    EXPECT_EQ(list.find("total")->i, 4);
    EXPECT_EQ(items[0].find("name")->s, "deep") << "the lineage first, newest first";
    EXPECT_TRUE(items[0].find("active")->b);
    EXPECT_EQ(items[1].find("name")->s, "a");
    EXPECT_EQ(items[2].find("name")->s, "trunk");
    EXPECT_EQ(items[3].find("name")->s, "b");
    EXPECT_FALSE(items[3].find("on_lineage")->b);
}

/// The actions over the verbs: previous, siblings, pin (a toggle), a switch by name, the menu's list, and a delete
/// that needs its 1 s hold (or now) and never takes the active lineage
TEST_F(TTDControl_BranchNavigation_Test, ActionsDriveTheVerbs)
{
    using namespace actions;
    std::shared_ptr<Emulator> shared(_emulator, [](Emulator*) {});
    ActionRegistry registry([&shared](const std::string& id) {
        return id == shared->GetId() ? shared : std::shared_ptr<Emulator>();
    });
    std::vector<RegistrationHandle> handles = RegisterBranchActions(registry);
    ActionOrigin origin;
    origin.origin = execution::Origin::Gui;
    origin.identity = "test";
    auto press = [](StateNode params = StateNode::Object(), bool pressed = true) {
        ActionValue value;
        value.pressed = pressed;
        value.params = std::move(params);
        return value;
    };
    auto run = [&](const char* id, ActionValue value) { return registry.Run(id, std::move(value), shared->GetId(), origin); };

    EXPECT_EQ(run("ttd.branch_prev", press()).error, "not_now") << "nothing was left yet";
    EXPECT_EQ(run("ttd.branch_next_sibling", press()).error, "not_now") << "one branch";
    Fork("a", "0", 10);
    Fork("b", "0", 10);

    ActionResult result = run("ttd.branch_prev", press());
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Active(), "a");
    ASSERT_TRUE(run("ttd.branch_next_sibling", press()).ok);
    EXPECT_EQ(Active(), "b");
    ASSERT_TRUE(run("ttd.branch_prev_sibling", press()).ok);
    EXPECT_EQ(Active(), "a");

    StateNode name = StateNode::Object();
    name["branch"] = std::string("b");
    ASSERT_TRUE(run("ttd.branch_switch", press(name)).ok);
    EXPECT_EQ(Active(), "b");
    EXPECT_EQ(run("ttd.branch_switch", press()).error, "Bad Request");

    EXPECT_EQ(registry.ToggleState("ttd.branch_pin", shared->GetId()), std::optional<bool>(false));
    ASSERT_TRUE(run("ttd.branch_pin", press()).ok);
    EXPECT_EQ(registry.ToggleState("ttd.branch_pin", shared->GetId()), std::optional<bool>(true));
    ASSERT_TRUE(run("ttd.branch_pin", press()).ok);
    EXPECT_EQ(registry.ToggleState("ttd.branch_pin", shared->GetId()), std::optional<bool>(false));

    result = run("ttd.branch_menu", press());
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.body.find("items")->items.size(), 3u);

    // The delete: a short press does nothing, now deletes; the active lineage stays
    StateNode doomed = StateNode::Object();
    doomed["branch"] = std::string("a");
    ASSERT_TRUE(run("ttd.branch_delete", press(doomed)).ok) << "armed";
    EXPECT_EQ(run("ttd.branch_delete", press(doomed, false)).error, "not_now") << "released before 1 s";
    StateNode active = StateNode::Object();
    active["branch"] = std::string("b");
    active["now"] = true;
    EXPECT_FALSE(run("ttd.branch_delete", press(active)).ok) << "the active branch stays";
    doomed["now"] = true;
    result = run("ttd.branch_delete", press(doomed));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(_ttd->ReadSessionInfo().branches->branches.size(), 2u);

    handles.clear();
    registry.Shutdown();
}
