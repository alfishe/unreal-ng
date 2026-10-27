/// @file scriptcallbacks_test.cpp
/// @brief Script callbacks (conditions-and-callbacks.md §3-§5). Deferred mode: the emulation thread notes a hit in
/// the callback's ring with a snapshot, a full ring drops and counts, the reported call times drive the policy, and a
/// script's callbacks go with it. Frame and sync modes, with a stand-in hook state: a sync call runs inside the
/// access, a frame call at the frame end, a call out of instructions suspends, and unpaced hooks hold the audio.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <set>
#include <utility>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/callbacks/scripthookhost.h"
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"
#include "haptics/hapticmixer.h"
#include "haptics/hapticprofile.h"
#include "haptics/profilehaptics.h"
#include "profiles/json.h"

namespace
{
/// One pass per frame (IM 2 with its own vector): read #9000, write it less one, put it back
const uint8_t kProgram[] = {
    0xF3,              // 8000 DI
    0xED, 0x5E,        // 8001 IM 2
    0x3E, 0x81,        // 8003 LD A,#81
    0xED, 0x47,        // 8005 LD I,A
    0xFB,              // 8007 EI
    0x76,              // 8008 HALT
    0x3A, 0x00, 0x90,  // 8009 LD A,(#9000)
    0x3D,              // 800C DEC A
    0x32, 0x00, 0x90,  // 800D LD (#9000),A
    0x3C,              // 8010 INC A
    0x32, 0x00, 0x90,  // 8011 LD (#9000),A
    0xC3, 0x07, 0x80,  // 8014 JP #8007
};
constexpr uint16_t kByte = 0x9000;
constexpr uint8_t kLives = 5;

/// The hook state's stand-in: records the calls; a call can take time, fail or run out of instructions
class FakeHookHost : public IScriptHookHost
{
public:
    static inline FakeHookHost* last = nullptr;
    std::vector<CallbackHit> hits;
    std::vector<uint64_t> callFrames;  ///< the frame counter at each call
    std::set<uint32_t> compiled;
    std::set<uint32_t> forgotten;
    uint32_t busyUs = 0;
    bool abort = false;
    EmulatorContext* context = nullptr;
    std::vector<std::pair<std::string, uint16_t>> setRegs;  ///< a logic call's registers
    std::vector<bool> logicCalls;

    bool Compile(uint32_t id, const std::string& chunk, std::string& error) override
    {
        if (chunk == "bad")
        {
            error = "syntax error";
            return false;
        }
        compiled.insert(id);
        return true;
    }
    HookCallResult Call(uint32_t /*id*/, const CallbackHit& hit, const std::string& /*event*/, uint64_t budget,
                        HookEdits* edits) override
    {
        hits.push_back(hit);
        logicCalls.push_back(edits != nullptr);
        if (edits != nullptr)
            edits->regs = setRegs;
        callFrames.push_back(context->emulatorState.frame_counter);
        const auto end = std::chrono::steady_clock::now() + std::chrono::microseconds(busyUs);
        while (std::chrono::steady_clock::now() < end)
        {
        }
        HookCallResult result;
        if (abort)
        {
            result.ok = false;
            result.aborted = true;
            result.instructions = budget + 1000;
            result.error = "instruction budget exceeded";
        }
        return result;
    }
    void Forget(uint32_t id) override
    {
        forgotten.insert(id);
    }
};

std::unique_ptr<IScriptHookHost> MakeFakeHookHost(EmulatorContext* context)
{
    auto host = std::make_unique<FakeHookHost>();
    host->context = context;
    FakeHookHost::last = host.get();
    return host;
}

size_t OwnedBreakpoints(BreakpointManager* breakpoints)
{
    size_t count = 0;
    for (const auto& [id, descriptor] : breakpoints->GetAllBreakpoints())
        count += descriptor != nullptr && descriptor->owner.rfind(ScriptCallbackManager::kOwnerPrefix, 0) == 0 ? 1 : 0;
    return count;
}
}  // namespace

