/// @file scriptcallbacks_test.cpp
/// @brief Script callbacks, deferred mode (conditions-and-callbacks.md §3-§5): the emulation thread notes a hit in the
/// callback's ring with a snapshot, a full ring drops and counts, the reported call times drive the policy, and a
/// script's callbacks go with it.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/memory/memory.h"
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
        _breakpoints = _context->pDebugManager->GetBreakpointsManager();
        ASSERT_NE(_callbacks, nullptr);
    }

    void TearDown() override
    {
        if (_callbacks)
            _callbacks->RemoveAll();
        if (_context && _context->pControllerManager)
            _context->pControllerManager->SetHapticFeed(nullptr);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint32_t WriteTap(const std::string& condition, CallbackPolicy policy = CallbackPolicy::Suspend, uint32_t ring = 256)
    {
        CallbackSpec spec;
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
