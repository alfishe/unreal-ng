// The paced run (execution::RunPacer, Stream Deck use case D1): the ladder, a step at a preset, a run on the pacer's
// thread that a breakpoint, a resume or a stop ends, the slider's rules (a paced preset pauses a running machine,
// full speed resumes it), and its end with the instance. Started machines in turbo, paused on NOPs at $8000; each
// test is boot-bound (a started machine): about 20-40 ms

#include "pch.h"

#include <memory>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/runpacer.h"
#include "emulator/memory/memory.h"

using execution::PaceState;
using execution::PaceUnit;
using execution::Requester;
using execution::RunPacer;
using execution::RunReply;

namespace
{
const Requester kGui{execution::Origin::Gui, {}};

size_t Preset(const char* name)
{
    size_t index = 0;
    EXPECT_TRUE(RunPacer::PresetIndex(name, index)) << name;
    return index;
}
}  // namespace

TEST(RunPacerLadder_Test, TheLadderIsTheSpeedControlsSliderCoarseToFine)
{
    const auto& presets = RunPacer::Presets();
    ASSERT_EQ(presets.size(), 9u);
    EXPECT_STREQ(presets.front().id, "full");
    EXPECT_EQ(presets.front().unit, PaceUnit::Full);
    EXPECT_STREQ(presets.back().id, "1op");
    EXPECT_EQ(presets.back().unit, PaceUnit::Instructions);
    EXPECT_EQ(presets[Preset("25fps")].fixedIntervalMs, 40);
    EXPECT_EQ(presets[Preset("1fps")].fixedIntervalMs, 1000);
    EXPECT_EQ(presets[Preset("100lines")].count, 100u);
    EXPECT_EQ(presets[Preset("100lines")].fixedIntervalMs, 0) << "the adjustable interval";

    size_t index = 0;
    EXPECT_TRUE(RunPacer::PresetIndex("5 fps", index)) << "the label";
    EXPECT_EQ(index, 2u);
    EXPECT_TRUE(RunPacer::PresetIndex("8", index)) << "the index";
    EXPECT_EQ(index, 8u);
    EXPECT_FALSE(RunPacer::PresetIndex("9", index));
    EXPECT_FALSE(RunPacer::PresetIndex("2fps", index));
    EXPECT_FALSE(RunPacer::PresetIndex("", index));
}

class RunPacer_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    std::shared_ptr<Emulator> _shared;
    std::shared_ptr<RunPacer> _pacer;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        ASSERT_TRUE(EmulatorTestHelper::EnableDebugFeatures(_emulator));
        _emulator->EnableTurboMode();  // frames only have to advance; nothing here looks at pixels
        _shared = EmulatorManager::GetInstance()->GetEmulator(_emulator->GetId());
        ASSERT_NE(_shared, nullptr);
        _pacer = RunPacer::For(_shared);
        ASSERT_NE(_pacer, nullptr);
        _emulator->StartAsync();
        ASSERT_TRUE(TestWait::For([&]() { return _emulator->GetContext()->emulatorState.frame_counter > 1; }));
    }

    void TearDown() override
    {
        _pacer.reset();
        _shared.reset();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);  // RunPacer::Drop: the pace ends, its thread is joined
    }

    void Pause()
    {
        _emulator->Pause();
        ASSERT_TRUE(TestWait::For([&]() {
            const execution::Snapshot s = _emulator->GetArbiter().GetSnapshot();
            return s.intent == execution::Intent::Pause && s.loopParked;
        }));
    }

    /// NOPs at $8000, the machine there with interrupts off
    void Nops()
    {
        Memory* memory = _emulator->GetMemory();
        for (uint16_t a = 0x8000; a < 0x8100; a++)
            memory->DirectWriteToZ80Memory(a, 0x00);
        Z80State* z80 = _emulator->GetZ80State();
        z80->pc = 0x8000;
        z80->iff1 = z80->iff2 = 0;
    }

    uint16_t Pc() { return _emulator->GetZ80State()->pc; }
};

