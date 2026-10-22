// The debug tools' catalogue actions (Stream Deck use cases D4, D7, D8.2, D10): breakpoints by kind, preset, PC,
// selection and group with their toggle readouts; a register, memory and a port written; the profilers, traces and
// analyzers on / off, paused, cleared with their readouts. Through ActionRegistry::Instance() on a machine that is not
// started (the managers act on the caller's thread)

#include <initializer_list>
#include <string>
#include <utility>

#include "_helpers/emulatortesthelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/readouts.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "pch.h"

using namespace actions;

namespace
{
using Params = std::initializer_list<std::pair<const char*, StateNode>>;

ActionValue Press(Params params = {})
{
    ActionValue value;
    for (const auto& [name, node] : params)
        value.params[name] = node;
    return value;
}

ActionOrigin Hands()
{
    ActionOrigin origin;
    origin.origin = execution::Origin::HostDevice;
    origin.identity = "deck-debugtools";
    origin.client = "deck-debugtools";
    return origin;
}

int64_t IntOf(const StateNode& body, const char* name)
{
    const StateNode* node = body.find(name);
    return node != nullptr && node->kind == StateNode::Kind::Int ? node->i : -1;
}
}  // namespace

class HostActionsDebugTools_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _id = _emulator->GetId();
        _breakpoints = _emulator->GetBreakpointManager();
        ASSERT_NE(_breakpoints, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    ActionResult Run(const char* id, ActionValue value)
    {
        return ActionRegistry::Instance().Run(id, std::move(value), _id, Hands());
    }

    std::optional<bool> Toggle(const char* id, Params params = {})
    {
        return ActionRegistry::Instance().ToggleState(id, _id, Press(params).params);
    }

    Emulator* _emulator = nullptr;
    BreakpointManager* _breakpoints = nullptr;
    std::string _id;
};

TEST_F(HostActionsDebugTools_Test, TheCatalogueHasTheActionsWithDescriptionsAndToggles)
{
    ActionRegistry& registry = ActionRegistry::Instance();
    for (const char* id : {"dbg.breakpoint_toggle_pc", "dbg.bp_add", "dbg.bp_remove", "dbg.bp_enable", "dbg.bp_disable",
                           "dbg.bp_toggle", "dbg.bp_group_toggle", "dbg.register_set", "mem.write", "port.out",
                           "sym.import", "sym.export", "sym.import_source", "sym.import_live", "sym.drop", "sym.set_toggle",
                           "prof.toggle", "prof.pause_toggle", "prof.clear", "analyzer.toggle", "analyzer.clear"})
    {
        const std::optional<ActionInfo> info = registry.Find(id);
        ASSERT_TRUE(info.has_value()) << id;
        EXPECT_FALSE(info->description.empty()) << id;
        EXPECT_FALSE(info->gui) << id;
    }
    for (const char* id : {"dbg.breakpoint_toggle_pc", "dbg.bp_toggle", "dbg.bp_group_toggle", "sym.set_toggle",
                           "prof.toggle", "prof.pause_toggle", "analyzer.toggle"})
        EXPECT_TRUE(registry.Find(id)->toggle) << id;
}

TEST_F(HostActionsDebugTools_Test, BreakpointAtPcTogglesWithItsState)
{
    _emulator->GetZ80State()->pc = 0x8123;
    EXPECT_EQ(Toggle("dbg.breakpoint_toggle_pc"), std::optional<bool>(false));
    ActionResult result = Run("dbg.breakpoint_toggle_pc", Press());
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Toggle("dbg.breakpoint_toggle_pc"), std::optional<bool>(true));
    EXPECT_EQ(_breakpoints->GetBreakpointsCount(), 1u);
    result = Run("dbg.breakpoint_toggle_pc", Press());
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(_breakpoints->GetBreakpointsCount(), 0u);
    // At an address instead of PC
    result = Run("dbg.breakpoint_toggle_pc", Press({{"address", StateNode("#4000")}}));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(IntOf(result.body, "address"), 0x4000);
}