class ScriptCallbacks_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ScriptCallbackManager* _callbacks = nullptr;
    BreakpointManager* _breakpoints = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        Memory* memory = _context->pMemory;
        for (size_t i = 0; i < sizeof(kProgram); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), kProgram[i]);
        for (unsigned i = 0; i <= 256; i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8100 + i), 0x83);
        const uint8_t isr[] = {0xFB, 0xED, 0x4D};  // 8383 EI; RETI
        for (size_t i = 0; i < sizeof(isr); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8383 + i), isr[i]);
        memory->DirectWriteToZ80Memory(kByte, kLives);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->sp = 0x7F00;
        _callbacks = _context->pDebugManager->GetScriptCallbacks();
        _factory = GetScriptHookHostFactory();
        _breakpoints = _context->pDebugManager->GetBreakpointsManager();
        ASSERT_NE(_callbacks, nullptr);
    }

    ScriptHookHostFactory _factory = nullptr;

    void TearDown() override
    {
        if (_callbacks)
            _callbacks->RemoveAll();
        SetScriptHookHostFactory(_factory);
        FakeHookHost::last = nullptr;
        if (_context && _context->pControllerManager)
            _context->pControllerManager->SetHapticFeed(nullptr);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint32_t WriteTap(const std::string& condition, CallbackPolicy policy = CallbackPolicy::Suspend, uint32_t ring = 256,
                      CallbackMode mode = CallbackMode::Deferred)
    {
        CallbackSpec spec;
        spec.mode = mode;
        if (mode != CallbackMode::Deferred)
            spec.chunk = "return function(hit) end";
        spec.trigger = CallbackTrigger::Tap;
        spec.tap.type = BRK_MEMORY;
        spec.tap.access = BRK_MEM_WRITE;
        spec.tap.address = kByte;
        spec.tap.condition = condition;
        spec.owner = "test";
        spec.policy = policy;
        spec.ring = ring;
        std::string error;
        const uint32_t id = _callbacks->Register(spec, error);
        EXPECT_NE(id, 0u) << error;
        return id;
    }
};

TEST_F(ScriptCallbacks_Test, TapHitsArriveAsSnapshots)
{
    const uint32_t id = WriteTap("MDT < 5");
    EXPECT_EQ(OwnedBreakpoints(_breakpoints), 1u) << "one hidden tap";
    _emulator->RunNFrames(3, false);
    EXPECT_FALSE(_emulator->IsPaused()) << "a tap callback never stops the machine";

    std::vector<CallbackHit> hits;
    _callbacks->Drain(id, hits);
    ASSERT_GE(hits.size(), 2u) << "one held write a frame";
    for (const CallbackHit& hit : hits)
    {
        EXPECT_EQ(hit.address, kByte);
        EXPECT_EQ(hit.value, kLives - 1) << "the byte the condition held for";
        EXPECT_EQ(hit.pc, 0x8010) << "the snapshot is taken after the write instruction";
        EXPECT_EQ(hit.af >> 8, kLives - 1);
        EXPECT_EQ(hit.kind, BRK_TAP_WRITE);
    }
    EXPECT_LT(hits.front().frame, hits.back().frame);
    CallbackInfo info;
    ASSERT_TRUE(_callbacks->Get(id, info));
    EXPECT_EQ(info.stats.hits, hits.size());
    EXPECT_EQ(info.pending, 0u) << "drained";
    EXPECT_EQ(info.target, "tap write #9000 if MDT < 5");

    std::string error;
    CallbackSpec bad;
    bad.tap.type = BRK_MEMORY;
    bad.tap.access = BRK_MEM_EXECUTE;
    bad.tap.address = 0x8009;
    bad.tap.condition = "RD == 1";
    EXPECT_EQ(_callbacks->Register(bad, error), 0u);
    EXPECT_NE(error.find("RD is not supplied"), std::string::npos) << error;
}

