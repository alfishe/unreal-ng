// AutomationLua::executeCode: an error the script raises (a nil index, error(), a syntax error) answers an error
// with Lua's message and leaves the interpreter running; an unprotected call used to terminate the process.
// Script callbacks (docs/features/script-callbacks.md): on_tap's function runs on the Lua thread with the hit

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "automation-lua.h"  // its LuaEmulator declares the Automation singleton
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/soundmanager.h"
#include "hooks/luahookhost.h"

// The Lua bindings call the Automation singleton only for the videowall sync mode; automation.cpp brings every
// automation module (WebAPI, Drogon) along, so core-tests defines the two members here and no test calls them
Automation& Automation::GetInstance()
{
    std::abort();
}

bool Automation::SetVideowallSingleSyncMode(bool, const std::string&)
{
    return false;
}

namespace
{
class AutomationLua_Test : public ::testing::Test
{
protected:
    AutomationLua _lua;

    void SetUp() override { _lua.start(); }
    void TearDown() override { _lua.stop(); }

    const void* PrintFunction()
    {
        return _lua.dispatchSync([](sol::state& lua) { return lua["print"].get<sol::function>().pointer(); });
    }
};
} // namespace

TEST_F(AutomationLua_Test, AScriptErrorAnswersAnErrorAndTheInterpreterKeepsRunning)
{
    const struct
    {
        const char* code;
        const char* message;
    } errors[] = {
        {"local t = nil; return t.x", "attempt to index"},
        {"error('boom')", "boom"},
        {"print(", "unexpected symbol"},
    };
    const void* print = PrintFunction();
    for (const auto& error : errors)
    {
        std::string message;
        std::string output;
        EXPECT_FALSE(_lua.executeCode(error.code, message, output)) << error.code;
        EXPECT_NE(message.find(error.message), std::string::npos) << error.code << ": " << message;
        EXPECT_EQ(PrintFunction(), print) << error.code << ": print still captures";
    }

    std::string message;
    std::string output;
    ASSERT_TRUE(_lua.executeCode("print('alive', 1 + 1)", message, output)) << message;
    EXPECT_EQ(output, "alive\t2\n");
    ASSERT_TRUE(_lua.executeCode("print('all', nil, 3, 4, 5, 6, 7)", message, output)) << message;
    EXPECT_EQ(output, "all\tnil\t3\t4\t5\t6\t7\n") << "every argument, nil included, as Lua's print";
}

/// The headless runner's file (unreal-qt --script): the chunk's result is the exit code; an error is 1 with its message
TEST_F(AutomationLua_Test, RunFileAnswersTheScriptsExitCode)
{
    const std::filesystem::path dir = TestPathHelper::GetUniqueTestScratchPath("runfile");
    std::filesystem::create_directories(dir);
    const struct
    {
        const char* code;
        int exitCode;
        const char* message;
    } cases[] = {
        {"return 3", 3, ""},
        {"return true", 0, ""},
        {"return false", 1, ""},
        {"local x = 1", 0, ""},
        {"return 'text'", 1, "not an exit code"},
        {"error('boom')", 1, "boom"},
    };
    int index = 0;
    for (const auto& c : cases)
    {
        const std::filesystem::path file = dir / ("script" + std::to_string(index++) + ".lua");
        std::ofstream(file) << c.code;
        std::string message;
        EXPECT_EQ(_lua.runFile(file.string(), message), c.exitCode) << c.code;
        EXPECT_NE(message.find(c.message), std::string::npos) << c.code << ": " << message;
        if (!*c.message)
            EXPECT_TRUE(message.empty()) << c.code << ": " << message;
    }
    std::string message;
    EXPECT_EQ(_lua.runFile((dir / "absent.lua").string(), message), 1);
    EXPECT_FALSE(message.empty());
    EXPECT_TRUE(_lua.ready());
}

namespace
{
/// The Lua thread's emulator binding, for a test emulator the manager does not know
class BoundLua : public AutomationLua
{
public:
    void Bind(Emulator* emulator)
    {
        dispatchSync([this, emulator](sol::state&) { _luaEmulator->setEmulator(emulator); });
    }
};

/// Each frame (the ROM's interrupt wakes the HALT): LD A,(#9000); DEC A; LD (#9000),A; INC A; LD (#9000),A
const uint8_t kWrites[] = {0xFB, 0x76, 0x3A, 0x00, 0x90, 0x3D, 0x32, 0x00, 0x90,
                           0x3C, 0x32, 0x00, 0x90, 0xC3, 0x00, 0x80};
}  // namespace