TEST_F(HostActionsDebugTools_Test, AddTakesEveryKindAndThePortPresets)
{
    ActionResult result = Run("dbg.bp_add", Press({{"kind", StateNode("out")}, {"preset", StateNode("#7FFD")}}));
    ASSERT_TRUE(result.ok) << result.message;
    BreakpointSnapshot bp = _breakpoints->GetBreakpointById(static_cast<uint16_t>(IntOf(result.body, "id")));
    ASSERT_NE(bp, nullptr);
    EXPECT_EQ(bp->type, BRK_IO);
    EXPECT_EQ(bp->z80address, 0x7FFD);
    EXPECT_EQ(bp->portMask, 0x8002);

    result = Run("dbg.bp_add", Press({{"kind", StateNode("write")}, {"address", StateNode("0x5800")},
                                      {"end", StateNode("0x5AFF")}, {"hits", StateNode(">=3")},
                                      {"group", StateNode("attrs")}}));
    ASSERT_TRUE(result.ok) << result.message;
    bp = _breakpoints->GetBreakpointById(static_cast<uint16_t>(IntOf(result.body, "id")));
    ASSERT_NE(bp, nullptr);
    EXPECT_TRUE(bp->isRange);
    EXPECT_EQ(bp->hitMode, BRK_HIT_AT_LEAST);
    EXPECT_EQ(bp->group, "attrs");

    result = Run("dbg.bp_add", Press({{"kind", StateNode("keyboard")}, {"key", StateNode("enter")},
                                      {"on", StateNode("release")}}));
    ASSERT_TRUE(result.ok) << result.message;
    bp = _breakpoints->GetBreakpointById(static_cast<uint16_t>(IntOf(result.body, "id")));
    ASSERT_NE(bp, nullptr);
    EXPECT_EQ(bp->type, BRK_KEYBOARD);
    EXPECT_EQ(bp->keyType, BRK_KEY_RELEASE);

    result = Run("dbg.bp_add", Press({{"kind", StateNode("magic")}, {"opcode", StateNode("dd01")}}));
    ASSERT_TRUE(result.ok) << result.message;

    // Refusals name the field
    EXPECT_EQ(Run("dbg.bp_add", Press({{"kind", StateNode("sometimes")}})).error, "Bad Request");
    EXPECT_EQ(Run("dbg.bp_add", Press({{"kind", StateNode("exec")}, {"preset", StateNode("fe")}})).error, "Bad Request");
    EXPECT_EQ(Run("dbg.bp_add", Press({{"kind", StateNode("in")}, {"preset", StateNode("1234")}})).error, "Bad Request");
    EXPECT_EQ(Run("dbg.bp_add", Press({{"kind", StateNode("exec")}})).error, "Bad Request") << "no address";
}

TEST_F(HostActionsDebugTools_Test, SelectionsSwitchRemoveAndShowTheirState)
{
    for (const char* address : {"0x8000", "0x8010"})
        ASSERT_TRUE(Run("dbg.bp_add", Press({{"address", StateNode(address)}, {"group", StateNode("loop")}})).ok);
    ASSERT_TRUE(Run("dbg.bp_add", Press({{"kind", StateNode("in")}, {"preset", StateNode("fe")}})).ok);

    EXPECT_EQ(Toggle("dbg.bp_group_toggle", {{"group", StateNode("loop")}}), std::optional<bool>(true));
    ActionResult result = Run("dbg.bp_group_toggle", Press({{"group", StateNode("loop")}}));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(IntOf(result.body, "selected"), 2);
    EXPECT_EQ(Toggle("dbg.bp_group_toggle", {{"group", StateNode("loop")}}), std::optional<bool>(false));
    EXPECT_EQ(Toggle("dbg.bp_toggle", {{"select", StateNode("in")}}), std::optional<bool>(true));
    EXPECT_EQ(Toggle("dbg.bp_toggle", {{"select", StateNode("all")}}), std::optional<bool>(false)) << "one is off";
    EXPECT_FALSE(Toggle("dbg.bp_group_toggle", {{"group", StateNode("none")}}).has_value());

    // The readouts name them: toggle.bp_group.<group>, toggle.bp.<select>
    EXPECT_EQ(ReadReadout(*_emulator, "toggle.bp_group.loop").find("on")->b, false);
    EXPECT_EQ(ReadReadout(*_emulator, "toggle.bp.in").find("on")->b, true);
    const StateNode summary = ReadReadout(*_emulator, "debug.breakpoints");
    EXPECT_EQ(IntOf(summary, "total"), 3);
    EXPECT_EQ(IntOf(summary, "active"), 1);

    result = Run("dbg.bp_enable", Press({{"select", StateNode("all")}}));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(IntOf(result.body, "changed"), 2);
    result = Run("dbg.bp_remove", Press({{"kind", StateNode("port")}}));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(IntOf(result.body, "changed"), 1);
    EXPECT_EQ(_breakpoints->GetBreakpointsCount(), 2u);
    EXPECT_EQ(Run("dbg.bp_disable", Press({{"id", StateNode(999)}})).error, "Not Found");
    EXPECT_EQ(Run("dbg.bp_toggle", Press({{"select", StateNode("beam")}})).error, "Not Found") << "nothing selected";
    EXPECT_EQ(Run("dbg.bp_remove", Press({{"select", StateNode("whatever")}})).error, "Bad Request");
}