TEST_F(ScriptCallbacks_Test, AFullRingDropsAndNeverWaits)
{
    const uint32_t id = WriteTap("", CallbackPolicy::Suspend, 16);
    _emulator->RunNFrames(12, false);  // two writes a frame, nobody drains
    CallbackInfo info;
    ASSERT_TRUE(_callbacks->Get(id, info));
    EXPECT_EQ(info.ring, 16u);
    EXPECT_EQ(info.pending, 16u);
    EXPECT_EQ(info.stats.hits, 16u);
    EXPECT_GT(info.stats.dropped, 0u);
    std::vector<CallbackHit> hits;
    EXPECT_EQ(_callbacks->Drain(id, hits, 100), 16u) << "the oldest kept";
}

TEST_F(ScriptCallbacks_Test, OverrunsSuspendDisableOrCount)
{
    const uint32_t suspend = WriteTap("");
    const uint32_t ignore = WriteTap("", CallbackPolicy::Ignore);
    const uint32_t disable = WriteTap("", CallbackPolicy::Disable);
    EXPECT_EQ(OwnedBreakpoints(_breakpoints), 3u);

    for (int call = 0; call < 2; call++)
    {
        _callbacks->Report(suspend, 1500, false);
        _callbacks->Report(ignore, 1500, false);
    }
    CallbackInfo info;
    ASSERT_TRUE(_callbacks->Get(suspend, info));
    EXPECT_EQ(info.state, CallbackState::Active) << "two overruns: a stall, not a pattern";
    _callbacks->Report(suspend, 1500, false);
    _callbacks->Report(ignore, 1500, false);
    ASSERT_TRUE(_callbacks->Get(suspend, info));
    EXPECT_EQ(info.state, CallbackState::Suspended);
    EXPECT_NE(info.reason.find("3 calls over 1000 us"), std::string::npos) << info.reason;
    EXPECT_EQ(info.stats.overruns, 3u);
    EXPECT_EQ(info.stats.maxUs, 1500u);
    ASSERT_TRUE(_callbacks->Get(ignore, info));
    EXPECT_EQ(info.state, CallbackState::Active) << "ignore only counts";
    EXPECT_EQ(info.stats.overruns, 3u);

    _callbacks->Report(disable, 20000, false);  // one call over 10x the budget
    ASSERT_TRUE(_callbacks->Get(disable, info));
    EXPECT_EQ(info.state, CallbackState::Disabled);
    EXPECT_EQ(OwnedBreakpoints(_breakpoints), 2u) << "a disabled callback's tap goes";
    EXPECT_FALSE(_callbacks->Resume(disable)) << "disabled: register again";

    _emulator->RunNFrames(2, false);
    ASSERT_TRUE(_callbacks->Get(suspend, info));
    EXPECT_EQ(info.stats.hits, 0u) << "a suspended callback notes nothing";
    ASSERT_TRUE(_callbacks->Resume(suspend));
    _emulator->RunNFrames(2, false);
    ASSERT_TRUE(_callbacks->Get(suspend, info));
    EXPECT_EQ(info.state, CallbackState::Active);
    EXPECT_EQ(info.stats.overruns, 0u) << "resumed with fresh statistics";
    EXPECT_GT(info.stats.hits, 0u);

    _callbacks->Report(ignore, 10, true, "boom");
    ASSERT_TRUE(_callbacks->Get(ignore, info));
    EXPECT_EQ(info.stats.errors, 1u);
    EXPECT_EQ(info.stats.lastError, "boom");

    _callbacks->RemoveOwner("test");
    EXPECT_TRUE(_callbacks->List().empty());
    EXPECT_EQ(OwnedBreakpoints(_breakpoints), 0u);
}

