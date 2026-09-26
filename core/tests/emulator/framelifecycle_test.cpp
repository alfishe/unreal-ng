#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/soundcardscope.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/ianalyzer.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"
#ifdef UNREALNG_HAVE_OPL4
#include "emulator/sound/chips/soundchip_moonsound.h"
#endif

/// Frame lifecycle invariant: a frame boundary is ONE operation - CPU close
/// (Core::FinishCPUFrame), frame-end hooks, frame-start hooks, CPU frame start
/// (MainLoop::CompleteFrame) - executed exactly once per crossed frame by every
/// run path: the continuous main loop and every Emulator::Run*. Before the
/// unification the paths disagreed (the scanline / interrupt / condition
/// steppers skipped the hooks entirely, the step paths executed an opcode in
/// the same iteration that accepted an INT, a pause park could leave the
/// machine on either side of OnFrameStart), so the same program produced
/// different machine trajectories and TTD checkpoints depending on how it was
/// driven.
namespace
{
/// Counts the analyzer frame dispatches MainLoop::OnFrameStart/OnFrameEnd make
class FrameHookCounter : public IAnalyzer
{
public:
    int starts = 0;
    int ends = 0;

    void onActivate(AnalyzerManager*) override {}
    void onDeactivate() override {}
    void onFrameStart() override { starts++; }
    void onFrameEnd() override { ends++; }
    std::string getName() const override { return "frame-hook-counter"; }
    std::string getUUID() const override { return "frame-hook-counter"; }
};

/// Everything program-visible about the machine at a position
struct MachineTrajectoryPoint
{
    ttd::TTDCpuState cpu;
    uint64_t frame = 0;
    uint64_t tStates = 0;
    uint64_t ramHash = 0;
};

MachineTrajectoryPoint Observe(Emulator* emulator)
{
    EmulatorContext* context = emulator->GetContext();
    const Z80* z80 = context->pCore->GetZ80();

    MachineTrajectoryPoint point;
    point.cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(z80));
    point.frame = context->emulatorState.frame_counter;
    point.tStates = context->emulatorState.t_states;
    const size_t ramBytes = static_cast<size_t>(context->config.ramsize) * 1024u;
    point.ramHash = ttd::HashBytes(context->pMemory->RAMBase(), ramBytes);
    return point;
}

void ExpectSameTrajectoryPoint(const MachineTrajectoryPoint& actual, const MachineTrajectoryPoint& expected,
                               const std::string& path)
{
    EXPECT_EQ(actual.frame, expected.frame) << path << ": frame";
    EXPECT_EQ(actual.tStates, expected.tStates) << path << ": t_states";
    EXPECT_EQ(actual.cpu.pc, expected.cpu.pc) << path << ": PC";
    EXPECT_EQ(std::memcmp(&actual.cpu, &expected.cpu, sizeof(actual.cpu)), 0) << path << ": CPU state";
    EXPECT_EQ(actual.ramHash, expected.ramHash) << path << ": RAM";
}

std::vector<uint8_t> DeviceState(ttd::TTDSerializable* device)
{
    std::vector<uint8_t> blob(device->TTDStateSize());
    if (!blob.empty())
        device->TTDSaveState(blob.data());
    return blob;
}
}  // namespace

class FrameLifecycle_Test : public ::testing::Test
{
protected:
    // Keep the General Sound / MoonSound cards the configs fit (the turbo check compares the GS and MoonSound core state):
    // declared first, so it is active before any machine is created
    SoundCardScope _soundCards;

protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    FrameHookCounter* _hooks = nullptr;

    void SetUp() override
    {
        _emulator = CreateMachine();
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();

        AnalyzerManager* analyzers = _context->pDebugManager->GetAnalyzerManager();
        auto counter = std::make_unique<FrameHookCounter>();
        _hooks = counter.get();
        analyzers->registerAnalyzer("frame-hook-counter", std::move(counter));
        analyzers->activate("frame-hook-counter");
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pDebugManager->GetAnalyzerManager()->unregisterAnalyzer("frame-hook-counter");
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    static Emulator* CreateMachine()
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        if (emulator)
        {
            emulator->DebugOn();

            // Power-on RAM is deliberately randomized (Memory::RandomizeMemoryContent,
            // screen pages): instances compared against each other start from the
            // same content, then a reset starts frame 0 from it
            EmulatorContext* context = emulator->GetContext();
            std::memset(context->pMemory->RAMBase(), 0, static_cast<size_t>(context->config.ramsize) * 1024u);
            emulator->Reset();
        }
        return emulator;
    }

    static MainLoop_CUT* MainLoopOf(Emulator* emulator)
    {
        return reinterpret_cast<MainLoop_CUT*>(emulator->GetContext()->pMainLoop);
    }

    uint64_t Frame() const { return _context->emulatorState.frame_counter; }
};