TEST_F(HostActionsDebugTools_Test, RegisterMemoryAndPort)
{
    ActionResult result = Run("dbg.register_set", Press({{"name", StateNode("hl")}, {"value", StateNode("#1234")}}));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(_emulator->GetZ80State()->hl, 0x1234);
    EXPECT_EQ(Run("dbg.register_set", Press({{"name", StateNode("xyz")}, {"value", StateNode(1)}})).error, "Bad Request");
    EXPECT_EQ(Run("dbg.register_set", Press({{"name", StateNode("im")}, {"value", StateNode(7)}})).error, "Bad Request")
        << "IM holds 0..2";

    result = Run("mem.write", Press({{"address", StateNode("0x8000")}, {"bytes", StateNode("3E 01 C9")}}));
    ASSERT_TRUE(result.ok) << result.message;
    Memory* memory = _emulator->GetMemory();
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x8000), 0x3E);
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x8002), 0xC9);
    result = Run("mem.write", Press({{"address", StateNode("0x9000")}, {"value", StateNode("0xAA")},
                                     {"count", StateNode(16)}}));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x900F), 0xAA);
    EXPECT_EQ(Run("mem.write", Press({{"address", StateNode("0xFFFF")}, {"bytes", StateNode("0102")}})).error,
              "Bad Request") << "past #FFFF";

    result = Run("port.out", Press({{"port", StateNode("fe")}, {"value", StateNode("2")}}));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(IntOf(result.body, "port"), 0xFE);
    EXPECT_EQ(Run("port.out", Press({{"port", StateNode("#7FFD")}})).error, "Bad Request") << "no value";
}

TEST_F(HostActionsDebugTools_Test, ProfilersAndAnalyzersToggleWithTheirState)
{
    // ~70 ms: eight profilers switched on and off (each start enables its features and clears its buffers)
    EXPECT_EQ(Toggle("prof.toggle", {{"name", StateNode("opcode")}}), std::optional<bool>(false));
    ActionResult result = Run("prof.toggle", Press({{"name", StateNode("opcode")}}));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.body.find("state")->s, "capturing");
    EXPECT_EQ(Toggle("prof.toggle", {{"name", StateNode("opcode")}}), std::optional<bool>(true));
    result = Run("prof.pause_toggle", Press({{"name", StateNode("opcode")}}));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Toggle("prof.pause_toggle", {{"name", StateNode("opcode")}}), std::optional<bool>(true));
    EXPECT_TRUE(Run("prof.clear", Press({{"name", StateNode("opcode")}})).ok);
    result = Run("prof.toggle", Press({{"name", StateNode("opcode")}}));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.body.find("state")->s, "stopped") << "a paused profiler counts as running: the toggle stops it";

    for (const char* name : {"memory", "calltrace", "coverage", "aylog", "all"})
    {
        result = Run("prof.toggle", Press({{"name", StateNode(name)}, {"on", StateNode(true)}}));
        ASSERT_TRUE(result.ok) << name << ": " << result.message;
        EXPECT_EQ(ReadReadout(*_emulator, std::string("toggle.profiler.") + name).find("on")->b, true) << name;
        result = Run("prof.toggle", Press({{"name", StateNode(name)}, {"on", StateNode(false)}}));
        ASSERT_TRUE(result.ok) << name << ": " << result.message;
    }
    EXPECT_EQ(Run("prof.pause_toggle", Press({{"name", StateNode("coverage")}})).error, "Bad Request")
        << "coverage cannot pause";
    EXPECT_EQ(Run("prof.pause_toggle", Press({{"name", StateNode("memory")}})).error, "Conflict") << "not running";
    EXPECT_EQ(Run("prof.toggle", Press({{"name", StateNode("nosuch")}})).error, "Bad Request");

    result = Run("analyzer.toggle", Press({{"name", StateNode("trdos")}}));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(Toggle("analyzer.toggle", {{"name", StateNode("trdos")}}), std::optional<bool>(true));
    EXPECT_TRUE(Run("analyzer.clear", Press({{"name", StateNode("trdos")}})).ok);
    EXPECT_TRUE(Run("analyzer.toggle", Press({{"name", StateNode("trdos")}, {"on", StateNode(false)}})).ok);
    EXPECT_EQ(Run("analyzer.toggle", Press({{"name", StateNode("nosuch")}})).error, "Not Found");
}

TEST_F(HostActionsDebugTools_Test, SymbolSetToggleRefusesAnUnknownSet)
{
    EXPECT_EQ(Run("sym.set_toggle", Press({{"id", StateNode("nosuchset")}})).error, "Not Found");
    EXPECT_FALSE(Toggle("sym.set_toggle", {{"id", StateNode("nosuchset")}}).has_value());
    EXPECT_EQ(Run("sym.drop", Press({{"id", StateNode("nosuchset")}})).error, "Not Found");
}