TEST_F(ScriptCallbacks_Test, BreakpointStopsAndGameEvents)
{
    std::string error;
    CallbackSpec spec;
    spec.trigger = CallbackTrigger::Breakpoint;
    spec.breakpointId = 4242;
    EXPECT_EQ(_callbacks->Register(spec, error), 0u);
    EXPECT_EQ(error, "no breakpoint 4242");

    BreakpointSpec stop;
    stop.type = BRK_MEMORY;
    stop.access = BRK_MEM_EXECUTE;
    stop.address = 0x8009;
    const uint16_t breakpoint = _breakpoints->AddBreakpoint(stop, error);
    ASSERT_NE(breakpoint, BRK_INVALID) << error;
    spec.breakpointId = breakpoint;
    const uint32_t onStop = _callbacks->Register(spec, error);
    ASSERT_NE(onStop, 0u) << error;
    EXPECT_TRUE(_callbacks->WantsBreakpointStops());
    _callbacks->OnBreakpointStop(breakpoint, 0x8009, 0);
    std::vector<CallbackHit> hits;
    ASSERT_EQ(_callbacks->Drain(onStop, hits), 1u);
    EXPECT_EQ(hits[0].kind, CallbackHit::kKindStop);
    EXPECT_EQ(hits[0].breakpointId, breakpoint);
    _breakpoints->RemoveBreakpointByID(breakpoint);

    controllers::HapticMixer mixer([](const controllers::HostDeviceId&, const controllers::RumbleLevels&) { return true; },
                                   false);
    controllers::JsonValue section;
    ASSERT_TRUE(controllers::ParseJson(R"({"sources": {"five": {"probe": "read", "address": "0x9000", "if": "MDT == 5"}}})",
                                       section, error))
        << error;
    controllers::HapticProfile profile;
    ASSERT_TRUE(controllers::ParseHapticProfile(section, profile, error)) << error;
    _context->pControllerManager->SetHapticFeed(std::make_shared<controllers::ProfileHaptics>(
        profile, "game/test", mixer, [] { return std::vector<controllers::HostDeviceId>{}; }));
    CallbackSpec event;
    event.trigger = CallbackTrigger::GameEvent;
    event.event = "five";
    const uint32_t onEvent = _callbacks->Register(event, error);
    ASSERT_NE(onEvent, 0u) << error;
    _emulator->RunNFrames(4, false);
    hits.clear();
    _callbacks->Drain(onEvent, hits);
    ASSERT_GE(hits.size(), 2u) << "the event fires every frame";
    EXPECT_EQ(hits[0].kind, CallbackHit::kKindEvent);
}

TEST_F(ScriptCallbacks_Test, HooksNeedTheHookStateAndCompile)
{
    SetScriptHookHostFactory(nullptr);
    CallbackSpec spec;
    spec.trigger = CallbackTrigger::GameEvent;
    spec.event = "died";
    spec.mode = CallbackMode::Sync;
    spec.chunk = "return function(hit) end";
    std::string error;
    EXPECT_EQ(_callbacks->Register(spec, error), 0u);
    EXPECT_NE(error.find("Lua hook state"), std::string::npos) << error;

    SetScriptHookHostFactory(&MakeFakeHookHost);
    spec.chunk = "bad";
    EXPECT_EQ(_callbacks->Register(spec, error), 0u);
    EXPECT_EQ(error, "hook: syntax error");
    spec.chunk.clear();
    EXPECT_EQ(_callbacks->Register(spec, error), 0u) << "a hook is source";
    spec.chunk = "return function(hit) end";
    spec.mode = CallbackMode::Deferred;
    spec.policy = CallbackPolicy::Unpaced;
    EXPECT_EQ(_callbacks->Register(spec, error), 0u);
    EXPECT_NE(error.find("unpaced"), std::string::npos) << error;

    spec.mode = CallbackMode::Frame;
    const uint32_t id = _callbacks->Register(spec, error);
    ASSERT_NE(id, 0u) << error;
    CallbackInfo info;
    ASSERT_TRUE(_callbacks->Get(id, info));
    EXPECT_EQ(info.mode, CallbackMode::Frame);
    EXPECT_EQ(info.instructions, ScriptCallbackManager::kDefaultInstructions);
    EXPECT_EQ(FakeHookHost::last->compiled.count(id), 1u);
    EXPECT_TRUE(_callbacks->WantsFrameEnd());
    EXPECT_TRUE(_callbacks->Remove(id));
    EXPECT_EQ(FakeHookHost::last->forgotten.count(id), 1u) << "its function goes with it";
    EXPECT_FALSE(_callbacks->WantsFrameEnd());

    const uint32_t deferred = WriteTap("");
    EXPECT_FALSE(_callbacks->SetPolicy(deferred, CallbackPolicy::Unpaced)) << "a deferred callback cannot slow the machine";
}