/// Every run path runs the frame-end and frame-start hooks exactly once for
/// every frame it crosses, and never otherwise; t_states (advanced by the frame
/// end) stays locked to the frame counter
TEST_F(FrameLifecycle_Test, EveryRunPathRunsFrameHooksOncePerCrossedFrame)
{
    const CONFIG& config = _context->config;

    // Get past the ROM init so interrupts are enabled (RunUntilInterrupt)
    _emulator->RunNFrames(60);

    struct Path
    {
        std::string name;
        std::function<void(Emulator*)> run;
    };
    const std::vector<Path> paths = {
        {"MainLoop::RunFrame x3", [](Emulator* e) { for (int i = 0; i < 3; i++) MainLoopOf(e)->RunFrame(); }},
        {"RunSingleCPUCycle x40000", [](Emulator* e) { for (int i = 0; i < 40000; i++) e->RunSingleCPUCycle(); }},
        {"RunNCPUCycles(60000)", [](Emulator* e) { e->RunNCPUCycles(60000); }},
        {"RunFrame x3", [](Emulator* e) { for (int i = 0; i < 3; i++) e->RunFrame(); }},
        {"RunNFrames(3)", [](Emulator* e) { e->RunNFrames(3); }},
        {"RunTStates(3 frames)", [&](Emulator* e) { e->RunTStates(config.frame * 3); }},
        {"RunUntilScanline x3", [](Emulator* e) { for (int i = 0; i < 3; i++) e->RunUntilScanline(10); }},
        {"RunNScanlines(900)", [](Emulator* e) { e->RunNScanlines(900); }},
        {"RunUntilNextScreenPixel x3", [](Emulator* e) { for (int i = 0; i < 3; i++) e->RunUntilNextScreenPixel(); }},
        {"RunUntilInterrupt x3", [](Emulator* e) { for (int i = 0; i < 3; i++) e->RunUntilInterrupt(); }},
        {"RunUntilCondition(3 frames)",
         [&](Emulator* e) { e->RunUntilCondition([](const Z80State&) { return false; }, config.frame * 3); }},
    };

    for (const Path& path : paths)
    {
        SCOPED_TRACE(path.name);
        _emulator->ResetLineStepAnchor();

        const uint64_t frameBefore = Frame();
        const uint64_t tStatesBefore = _context->emulatorState.t_states;
        const int startsBefore = _hooks->starts;
        const int endsBefore = _hooks->ends;

        path.run(_emulator);

        const int crossed = static_cast<int>(Frame() - frameBefore);
        EXPECT_GT(crossed, 0) << "the path must cross at least one frame for the check to mean anything";
        EXPECT_EQ(_hooks->ends - endsBefore, crossed) << "OnFrameEnd once per crossed frame";
        EXPECT_EQ(_hooks->starts - startsBefore, crossed) << "OnFrameStart once per crossed frame";
        EXPECT_EQ(_context->emulatorState.t_states - tStatesBefore, static_cast<uint64_t>(crossed) * config.frame)
            << "t_states advances with the frame counter";
        EXPECT_LT(_context->pCore->GetZ80()->t, _context->pCore->GetZ80()->_frameLimit)
            << "a path never leaves the machine on a half-crossed boundary";
    }
}

/// The same program driven to the same frame boundary by different run paths
/// reaches the same machine state. Before the unification the step paths
/// executed an opcode in the same iteration that accepted an INT, so their
/// trajectory drifted from the main loop's from the first interrupt on
TEST_F(FrameLifecycle_Test, AllRunPathsProduceTheSameMachineTrajectory)
{
    constexpr uint64_t kTargetFrame = 30;

    struct Path
    {
        std::string name;
        std::function<void(Emulator*)> runToTarget;
    };
    const std::vector<Path> paths = {
        {"Emulator::RunFrame", [](Emulator* e) { while (e->GetContext()->emulatorState.frame_counter < kTargetFrame) e->RunFrame(); }},
        {"RunNFrames", [](Emulator* e) { e->RunNFrames(static_cast<unsigned>(kTargetFrame)); }},
        {"RunNCPUCycles(1)", [](Emulator* e) { while (e->GetContext()->emulatorState.frame_counter < kTargetFrame) e->RunNCPUCycles(1); }},
        {"RunTStates(1)", [](Emulator* e) { while (e->GetContext()->emulatorState.frame_counter < kTargetFrame) e->RunTStates(1); }},
        {"RunUntilCondition",
         [](Emulator* e)
         {
             const EmulatorState* state = &e->GetContext()->emulatorState;
             e->RunUntilCondition([state](const Z80State&) { return state->frame_counter >= kTargetFrame; });
         }},
    };

    // Reference: the continuous main loop's frame pass
    while (Frame() < kTargetFrame)
        MainLoopOf(_emulator)->RunFrame();
    const MachineTrajectoryPoint reference = Observe(_emulator);
    ASSERT_EQ(reference.frame, kTargetFrame);

    for (const Path& path : paths)
    {
        SCOPED_TRACE(path.name);
        Emulator* other = CreateMachine();
        ASSERT_NE(other, nullptr);

        path.runToTarget(other);
        ExpectSameTrajectoryPoint(Observe(other), reference, path.name);

        EmulatorTestHelper::CleanupEmulator(other);
    }
}

