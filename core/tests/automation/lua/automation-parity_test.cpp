/// @file automation-parity_test.cpp
/// @brief Surface parity of the debugger's CPU parameter (debugger model, Phase 3 stage 4): one scenario on a card
/// CPU and the main CPU (a card breakpoint hit, steps of both CPUs, registers, memory and the snapshot) run through
/// the Lua plane and through the core builders the CLI, REST, WebSocket and MCP planes answer with (RunControl,
/// CpuView, DebugSnapshot). The answers are compared field by field.
///
/// core-tests links the Lua interpreter, not the CLI processor, the drogon handlers or Python: those planes are the
/// thin adapters over the same core calls checked here (their own tests: LuaCardCpu_Test, McpDebugTools_Test).

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testwaithelper.h"
#include "automation-lua.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/snapshot/debugsnapshot.h"
#include "debugger/targets/cpuview.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/lua_emulator.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/statenodejson.h"

namespace
{
class BoundLua : public AutomationLua
{
public:
    void Bind(Emulator* emulator)
    {
        dispatchSync([this, emulator](sol::state&) { BindEmulator(emulator); });
    }

    /// The Lua expression's value as a StateNode (LuaToStateNode), read on the Lua thread
    StateNode Value(const std::string& expression)
    {
        return dispatchSync([&expression](sol::state& lua) {
            sol::protected_function_result result = lua.safe_script("return " + expression, sol::script_pass_on_error);
            if (!result.valid())
            {
                const sol::error error = result;
                StateNode failed = StateNode::Object();
                failed["lua_error"] = std::string(error.what());
                return failed;
            }
            return LuaToStateNode(result.get<sol::object>());
        });
    }
};

/// The card routine of GSCardTarget_Test, interrupts off: #4000 CALL #4010 / #4003 NOP / #4004 JR #4000 ...
/// #4010 NOP / #4011 NOP / #4012 RET Z / #4013 RET
constexpr uint16_t kCall = 0x4000;
constexpr uint16_t kRoutine = 0x4010;
constexpr uint16_t kStack = 0x7F00;

void LoadRoutine(GeneralSoundCard& card)
{
    const uint8_t code[] = {0xCD, 0x10, 0x40, 0x00, 0x18, 0xFA};
    const uint8_t routine[] = {0x00, 0x00, 0xC8, 0xC9};
    for (size_t i = 0; i < sizeof(code); i++)
        ASSERT_TRUE(card.pokeCardMemory(static_cast<uint16_t>(kCall + i), code[i]));
    for (size_t i = 0; i < sizeof(routine); i++)
        ASSERT_TRUE(card.pokeCardMemory(static_cast<uint16_t>(kRoutine + i), routine[i]));
    Z80CpuRegisters regs{};
    ASSERT_TRUE(card.cardRegisters(regs));
    regs.pc = kCall;
    regs.sp = kStack;
    regs.af = 0x0000;
    regs.iff1 = regs.iff2 = 0;
    regs.halted = 0;
    ASSERT_TRUE(card.setCardRegisters(regs));
}

/// Field-by-field: every member of `core` in `lua` with an equal value (a null member is one Lua leaves out, an empty
/// array an empty table), and no member of `lua` that `core` lacks. `skip` names members not compared at any depth
void Compare(const StateNode& core, const StateNode& lua, const std::string& path, std::vector<std::string>& diffs,
             const std::vector<std::string>& skip = {})
{
    using Kind = StateNode::Kind;
    const bool coreEmpty = (core.kind == Kind::Array && core.items.empty()) ||
                           (core.kind == Kind::Object && core.members.empty());
    const bool luaEmpty = lua.kind == Kind::Null || (lua.kind == Kind::Object && lua.members.empty()) ||
                          (lua.kind == Kind::Array && lua.items.empty());
    if (core.kind == Kind::Null || coreEmpty)
    {
        if (!luaEmpty)
            diffs.push_back(path + ": Lua has " + StateNodeToJsonText(lua));
        return;
    }
    const bool numbers = (core.kind == Kind::Int || core.kind == Kind::Double) &&
                         (lua.kind == Kind::Int || lua.kind == Kind::Double);
    if (numbers)
    {
        const double a = core.kind == Kind::Int ? static_cast<double>(core.i) : core.d;
        const double b = lua.kind == Kind::Int ? static_cast<double>(lua.i) : lua.d;
        if (a != b)
            diffs.push_back(path + ": " + StateNodeToJsonText(core) + " != " + StateNodeToJsonText(lua));
        return;
    }
    if (core.kind != lua.kind)
    {
        diffs.push_back(path + ": " + StateNodeToJsonText(core) + " != Lua " + StateNodeToJsonText(lua));
        return;
    }
    auto skipped = [&skip](const std::string& name) {
        for (const std::string& s : skip)
            if (s == name)
                return true;
        return false;
    };
    switch (core.kind)
    {
        case Kind::Bool:
            if (core.b != lua.b)
                diffs.push_back(path + ": bool differs");
            break;
        case Kind::String:
            if (core.s != lua.s)
                diffs.push_back(path + ": \"" + core.s + "\" != \"" + lua.s + "\"");
            break;
        case Kind::Array:
            if (core.items.size() != lua.items.size())
            {
                diffs.push_back(path + ": " + std::to_string(core.items.size()) + " items != " +
                                std::to_string(lua.items.size()));
                break;
            }
            for (size_t i = 0; i < core.items.size(); i++)
                Compare(core.items[i], lua.items[i], path + "[" + std::to_string(i) + "]", diffs, skip);
            break;
        case Kind::Object:
            for (const auto& [name, value] : core.members)
            {
                if (skipped(name))
                    continue;
                const StateNode* other = lua.find(name);
                Compare(value, other ? *other : StateNode(), path + "." + name, diffs, skip);
            }
            for (const auto& [name, value] : lua.members)
                if (!skipped(name) && !core.find(name))
                    diffs.push_back(path + "." + name + ": only in Lua");
            break;
        default: break;
    }
}

std::string Diffs(const std::vector<std::string>& diffs)
{
    std::string text;
    for (const std::string& d : diffs)
        text += "\n  " + d;
    return text;
}
}  // namespace