TEST_F(ScriptCallbacks_Test, ASyncHookRunsInsideTheAccess)
{
    SetScriptHookHostFactory(&MakeFakeHookHost);
    const uint32_t id = WriteTap("MDT < 5", CallbackPolicy::Suspend, 256, CallbackMode::Sync);
    _emulator->RunNFrames(3, false);
    FakeHookHost& host = *FakeHookHost::last;
    ASSERT_GE(host.hits.size(), 2u) << "one held write a frame";
    for (const CallbackHit& hit : host.hits)
    {
        EXPECT_EQ(hit.value, kLives - 1);
        EXPECT_EQ(hit.pc, 0x8010) << "called right after the write instruction";
    }
    CallbackInfo info;
    ASSERT_TRUE(_callbacks->Get(id, info));
    EXPECT_EQ(info.stats.calls, host.hits.size());
    EXPECT_EQ(info.stats.hits, host.hits.size());
    EXPECT_EQ(info.pending, 0u) << "no ring on the way";
}

/// A logic callback's writes land at the next instruction boundary: PC set at the DEC's write skips the INC and its
/// write, so the byte goes down a life a frame
TEST_F(ScriptCallbacks_Test, ALogicHookWritesAtTheNextBoundary)
{
    SetScriptHookHostFactory(&MakeFakeHookHost);
    CallbackSpec spec;
    spec.trigger = CallbackTrigger::Tap;
    spec.tap.type = BRK_MEMORY;
    spec.tap.access = BRK_MEM_WRITE;
    spec.tap.address = kByte;
    spec.tap.condition = "MDT < 5";
    spec.owner = "test";
    spec.mode = CallbackMode::Sync;
    spec.chunk = "return function(hit) set_reg('pc', 0x8014) end";
    spec.role = CallbackRole::Logic;
    std::string error;
    const uint32_t id = _callbacks->Register(spec, error);
    ASSERT_NE(id, 0u) << error;
    FakeHookHost& host = *FakeHookHost::last;
    host.setRegs = {{"pc", 0x8014}};
    _emulator->RunNFrames(3, false);
    ASSERT_GE(host.hits.size(), 2u);
    EXPECT_TRUE(host.logicCalls[0]) << "the call gets the edits to fill";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(kByte), kLives - host.hits.size());
    EXPECT_FALSE(_context->HasStepWork(EmulatorContext::kStepWorkScriptEdits)) << "applied and cleared";
    CallbackInfo info;
    ASSERT_TRUE(_callbacks->Get(id, info));
    EXPECT_EQ(info.role, CallbackRole::Logic);

    spec.mode = CallbackMode::Deferred;
    spec.chunk.clear();
    EXPECT_EQ(_callbacks->Register(spec, error), 0u);
    EXPECT_NE(error.find("logic role"), std::string::npos) << error;
}

TEST_F(ScriptCallbacks_Test, AFrameHookRunsAtTheFrameEnd)
{
    SetScriptHookHostFactory(&MakeFakeHookHost);
    const uint32_t id = WriteTap("MDT < 5", CallbackPolicy::Suspend, 256, CallbackMode::Frame);
    _emulator->RunNFrames(3, false);
    FakeHookHost& host = *FakeHookHost::last;
    ASSERT_GE(host.hits.size(), 2u);
    for (size_t i = 0; i < host.hits.size(); i++)
        EXPECT_EQ(host.callFrames[i], host.hits[i].frame + 1) << "at the end of the hit's frame (the counter moved on)";
    CallbackInfo info;
    ASSERT_TRUE(_callbacks->Get(id, info));
    EXPECT_EQ(info.stats.calls, host.hits.size());
    EXPECT_EQ(info.pending, 0u);
}