TEST(AutomationLuaCallbacks_Test, OnTapRunsTheFunctionOnTheLuaThreadAndGoesWithTheScript)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    for (size_t i = 0; i < sizeof(kWrites); i++)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), kWrites[i]);
    context->pMemory->DirectWriteToZ80Memory(0x9000, 5);
    context->pCore->GetZ80()->pc = 0x8000;
    context->pCore->GetZ80()->sp = 0x7F00;
    context->pCore->GetZ80()->iy = 0x5C3A;  // the ROM's frame interrupt keeps its system variables there
    ScriptCallbackManager* callbacks = context->pDebugManager->GetScriptCallbacks();

    {
        BoundLua lua;
        lua.start();
        lua.Bind(emulator);
        std::string message;
        std::string output;
        ASSERT_TRUE(lua.executeCode(R"(
            values = {}
            tap = on_tap({write = 0x9000, ["if"] = "MDT < 5"}, function(hit) values[#values + 1] = hit.value end,
                         {label = "lives"})
            bad = on_tap({write = 0x9000}, function(hit) error("boom") end)
            refused, why = on_tap({write = 0x9000, ["if"] = "RD == 1"}, function() end)
            print(tap, bad, refused, why ~= nil)
        )", message, output)) << message;
        EXPECT_EQ(output, "1\t2\t-1\ttrue\n");

        emulator->RunNFrames(3, false);
        EXPECT_TRUE(TestWait::For([&lua]() { return lua.dispatchSync([](sol::state& state) {
            return state["values"].get<sol::table>().size() >= 2;
        }); })) << "the wake runs the function without a task";
        ASSERT_TRUE(lua.executeCode("callbacks_run(); for _, v in ipairs(values) do assert(v == 4) end", message, output))
            << message;

        CallbackInfo info;
        ASSERT_TRUE(callbacks->Get(2, info));
        EXPECT_EQ(info.owner, "lua");
        EXPECT_GE(info.stats.errors, 2u);
        EXPECT_NE(info.stats.lastError.find("boom"), std::string::npos) << info.stats.lastError;
        ASSERT_TRUE(callbacks->Get(1, info));
        EXPECT_EQ(info.label, "lives");
        EXPECT_EQ(info.stats.errors, 0u);
        EXPECT_EQ(info.stats.calls, info.stats.hits);

        ASSERT_TRUE(lua.executeCode("local list = callbacks(); assert(#list == 2 and list[1].label == 'lives')"
                                    " assert(callback_remove(2)) assert(not callback_remove(2))",
                                    message, output))
            << message;
        lua.stop();
    }
    EXPECT_TRUE(callbacks->List().empty()) << "the script's callbacks go with its interpreter";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The hook state (docs/features/script-callbacks.md "Hook state"): a chunk compiled per callback, run on the
/// emulation thread inside the access (sync) with the machine API and an instruction budget
class LuaHookHost_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ScriptCallbackManager* _callbacks = nullptr;
    ScriptHookHostFactory _factory = nullptr;
    std::unique_ptr<ttd::TimeTravelController> _controller;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        for (size_t i = 0; i < sizeof(kWrites); i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), kWrites[i]);
        _context->pMemory->DirectWriteToZ80Memory(0x9000, 5);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->sp = 0x7F00;
        z80->iy = 0x5C3A;
        z80->hl = 0x1234;
        _callbacks = _context->pDebugManager->GetScriptCallbacks();
        _factory = GetScriptHookHostFactory();
        SetScriptHookHostFactory(&LuaHookHost::Create);
    }

    void TearDown() override
    {
        if (_callbacks)
            _callbacks->RemoveAll();
        if (_controller)
        {
            _controller->StopRecording();
            _context->pTimeTravelHooks = _context->pTimeTravelManager;
            _context->ttdWriteSink = _context->pTimeTravelManager;
            _context->pTimeTravelController = nullptr;
            _controller.reset();
        }
        SetScriptHookHostFactory(_factory);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint32_t SyncTap(const std::string& chunk, uint64_t instructions = 0, CallbackRole role = CallbackRole::View)
    {
        CallbackSpec spec;
        spec.trigger = CallbackTrigger::Tap;
        spec.tap.type = BRK_MEMORY;
        spec.tap.access = BRK_MEM_WRITE;
        spec.tap.address = 0x9000;
        spec.tap.condition = "MDT < 5";
        spec.owner = "test";
        spec.mode = CallbackMode::Sync;
        spec.chunk = chunk;
        spec.instructions = instructions;
        spec.role = role;
        std::string error;
        const uint32_t id = _callbacks->Register(spec, error);
        EXPECT_NE(id, 0u) << error;
        return id;
    }

    CallbackInfo Info(uint32_t id)
    {
        CallbackInfo info;
        EXPECT_TRUE(_callbacks->Get(id, info));
        return info;
    }
};