TEST_F(RunPacer_Test, AStepRunsOneUnitOfThePresetAndFullSpeedHasNone)
{
    Pause();
    Nops();
    RunReply reply = _pacer->Step(kGui);
    EXPECT_EQ(reply.status, RunReply::Status::BadRequest) << "no step at full speed";

    ASSERT_TRUE(_pacer->Set(kGui, Preset("1op"), std::nullopt).Ok());
    reply = _pacer->Step(kGui);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(Pc(), 0x8001);
    EXPECT_EQ(reply.body.find("stop")->find("reason")->s, "step");
    EXPECT_EQ(reply.body.find("pace")->find("preset")->s, "1op");
    EXPECT_FALSE(_pacer->State().running) << "a step is not a run";

    // Ten scanlines: ten lines' t-states (the NOPs run on)
    ASSERT_TRUE(_pacer->Set(kGui, Preset("10lines"), std::nullopt).Ok());
    const uint16_t before = Pc();
    ASSERT_TRUE(_pacer->Step(kGui).Ok());
    const unsigned line = _emulator->GetContext()->config.t_line;
    EXPECT_NEAR(static_cast<double>(Pc() - before), 10.0 * line / 4.0, 2.0) << "a NOP is 4 t-states";
}

TEST_F(RunPacer_Test, ATickIsDeterministicOnTheCallersThread)
{
    Pause();
    Nops();
    ASSERT_TRUE(_pacer->Set(kGui, Preset("1op"), 7).Ok());
    for (int i = 0; i < 3; i++)
        ASSERT_TRUE(_pacer->Tick());
    EXPECT_EQ(Pc(), 0x8003);
    const PaceState state = _pacer->State();
    EXPECT_EQ(state.ticks, 3u);
    EXPECT_EQ(state.intervalMs, 7);
    EXPECT_EQ(state.effectiveMs, 7);
    EXPECT_FALSE(state.running);
}

TEST_F(RunPacer_Test, ARunStepsOnItsOwnUntilStopped)
{
    Pause();
    Nops();
    ASSERT_TRUE(_pacer->Set(kGui, Preset("1op"), RunPacer::kIntervalMin).Ok());
    const RunReply started = _pacer->Start(kGui);
    ASSERT_TRUE(started.Ok()) << started.message;
    EXPECT_TRUE(started.body.find("pace")->find("running")->b);
    ASSERT_TRUE(TestWait::For([&]() { return _pacer->State().ticks >= 3; }));
    ASSERT_TRUE(_pacer->Stop().Ok());
    const PaceState state = _pacer->State();
    EXPECT_FALSE(state.running);
    EXPECT_EQ(state.stopReason, "stopped");
    EXPECT_TRUE(_emulator->IsPaused()) << "the machine stays paused";
    EXPECT_GE(Pc(), 0x8003);
}

TEST_F(RunPacer_Test, ABreakpointEndsTheRunWhereItStopped)
{
    Pause();
    Nops();
    auto* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_EXECUTE;
    breakpoint->z80address = 0x8005;
    ASSERT_NE(_emulator->GetBreakpointManager()->AddBreakpoint(breakpoint), 0);

    ASSERT_TRUE(_pacer->Set(kGui, Preset("1op"), RunPacer::kIntervalMin).Ok());
    ASSERT_TRUE(_pacer->Start(kGui).Ok());
    ASSERT_TRUE(TestWait::For([&]() { return !_pacer->State().running; }));
    const PaceState state = _pacer->State();
    EXPECT_EQ(state.stopReason, "breakpoint");
    EXPECT_EQ(state.breakpointAddress, 0x8005);
    EXPECT_EQ(Pc(), 0x8005) << "stopped before the breakpoint's instruction";
    EXPECT_EQ(_pacer->StateBody().find("breakpoint")->find("address")->i, 0x8005);
}

TEST_F(RunPacer_Test, AResumeOfTheMachineEndsTheRun)
{
    Pause();
    Nops();
    ASSERT_TRUE(_pacer->Set(kGui, Preset("frame"), 20).Ok());
    ASSERT_TRUE(_pacer->Start(kGui).Ok());
    ASSERT_TRUE(TestWait::For([&]() { return _pacer->State().ticks >= 1; }));
    _emulator->Resume(kGui);
    ASSERT_TRUE(TestWait::For([&]() { return !_pacer->State().running; }));
    EXPECT_EQ(_pacer->State().stopReason, "resumed");
}