TEST_F(ScriptCallbacks_Test, AHookOutOfInstructionsIsSuspended)
{
    SetScriptHookHostFactory(&MakeFakeHookHost);
    const uint32_t id = WriteTap("", CallbackPolicy::Suspend, 256, CallbackMode::Sync);
    FakeHookHost::last->abort = true;
    _emulator->RunNFrames(3, false);
    CallbackInfo info;
    ASSERT_TRUE(_callbacks->Get(id, info));
    EXPECT_EQ(info.stats.calls, 1u) << "suspended at the first runaway call";
    EXPECT_EQ(info.stats.aborted, 1u);
    EXPECT_EQ(info.stats.errors, 1u);
    EXPECT_EQ(info.state, CallbackState::Suspended);
    EXPECT_NE(info.reason.find("100000 instructions"), std::string::npos) << info.reason;

    // Disabled from the emulation thread: the tap stays armed and silent until the callback is removed
    ASSERT_TRUE(_callbacks->Resume(id));
    ASSERT_TRUE(_callbacks->SetPolicy(id, CallbackPolicy::Disable));
    _emulator->RunNFrames(2, false);
    ASSERT_TRUE(_callbacks->Get(id, info));
    EXPECT_EQ(info.state, CallbackState::Disabled);
    EXPECT_EQ(info.stats.calls, 1u);
    EXPECT_EQ(OwnedBreakpoints(_breakpoints), 1u);
    EXPECT_TRUE(_callbacks->Remove(id));
    EXPECT_EQ(OwnedBreakpoints(_breakpoints), 0u);
}

TEST_F(ScriptCallbacks_Test, UnpacedHooksHoldTheAudioWhileTheyKeepFramesLong)
{
    SetScriptHookHostFactory(&MakeFakeHookHost);
    WriteTap("", CallbackPolicy::Unpaced, 256, CallbackMode::Sync);
    FakeHookHost& host = *FakeHookHost::last;
    SoundManager* sound = _context->pSoundManager;
    constexpr uint32_t kPeriod = 400;
    uint64_t now = 1'000'000;
    BreakpointTapHit tap;
    tap.cookie = 0;  // the first slot
    tap.address = kByte;
    tap.kind = BRK_TAP_WRITE;
    auto frame = [&](uint32_t busyUs, uint64_t wallUs) {
        host.busyUs = busyUs;
        _callbacks->OnTap(tap);
        now += wallUs;
        _callbacks->OnFrameEnd(now, kPeriod);
    };

    frame(20, kPeriod);
    for (unsigned i = 0; i + 1 < ScriptCallbackManager::kUnpacedSlowFrames; i++)
        frame(20, kPeriod * 2);
    EXPECT_FALSE(_callbacks->IsUnpaced()) << "one frame short";
    frame(20, kPeriod);
    frame(20, kPeriod * 2);
    EXPECT_FALSE(_callbacks->IsUnpaced()) << "the slow frames run in a row";
    for (unsigned i = 0; i < ScriptCallbackManager::kUnpacedSlowFrames; i++)
        frame(20, kPeriod * 2);
    ASSERT_TRUE(_callbacks->IsUnpaced());
    EXPECT_EQ(sound->hostOutputHolds(SoundManager::HostHoldReason::Unpaced), 1);
    EXPECT_EQ(_callbacks->HookStatus().unpacedSpans, 1u);

    // The hooks still heavy (over a quarter of the period): it stays
    for (unsigned i = 0; i < 10; i++)
        frame(kPeriod / 2, kPeriod / 2);
    EXPECT_TRUE(_callbacks->IsUnpaced());
    EXPECT_GT(_callbacks->HookStatus().framesOverShare, 0u);
    for (unsigned i = 0; i < ScriptCallbackManager::kUnpacedCalmFrames; i++)
        frame(0, kPeriod / 10);
    EXPECT_FALSE(_callbacks->IsUnpaced()) << "calm frames give the pace back";
    EXPECT_EQ(sound->hostOutputHolds(SoundManager::HostHoldReason::Unpaced), 0);
}