TEST_F(LuaHookHost_Test, AHookReadsAndWritesTheMachineAndSharesValues)
{
    const uint32_t id = SyncTap(R"(
        local count = 0
        return function(hit)
            count = count + 1
            shared_set("count", count)
            shared_set("value", hit.value)
            shared_set("peek", peek(0x9000))
            shared_set("hl", reg("hl"))
            shared_set("same_frame", frame() == hit.frame)
            poke(0x9100, hit.value + 0x10)
        end)");
    _emulator->RunNFrames(3, false);
    const CallbackInfo info = Info(id);
    EXPECT_EQ(info.stats.errors, 0u) << info.stats.lastError;
    ASSERT_GE(info.stats.calls, 2u);
    EXPECT_EQ(_callbacks->SharedGet("count").i, static_cast<int64_t>(info.stats.calls)) << "one state for every call";
    EXPECT_EQ(_callbacks->SharedGet("value").i, 4);
    EXPECT_EQ(_callbacks->SharedGet("peek").i, 4) << "inside the access: the byte just written";
    EXPECT_EQ(_callbacks->SharedGet("hl").i, 0x1234);
    EXPECT_TRUE(_callbacks->SharedGet("same_frame").b);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9100), 0x14);

    std::string error;
    CallbackSpec spec;
    spec.trigger = CallbackTrigger::GameEvent;
    spec.event = "x";
    spec.mode = CallbackMode::Frame;
    spec.chunk = "return 5";
    EXPECT_EQ(_callbacks->Register(spec, error), 0u);
    EXPECT_NE(error.find("returns its function"), std::string::npos) << error;
    spec.chunk = "return function(";
    EXPECT_EQ(_callbacks->Register(spec, error), 0u);
    EXPECT_EQ(error.rfind("hook: ", 0), 0u) << error;
}

TEST_F(LuaHookHost_Test, ARunawayHookStopsAtItsBudgetAndCannotCatchIt)
{
    const uint32_t runaway = SyncTap("return function() local n = 0 while true do n = n + 1 end end", 5000);
    const uint32_t catcher = SyncTap("return function() pcall(function() while true do end end) end");
    const uint32_t opener = SyncTap("return function() io.open('x') end");
    const uint64_t frame = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(2, false);
    EXPECT_EQ(_context->emulatorState.frame_counter, frame + 2) << "the machine ran on";

    CallbackInfo info = Info(runaway);
    EXPECT_EQ(info.stats.calls, 1u);
    EXPECT_EQ(info.stats.aborted, 1u);
    EXPECT_EQ(info.state, CallbackState::Suspended);
    EXPECT_NE(info.reason.find("5000 instructions"), std::string::npos) << info.reason;
    EXPECT_NE(info.stats.lastError.find("instruction budget of 5000"), std::string::npos) << info.stats.lastError;
    info = Info(catcher);
    EXPECT_EQ(info.stats.aborted, 0u) << "no pcall in the hook state";
    EXPECT_NE(info.stats.lastError.find("pcall"), std::string::npos) << info.stats.lastError;
    info = Info(opener);
    EXPECT_NE(info.stats.lastError.find("io"), std::string::npos) << "no io library: " << info.stats.lastError;
}