TEST_F(RunPacer_Test, APacedPresetPausesARunningMachineAndFullSpeedResumesIt)
{
    // Off full speed while the machine runs: it pauses and the pace runs (the slider)
    ASSERT_FALSE(_emulator->IsPaused());
    RunReply reply = _pacer->Set(kGui, Preset("25fps"), std::nullopt);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(_emulator->IsPaused());
    EXPECT_TRUE(_pacer->State().running);
    EXPECT_EQ(_pacer->State().effectiveMs, 40);

    // Between paced presets the run goes on
    ASSERT_TRUE(_pacer->Shift(kGui, 1).Ok());
    EXPECT_EQ(RunPacer::Presets()[_pacer->State().preset].id, std::string("5fps"));
    EXPECT_TRUE(_pacer->State().running);

    // Onto full speed: the pace stops and the machine runs again
    reply = _pacer->Set(kGui, Preset("full"), std::nullopt);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_FALSE(_pacer->State().running);
    ASSERT_TRUE(TestWait::For([&]() { return !_emulator->IsPaused(); }));
}

TEST_F(RunPacer_Test, TheToggleIsTheDialsPushAndTheEdgesAndRangesAreChecked)
{
    // At full speed the push pauses the machine, the next one continues it
    ASSERT_TRUE(_pacer->Toggle(kGui).Ok());
    ASSERT_TRUE(TestWait::For([&]() { return _emulator->IsPaused(); }));
    ASSERT_TRUE(_pacer->Toggle(kGui).Ok());
    ASSERT_TRUE(TestWait::For([&]() { return !_emulator->IsPaused(); }));

    Pause();
    RunReply reply = _pacer->Shift(kGui, -1);
    ASSERT_TRUE(reply.Ok());
    EXPECT_TRUE(reply.body.find("edge")->b) << "full speed is the coarsest";
    reply = _pacer->Shift(kGui, 100);
    ASSERT_TRUE(reply.Ok());
    EXPECT_EQ(_pacer->State().preset, RunPacer::Presets().size() - 1) << "clamped to the finest";
    EXPECT_EQ(_pacer->Set(kGui, std::nullopt, 0).status, RunReply::Status::BadRequest);
    EXPECT_EQ(_pacer->Set(kGui, std::nullopt, 5001).status, RunReply::Status::BadRequest);
    EXPECT_EQ(_pacer->Set(kGui, 99, std::nullopt).status, RunReply::Status::BadRequest);

    // A paused machine at a paced preset: the push starts the pace, the next one stops it
    ASSERT_TRUE(_pacer->Set(kGui, std::nullopt, 50).Ok());
    ASSERT_TRUE(_pacer->Toggle(kGui).Ok());
    EXPECT_TRUE(_pacer->State().running);
    ASSERT_TRUE(_pacer->Toggle(kGui).Ok());
    EXPECT_FALSE(_pacer->State().running);
}

TEST_F(RunPacer_Test, AnotherSurfaceHoldingRunControlRefusesThePace)
{
    Pause();
    const unreal::UUID owner = unreal::UUID::Generate();
    ASSERT_TRUE(_emulator->GetContext()->TakeRunControl(owner, "gdb"));
    ASSERT_TRUE(_pacer->Set(kGui, Preset("1op"), 5).Ok());
    EXPECT_EQ(_pacer->Start(kGui).status, RunReply::Status::Held);
    EXPECT_EQ(_pacer->Step(kGui).status, RunReply::Status::Held);
    const Requester holder{execution::Origin::Gdb, owner.toString()};
    EXPECT_TRUE(_pacer->Step(holder).Ok()) << "the holder steps";
    _emulator->GetContext()->ReleaseRunControl(owner);
}

TEST_F(RunPacer_Test, TheInstanceGoingEndsTheRunAndForgetsThePacer)
{
    Pause();
    Nops();
    ASSERT_TRUE(_pacer->Set(kGui, Preset("1op"), 10).Ok());
    ASSERT_TRUE(_pacer->Start(kGui).Ok());
    const std::string id = _emulator->GetId();
    std::shared_ptr<RunPacer> pacer = _pacer;
    _pacer.reset();
    _shared.reset();
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;
    EXPECT_EQ(RunPacer::Find(id), nullptr);
    EXPECT_FALSE(pacer->State().running);
    EXPECT_EQ(pacer->State().stopReason, "gone");
}