/// A queued host speed change is applied at the frame boundary on every path -
/// never mid-frame by a step command (the step paths used to apply it at the
/// start of each call, so single-stepping changed the clock of the frame in
/// progress)
TEST_F(FrameLifecycle_Test, QueuedMultiplierAppliesAtTheFrameBoundaryOnly)
{
    EmulatorState& state = _context->emulatorState;
    Z80* z80 = _context->pCore->GetZ80();
    const uint32_t baseLimit = z80->_frameLimit;
    ASSERT_EQ(state.current_z80_frequency_multiplier, 1);

    _emulator->RunNCPUCycles(100);
    _context->pCore->SetSpeedMultiplier(2);

    const uint64_t frame = Frame();
    while (Frame() == frame)
    {
        _emulator->RunNCPUCycles(1);
        if (Frame() != frame)
            break;
        ASSERT_EQ(state.current_z80_frequency_multiplier, 1) << "applied mid-frame by a step command";
        ASSERT_EQ(z80->_frameLimit, baseLimit) << "frame geometry changed mid-frame";
    }

    EXPECT_EQ(state.current_z80_frequency_multiplier, 2) << "applied at the boundary";
    EXPECT_EQ(z80->_frameLimit, baseLimit * 2) << "the new frame runs with the new geometry";
}

/// Reset and snapshot load replace the machine state from outside the frame
/// flow: the interrupted frame is abandoned (no frame end) and the frame is
/// started again exactly once from the new state
TEST_F(FrameLifecycle_Test, ResetAndSnapshotLoadRestartTheFrameOnce)
{
    _emulator->RunNCPUCycles(5000);  // mid-frame

    int starts = _hooks->starts;
    int ends = _hooks->ends;
    _emulator->Reset();
    EXPECT_EQ(_hooks->starts - starts, 1) << "reset starts frame 0 once";
    EXPECT_EQ(_hooks->ends - ends, 0) << "the abandoned frame is not closed";
    EXPECT_EQ(Frame(), 0u);

    _emulator->RunNCPUCycles(5000);
    const auto sna = TestPathHelper::FindProjectRoot() / "testdata/loaders/sna/Dizzy Y.sna";
    starts = _hooks->starts;
    ends = _hooks->ends;
    ASSERT_TRUE(_emulator->LoadSnapshot(sna.string())) << sna;
    EXPECT_EQ(_hooks->starts - starts, 1) << "snapshot load starts the frame once";
    EXPECT_EQ(_hooks->ends - ends, 0) << "the abandoned frame is not closed";

    // And the frame flow continues normally from there
    starts = _hooks->starts;
    ends = _hooks->ends;
    _emulator->RunNFrames(2);
    EXPECT_EQ(_hooks->starts - starts, 2);
    EXPECT_EQ(_hooks->ends - ends, 2);
}

/// Turbo mode without audio is a host-audio setting: every device with an
/// emulated core still reaches the end of every frame, so the machine state is
/// the same as at normal speed (the main loop used to skip the whole sound
/// frame end in this mode, leaving the GS coprocessor and the MoonSound time
/// axis dependent on whether turbo was on)
TEST_F(FrameLifecycle_Test, TurboWithoutAudioDoesNotChangeDeviceCoreState)
{
    constexpr int kFrames = 20;

    Emulator* turbo = CreateMachine();
    ASSERT_NE(turbo, nullptr);
    turbo->EnableTurboMode(/*withAudio=*/false);

    for (int i = 0; i < kFrames; i++)
    {
        MainLoopOf(_emulator)->RunFrame();
        MainLoopOf(turbo)->RunFrame();
    }

    ExpectSameTrajectoryPoint(Observe(turbo), Observe(_emulator), "turbo without audio");

    SoundManager* normalSound = _context->pSoundManager;
    SoundManager* turboSound = turbo->GetContext()->pSoundManager;

    ASSERT_TRUE(normalSound->hasGeneralSound() && turboSound->hasGeneralSound())
        << "the Pentagon config fits a General Sound card - the check below would be vacuous";
    EXPECT_EQ(DeviceState(turboSound->getGeneralSound()), DeviceState(normalSound->getGeneralSound()))
        << "GS coprocessor state must not depend on turbo";

#ifdef UNREALNG_HAVE_OPL4
    ASSERT_TRUE(normalSound->hasMoonSound() && turboSound->hasMoonSound())
        << "the Pentagon config fits a MoonSound card - the check below would be vacuous";
    EXPECT_EQ(DeviceState(turboSound->getMoonSound()), DeviceState(normalSound->getMoonSound()))
        << "MoonSound core state and time axis must not depend on turbo";
#endif

    EmulatorTestHelper::CleanupEmulator(turbo);
}