// The scenario on each plane from the same start: a card breakpoint ends a step over the CALL, then a card step and a
// main CPU step; the card's replies agree field by field (the breakpoint's own id and where the main CPU parked aside),
// and at one paused moment the card CPU's registers, memory and snapshot and the main CPU's snapshot read the same on
// both. Boot-bound, turbo
TEST(AutomationParity_Test, ACardStopStepsAndReadsAgreeOnTheLuaAndCorePlanes)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::NGS));
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->EnableTurboMode();
    emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return context->emulatorState.frame_counter > 2; }));
    emulator->Pause();
    ASSERT_TRUE(TestWait::For([&] { return emulator->IsEmulationParked(); }));
    GeneralSoundCard* card = context->pSoundManager->getGeneralSound();
    ASSERT_NE(card, nullptr);

    std::shared_ptr<Emulator> shared = EmulatorManager::GetInstance()->GetEmulator(emulator->GetId());
    ASSERT_NE(shared, nullptr);
    execution::RunControl control(shared);
    const execution::Requester requester{execution::Origin::WebApi, {}};
    BreakpointManager* breakpoints = context->pDebugManager->GetBreakpointsManager(DebugCpuId::GS);
    ASSERT_NE(breakpoints, nullptr);

    BoundLua lua;
    lua.start();
    lua.Bind(emulator);

    // Where both CPUs stand moves with each run: the main CPU's position and the frame are not the plane's answer
    const std::vector<std::string> moving = {"main_pc", "main_sp", "card_cycle", "main_t", "frame", "main", "ok"};
    struct Replies
    {
        StateNode hit, step;
        uint16_t breakpoint = 0;
    };
    Replies core, script;

    // The core plane (CLI, REST, WebSocket, MCP): RunControl::CardStep, the breakpoint manager
    LoadRoutine(*card);
    std::string error;
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_EXECUTE;
    spec.address = kRoutine + 1;
    core.breakpoint = breakpoints->AddBreakpoint(spec, error);
    ASSERT_NE(core.breakpoint, BRK_INVALID) << error;
    execution::RunReply reply = control.CardStep(requester, DebugCpuId::GS, "over", 1);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    core.hit = reply.body;
    breakpoints->RemoveBreakpointByID(core.breakpoint);
    reply = control.CardStep(requester, DebugCpuId::GS, "in", 1);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    core.step = reply.body;
    // A main CPU step on the core plane (before the Lua plane: a script's session then holds run control)
    reply = control.StepIn(requester);
    ASSERT_TRUE(reply.Ok()) << reply.message;

    // The Lua plane, from the same start
    LoadRoutine(*card);
    const StateNode id = lua.Value("bp(" + std::to_string(kRoutine + 1) + ", {cpu = 'gs'})");
    ASSERT_EQ(id.kind, StateNode::Kind::Int) << StateNodeToJsonText(id);
    script.breakpoint = static_cast<uint16_t>(id.i);
    script.hit = lua.Value("stepover('gs')");
    ASSERT_TRUE(lua.Value("bp_remove(" + std::to_string(script.breakpoint) + ", 'gs')").b);
    script.step = lua.Value("step('gs')");

    std::vector<std::string> diffs;
    Compare(core.hit, script.hit, "hit", diffs, moving);
    Compare(core.step, script.step, "step", diffs, moving);
    // The breakpoint ids are each plane's own; the rest of the stop is the same
    const StateNode* coreStop = core.hit.find("stop");
    const StateNode* luaStop = script.hit.find("stop");
    ASSERT_NE(coreStop, nullptr) << StateNodeToJsonText(core.hit);
    ASSERT_NE(luaStop, nullptr) << StateNodeToJsonText(script.hit);
    EXPECT_EQ(coreStop->find("reason")->s, "breakpoint");
    EXPECT_EQ(coreStop->find("breakpoint_id")->i, core.breakpoint);
    EXPECT_EQ(luaStop->find("breakpoint_id")->i, script.breakpoint);
    EXPECT_EQ(core.hit.find("pc")->i, kRoutine + 1);
    for (std::string& d : diffs)
        if (d.rfind("hit.stop.breakpoint_id", 0) == 0)
            d.clear();
    std::erase(diffs, std::string());
    EXPECT_TRUE(diffs.empty()) << "the card's breakpoint hit and step:" << Diffs(diffs);

    // A main CPU step on the Lua plane: each plane's own reply shape (the main step predates the CPU parameter)
    const StateNode mainStep = lua.Value("step()");
    EXPECT_NE(mainStep.kind, StateNode::Kind::Null);
    EXPECT_FALSE(mainStep.find("lua_error")) << StateNodeToJsonText(mainStep);

    // One paused moment: every read of both CPUs on both planes
    diffs.clear();
    Compare(CpuView::FlatRegisters(context, DebugCpuId::GS), lua.Value("get_registers('gs')"), "regs", diffs);
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(CpuView::Read(context, DebugCpuId::GS, kCall, 16, bytes));
    StateNode memory = StateNode::Array();
    for (uint8_t b : bytes)
        memory.push(StateNode(static_cast<int64_t>(b)));
    Compare(memory, lua.Value("mem_read_block(" + std::to_string(kCall) + ", 16, 'gs')"), "memory", diffs);

    DebugSnapshot::Options options;
    options.rawBytes = true;
    options.disasm = 4;
    options.memory = {"cpu:0x4000:16"};
    options.cpu = DebugCpuId::GS;
    const DebugSnapshot::Result cardSnapshot = DebugSnapshot::Build(emulator, options);
    ASSERT_TRUE(cardSnapshot.error.empty()) << cardSnapshot.error;
    Compare(cardSnapshot.snapshot,
            lua.Value("debug_snapshot({cpu = 'gs', disasm = 4, memory = {'cpu:0x4000:16'}})"), "gs snapshot", diffs);
    EXPECT_EQ(cardSnapshot.snapshot.find("debug_cpu")->s, "gs");
    EXPECT_EQ(cardSnapshot.snapshot.find("cpu")->s, "z80");

    options.cpu = DebugCpuId::Main;
    const DebugSnapshot::Result mainSnapshot = DebugSnapshot::Build(emulator, options);
    ASSERT_TRUE(mainSnapshot.error.empty()) << mainSnapshot.error;
    Compare(mainSnapshot.snapshot, lua.Value("debug_snapshot({disasm = 4, memory = {'cpu:0x4000:16'}})"),
            "main snapshot", diffs);
    EXPECT_EQ(mainSnapshot.snapshot.find("debug_cpu")->s, "main");
    EXPECT_TRUE(diffs.empty()) << "the reads:" << Diffs(diffs);

    // A refusal carries the same code on both: the PC history is the main CPU's
    options.cpu = DebugCpuId::GS;
    options.pchist = 4;
    const DebugSnapshot::Result refused = DebugSnapshot::Build(emulator, options);
    EXPECT_STREQ(refused.code, "not_supported");
    const StateNode luaCode = lua.Value("select(3, debug_snapshot({cpu = 'gs', pchist = 4}))");
    EXPECT_EQ(luaCode.s, refused.code);

    lua.stop();
    emulator->Stop();
    EmulatorTestHelper::CleanupEmulator(emulator);
}