TEST_F(LuaHookHost_Test, AHookDoesNotChangeTheMachineWhileTimeTravelRecords)
{
    _controller = std::make_unique<ttd::TimeTravelController>(_context);
    _context->pTimeTravelHooks = _controller.get();
    _context->ttdWriteSink = _controller.get();
    _context->pTimeTravelController = _controller.get();
    ASSERT_TRUE(ttd::TTDControl(_context).Execute({"start", {}}).Ok());

    const uint32_t id = SyncTap(R"(return function(hit)
        shared_set("seen", peek(0x9000))
        poke(0x9100, 1)
    end)");
    _emulator->RunNFrames(2, true);
    const CallbackInfo info = Info(id);
    ASSERT_GE(info.stats.calls, 1u);
    EXPECT_EQ(info.stats.errors, info.stats.calls);
    EXPECT_NE(info.stats.lastError.find("time travel"), std::string::npos) << info.stats.lastError;
    EXPECT_EQ(_callbacks->SharedGet("seen").i, 4) << "reading is fine";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9100), 0);
}

/// §6: a logic callback writes while TTD records; its writes land at the boundary and are an edit the replay applies
/// without running the callback again
TEST_F(LuaHookHost_Test, ALogicHookIsRecordedAndReplayedWithoutRunningIt)
{
    _controller = std::make_unique<ttd::TimeTravelController>(_context);
    _context->pTimeTravelHooks = _controller.get();
    _context->ttdWriteSink = _controller.get();
    _context->pTimeTravelController = _controller.get();
    ASSERT_TRUE(ttd::TTDControl(_context).Execute({"start", {}}).Ok());

    const uint32_t id = SyncTap(R"(return function(hit)
        local n = peek(0x9100) + 1
        poke(0x9100, n)
        poke(0x9101, math.random(0, 255))
        set_reg("de", 0x4000 + n)
        shared_set("unchanged", peek(0x9100) == n - 1)
    end)", 0, CallbackRole::Logic);
    _emulator->RunNFrames(6, true);
    Z80* z80 = _context->pCore->GetZ80();
    const CallbackInfo info = Info(id);
    ASSERT_GE(info.stats.calls, 4u);
    EXPECT_EQ(info.stats.errors, 0u) << info.stats.lastError;
    EXPECT_TRUE(_callbacks->SharedGet("unchanged").b) << "the writes wait for the boundary";
    const uint8_t count = _context->pMemory->DirectReadFromZ80Memory(0x9100);
    const uint8_t random = _context->pMemory->DirectReadFromZ80Memory(0x9101);
    EXPECT_EQ(count, info.stats.calls);
    EXPECT_EQ(z80->de, 0x4000 + count);
    const ttd::TTDTimePoint end = _controller->CurrentPosition();
    _controller->StopRecording();

    ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(1)->time.frame, 0}, nullptr));
    const uint8_t early = _context->pMemory->DirectReadFromZ80Memory(0x9100);
    EXPECT_LT(early, count);
    ASSERT_TRUE(_controller->SeekTo(end, nullptr));
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9100), count) << "the recorded writes, replayed";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9101), random);
    EXPECT_EQ(z80->de, 0x4000 + count) << "and the register";
    EXPECT_EQ(Info(id).stats.calls, info.stats.calls) << "the callback does not run on replay";
}

TEST_F(LuaHookHost_Test, AHookHasNoHostRandomness)
{
    const std::string chunk = R"(return function() shared_set("r", math.random(0, 1 << 30)) end)";
    std::vector<int64_t> values;
    for (int run = 0; run < 2; run++)
    {
        LuaHookHost host(_context);
        std::string error;
        ASSERT_TRUE(host.Compile(1, chunk, error)) << error;
        const HookCallResult result = host.Call(1, CallbackHit{}, "", 100000, nullptr);
        ASSERT_TRUE(result.ok) << result.error;
        values.push_back(_callbacks->SharedGet("r").i);
    }
    EXPECT_EQ(values[0], values[1]) << "a fixed seed";
    LuaHookHost host(_context);
    std::string error;
    ASSERT_TRUE(host.Compile(1, "return function() math.randomseed() end", error)) << error;
    const HookCallResult result = host.Call(1, CallbackHit{}, "", 100000, nullptr);
    EXPECT_FALSE(result.ok) << "a reseed names its seed";
}

TEST_F(LuaHookHost_Test, OnTapTakesAChunkFromTheScript)
{
    SetScriptHookHostFactory(nullptr);  // AutomationLua::start installs it
    BoundLua lua;
    lua.start();
    lua.Bind(_emulator);
    std::string message;
    std::string output;
    ASSERT_TRUE(lua.executeCode(R"(
        shared_set("limit", 3)
        sync = on_tap({write = 0x9000, ["if"] = "MDT < 5"},
                      [[return function(hit) shared_set("seen", (shared_get("seen") or 0) + 1) end]],
                      {mode = "sync", instructions = 2000, policy = "unpaced"})
        framed = on_tap({write = 0x9000}, [[return function(hit) shared_set("framed", shared_get("limit")) end]])
        wrong, why = on_tap({write = 0x9000}, function() end, {mode = "sync"})
        print(sync, framed, wrong, why)
    )", message, output)) << message;
    EXPECT_EQ(output, "1\t2\t-1\ta function runs deferred: frame and sync take a chunk (Lua source)\n");
    _emulator->RunNFrames(2, false);
    ASSERT_TRUE(lua.executeCode(R"(
        local list = callbacks()
        assert(list[1].mode == "sync" and list[1].instructions == 2000 and list[1].policy == "unpaced", list[1].mode)
        assert(list[2].mode == "frame" and list[2].aborted == 0)
        assert(shared_get("seen") >= 1 and shared_get("framed") == 3)
        assert(callback_hooks().unpaced == false)
    )", message, output)) << message;
    lua.stop();
    EXPECT_TRUE(_callbacks->List().empty()) << "a script's hooks go with it";
}

/// The sound card CPU's hooks (gs-debugger design §8A.7): a sync hook on the card's port read hands the card the byte
/// it returns, an exec hook reads the card's registers, a card event tap sees the firmware's page switches. The
/// PENTAGON with a classic General Sound: gs105a's POST, then (the host took the reply) its mailbox poll
/// IN A,(4) at #026E, RRCA at #0270
TEST(LuaGSCardHook_Test, ACardReadHookReplacesTheByteTheCardSees)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::Z80));
    auto* card = dynamic_cast<SoundChip_GeneralSound*>(context->pSoundManager->getGeneralSound());
    ASSERT_NE(card, nullptr);
    ScriptCallbackManager* callbacks = context->pDebugManager->GetScriptCallbacks();

    BoundLua lua;
    lua.start();
    lua.Bind(emulator);
    std::string message;
    std::string output;
    ASSERT_TRUE(lua.executeCode(R"(
        pages = on_tap({event = "page"}, [[return function(hit)
            shared_set("pages", (shared_get("pages") or 0) + 1)
            shared_set("page_kind", hit.kind .. " " .. hit.event .. " " .. hit.cpu)
        end]], {mode = "sync"})
        poll = on_tap({port_in = 4, mask = 0xFF, cpu = "gs"}, [[return function(hit)
            shared_set("polled", hit.value)
            return 0x5A
        end]], {mode = "sync"})
        rrca = on_tap({exec = 0x0270, cpu = "gs"}, [[return function(hit)
            if shared_get("a") == nil then shared_set("a", reg("a")) end
            shared_set("pc", hit.regs.pc)
            shared_set("code", peek(0x026E))
        end]], {mode = "sync"})
        main, why = on_tap({event = "int", cpu = "main"}, function() end)
        print(pages, poll, rrca, main, why)
    )", message, output)) << message;
    EXPECT_EQ(output, "1\t2\t3\t-1\ta main CPU tap callback watches the machine's events (cpu_int, fdc_command, ...)\n");

    for (int i = 0; i < 200 && card->getActivityCounters().volumeLatchWrites < 4; i++)
        emulator->RunNFrames(1, false);
    emulator->RunNFrames(1, false);
    if (card->readStatus() & 0x80)
        (void)card->portDeviceInMethod(GeneralSoundCard::PORT_DATA);
    for (int i = 0; i < 10 && callbacks->SharedGet("a").kind == CallbackValue::Kind::Nil; i++)
        emulator->RunNFrames(1, false);

    for (const CallbackInfo& info : callbacks->List())
        EXPECT_EQ(info.stats.errors, 0u) << info.target << ": " << info.stats.lastError;
    EXPECT_GE(callbacks->SharedGet("pages").i, 1) << "POST maps its pages";
    EXPECT_EQ(callbacks->SharedGet("page_kind").s, "card_event page gs");
    EXPECT_EQ(callbacks->SharedGet("polled").kind, CallbackValue::Kind::Integer) << "the card read port 4";
    EXPECT_EQ(callbacks->SharedGet("a").i, 0x5A) << "IN A,(4) took the hook's byte";
    EXPECT_EQ(callbacks->SharedGet("pc").i, 0x0270) << "the card's registers in the hit";
    EXPECT_EQ(callbacks->SharedGet("code").i, 0xDB) << "peek reads the card's memory (IN A,(n))";
    const std::vector<CallbackInfo> list = callbacks->List();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[1].cpu, DebugCpuId::GS);
    EXPECT_EQ(list[1].target, "tap in #0004 (gs CPU)");
    lua.stop();
    EXPECT_TRUE(callbacks->List().empty());
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// Q3 (gs-debugger design §8A.7): a logic hook on the card writes the card while TTD records - the byte its read
/// sees, a poke and a register - and the track keeps them; a seek back and forward replays them without the hook, and
/// a fork from a checkpoint records its own
TEST(LuaGSCardHook_Test, ALogicCardHookIsRecordedAndReplayedWithoutRunningIt)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::Z80));
    auto* card = dynamic_cast<SoundChip_GeneralSound*>(context->pSoundManager->getGeneralSound());
    ASSERT_NE(card, nullptr);
    ScriptCallbackManager* callbacks = context->pDebugManager->GetScriptCallbacks();
    // POST done and the reply taken: the firmware polls IN A,(4) at #026E
    for (int i = 0; i < 200 && card->getActivityCounters().volumeLatchWrites < 4; i++)
        emulator->RunNFrames(1, false);
    emulator->RunNFrames(1, false);
    if (card->readStatus() & 0x80)
        (void)card->portDeviceInMethod(GeneralSoundCard::PORT_DATA);

    ASSERT_EQ(card->mapCardAddress(0x6000).kind, GeneralSoundCard::CardPage::Ram) << "the hook's poke lands in RAM";

    auto controller = std::make_unique<ttd::TimeTravelController>(context);
    context->pTimeTravelHooks = controller.get();
    context->ttdWriteSink = controller.get();
    context->pTimeTravelController = controller.get();
    ASSERT_TRUE(ttd::TTDControl(context).Execute({"start", {}}).Ok());

    BoundLua lua;
    lua.start();
    lua.Bind(emulator);
    std::string message;
    std::string output;
    ASSERT_TRUE(lua.executeCode(R"(
        poll = on_tap({port_in = 4, mask = 0xFF, cpu = "gs"}, [[return function(hit)
            local n = (peek(0x6000) + (shared_get("step") or 1)) % 256
            poke(0x6000, n)
            set_reg("i", n)
            shared_set("unchanged", peek(0x6000) ~= n)
            return 0x5A
        end]], {mode = "sync", role = "logic", policy = "ignore"})
        rrca = on_tap({exec = 0x0270, cpu = "gs"}, [[return function(hit)
            if shared_get("a") == nil then shared_set("a", reg("a")) end
        end]], {mode = "sync", policy = "ignore"})
    )", message, output)) << message;
    // policy "ignore": on a loaded host a call over the time budget would suspend the hook within the first
    // frame, every poke would land before checkpoint 1 and the seek there would find the last count
    emulator->RunNFrames(4, true);

    for (const CallbackInfo& info : callbacks->List())
        EXPECT_EQ(info.stats.errors, 0u) << info.target << ": " << info.stats.lastError;
    const CallbackInfo poll = callbacks->List()[0];
    ASSERT_GE(poll.stats.calls, 4u);
    EXPECT_TRUE(callbacks->SharedGet("unchanged").b) << "the writes wait for the card's boundary";
    EXPECT_EQ(callbacks->SharedGet("a").i, 0x5A) << "IN A,(4) took the hook's byte";
    uint8_t count = 0;
    ASSERT_TRUE(card->peekCardMemory(0x6000, count));
    Z80CpuRegisters regs{};
    ASSERT_TRUE(card->cardRegisters(regs));
    EXPECT_EQ(regs.i, count) << "set_reg made at the same boundary";
    const ttd::TTDTimePoint end = controller->CurrentPosition();
    controller->StopRecording();

    callbacks->SharedSet("a", CallbackValue{});
    ASSERT_TRUE(controller->SeekTo({controller->GetCheckpoint(1)->time.frame, 0}, nullptr));
    uint8_t early = 0;
    ASSERT_TRUE(card->peekCardMemory(0x6000, early));
    EXPECT_NE(early, count);
    ASSERT_TRUE(controller->SeekTo(end, nullptr));
    uint8_t replayed = 0;
    ASSERT_TRUE(card->peekCardMemory(0x6000, replayed));
    EXPECT_EQ(replayed, count) << "the recorded pokes, replayed";
    Z80CpuRegisters after{};
    ASSERT_TRUE(card->cardRegisters(after));
    EXPECT_EQ(after.i, regs.i) << "and the register";
    EXPECT_EQ(after.pc, regs.pc) << "the card at the same instruction";
    EXPECT_EQ(after.af, regs.af);
    EXPECT_EQ(callbacks->List()[0].stats.calls, poll.stats.calls) << "the hook does not run on replay";
    EXPECT_EQ(context->pDebugManager->GetGSTarget()->PendingEdits(), 0u);

    // A fork: back to the checkpoint, the recording resumes there with the hook writing other bytes; the fork's track
    // keeps its own edits (the old ones after the point went with the old future)
    const ttd::TTDTimePoint from{controller->GetCheckpoint(1)->time.frame, 0};
    ASSERT_TRUE(controller->ResumeRecordingFrom(from));
    CallbackValue step;
    step.kind = CallbackValue::Kind::Integer;
    step.i = 3;
    callbacks->SharedSet("step", step);
    emulator->RunNFrames(4, true);
    for (const CallbackInfo& info : callbacks->List())
        EXPECT_EQ(info.stats.errors, 0u) << info.target << ": " << info.stats.lastError;
    uint8_t forked = 0;
    ASSERT_TRUE(card->peekCardMemory(0x6000, forked));
    EXPECT_NE(forked, count) << "the fork wrote other bytes";
    ASSERT_TRUE(card->cardRegisters(regs));
    EXPECT_EQ(regs.i, forked);
    const uint64_t forkCalls = callbacks->List()[0].stats.calls;
    EXPECT_GT(forkCalls, poll.stats.calls) << "the hook ran in the fork";
    const ttd::TTDTimePoint forkEnd = controller->CurrentPosition();
    controller->StopRecording();
    ASSERT_TRUE(controller->SeekTo(from, nullptr));
    ASSERT_TRUE(controller->SeekTo(forkEnd, nullptr));
    ASSERT_TRUE(card->peekCardMemory(0x6000, replayed));
    EXPECT_EQ(replayed, forked) << "the fork's pokes, replayed";
    ASSERT_TRUE(card->cardRegisters(after));
    EXPECT_EQ(after.i, regs.i);
    EXPECT_EQ(after.pc, regs.pc);
    EXPECT_EQ(after.af, regs.af);
    EXPECT_EQ(callbacks->List()[0].stats.calls, forkCalls) << "the hook does not run on replay";

    lua.stop();
    context->pTimeTravelHooks = nullptr;
    context->ttdWriteSink = nullptr;
    context->pTimeTravelController = nullptr;
    controller.reset();
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The named card callbacks (gs-debugger requirements X13): gs_on_page and gs_on_dac_fetch are taps on the card's
/// events, gs_on_port_in a tap on its 8-bit port decode; a DAC fetch's hit carries its channel in hit.arg
TEST(LuaGSCardHook_Test, NamedCardCallbacksSeeTheCardsEvents)
{
    SoundCardScope sound(TestSound::GeneralSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, GSTypeKind::Z80));
    auto* card = dynamic_cast<SoundChip_GeneralSound*>(context->pSoundManager->getGeneralSound());
    ASSERT_NE(card, nullptr);
    ScriptCallbackManager* callbacks = context->pDebugManager->GetScriptCallbacks();

    BoundLua lua;
    lua.start();
    lua.Bind(emulator);
    std::string message;
    std::string output;
    ASSERT_TRUE(lua.executeCode(R"(
        page = gs_on_page([[return function(hit) shared_set("page", hit.event) end]], {mode = "sync"})
        dac = gs_on_dac_fetch([[return function(hit)
            if hit.arg ~= nil then shared_set("channel", hit.arg) end
            shared_set("dac", hit.event)
        end]], {mode = "sync"})
        status = gs_on_port_in(4, [[return function(hit) shared_set("status", hit.cpu) end]], {mode = "sync"})
        main, why = gs_on_int(function() end, {cpu = "main"})
        print(page, dac, status, main, why)
    )", message, output)) << message;
    EXPECT_EQ(output, "1\t2\t3\t-1\tcpu must be a card's: 'gs' or 'ms'\n");

    for (int i = 0; i < 200 && (callbacks->SharedGet("dac").kind == CallbackValue::Kind::Nil ||
                                callbacks->SharedGet("status").kind == CallbackValue::Kind::Nil);
         i++)
        emulator->RunNFrames(1, false);

    for (const CallbackInfo& info : callbacks->List())
        EXPECT_EQ(info.stats.errors, 0u) << info.target << ": " << info.stats.lastError;
    EXPECT_EQ(callbacks->SharedGet("page").s, "page");
    EXPECT_EQ(callbacks->SharedGet("dac").s, "dac_fetch") << "fetches: " << card->getActivityCounters().dacFetches;
    ASSERT_EQ(callbacks->SharedGet("channel").kind, CallbackValue::Kind::Integer) << "a DAC fetch names its channel";
    EXPECT_LT(callbacks->SharedGet("channel").i, 4);
    EXPECT_EQ(callbacks->SharedGet("status").s, "gs");
    for (const CallbackInfo& info : callbacks->List())
        EXPECT_EQ(info.cpu, DebugCpuId::GS);
    lua.stop();
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// snapshot_load(path, {switch_model}) as every surface: a 128K .z80 on a 48K is refused with the machine named unless
// switch_model = true asks for the switch (a new instance: its id comes back)
TEST_F(AutomationLua_Test, SnapshotLoadSwitchesTheModelOnlyWhenAsked)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto spectrum48 = manager->CreateEmulatorWithModel("lua-switch", "48K", LoggerLevel::LogError);
    ASSERT_TRUE(spectrum48);
    manager->SetSelectedEmulatorId(spectrum48->GetId());
    const std::string path = TestPathHelper::GetTestDataPath("loaders/z80/libspectrum/synth-128.z80");

    std::string message;
    std::string output;
    ASSERT_TRUE(_lua.executeCode("local ok, reason = snapshot_load('" + path + "', {switch_model = false}); print(ok, reason)",
                                 message, output))
        << message;
    EXPECT_EQ(output.rfind("false\t", 0), 0u) << output;
    EXPECT_NE(output.find("128k"), std::string::npos) << "the machine it needs: " << output;

    output.clear();
    ASSERT_TRUE(_lua.executeCode("local ok, reason, id = snapshot_load('" + path + "', {switch_model = true}); print(ok, id)",
                                 message, output))
        << message;
    EXPECT_EQ(output.rfind("true\t", 0), 0u) << output;
    const std::string id = output.substr(5, output.size() - 6);
    ASSERT_NE(id, spectrum48->GetId()) << "a new instance";
    auto switched = manager->GetEmulator(id);
    ASSERT_TRUE(switched) << output;
    EXPECT_EQ(switched->GetContext()->config.mem_model, MM_SPECTRUM128);

    output.clear();
    ASSERT_TRUE(_lua.executeCode("print((snapshot_load('" + path + "', 'legacy')))", message, output)) << message;
    EXPECT_EQ(output, "true\n") << "a string is still the commit (on the 128K now selected)";

    for (const auto& emulatorId : manager->GetEmulatorIds())
        manager->RemoveEmulator(emulatorId);
}
