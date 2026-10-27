// Machine fuzz of the execution arbiter (docs/inprogress/2026-10-09-execution-arbiter/design.md §8.2): several threads
// drive one running Pentagon at once through the public control surface - pauses, resumes, silent pairs, direct runs
// (frames, T-states, single instructions), commands - in a seeded mix. Checked on every run: never two threads
// executing the Z80 (I1), every RunNFrames(k) advances exactly k frames (no main-loop frame slipped in), no hang (I9),
// and the machine ends parked when paused. UNREAL_EXEC_FUZZ_SEED replays one seed, UNREAL_EXEC_FUZZ_ITERATIONS runs
// more (default 3: each seed boots a machine, about 100 ms).

#include "_helpers/testtier.h"
#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <cstdlib>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulatormanager.h"
#include "emulator/rzx/rzxsession.h"
#include "emulator/zxpoly/zxpolygroup.h"

using namespace std::chrono_literals;

namespace
{

uint64_t Frame(Emulator* emulator)
{
    return emulator->GetContext()->emulatorState.frame_counter;
}

bool DeviceStateProbe(Emulator* emulator)
{
    return emulator->IsPaused() || emulator->IsEmulationParked() || emulator->GetArbiter().GetSnapshot().edges > 0;
}

uint32_t EnvU32(const char* name, uint32_t fallback)
{
    const char* v = std::getenv(name);
    return v && *v ? static_cast<uint32_t>(std::strtoul(v, nullptr, 10)) : fallback;
}

std::vector<std::string> RunMachineScenario(uint32_t seed)
{
    std::vector<std::string> problems;
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    if (!emulator)
        return {"emulator creation failed"};
    emulator->EnableTurboMode();
    emulator->StartAsync();
    TestWait::For([&]() { return Frame(emulator) > 1; });

    std::mt19937 setup(seed);
    const int threads = 2 + static_cast<int>(setup() % 4);
    const int ops = 15 + static_cast<int>(setup() % 25);
    auto snapshotPath = [seed](int thread) {
        return TestPathHelper::GetUniqueTestScratchPath("exec-fuzz-" + std::to_string(seed) + "-" +
                                                        std::to_string(thread) + ".szx");
    };
    std::atomic<int> frameErrors{0};
    std::atomic<int> finished{0};
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
    {
        workers.emplace_back([&, t]() {
            std::mt19937 rng(seed * 7919u + static_cast<uint32_t>(t));
            const execution::Requester who{t % 2 ? execution::Origin::WebApi : execution::Origin::Gui, {}};
            for (int op = 0; op < ops; ++op)
            {
                switch (rng() % 14)
                {
                    case 0: emulator->Pause(); break;
                    case 1: emulator->Resume(); break;
                    case 2:
                        // A hold around a short piece of work (the TTD / media brackets)
                    {
                        Emulator::CpuHold hold(*emulator, "fuzz.hold");
                        for (int i = static_cast<int>(rng() % 20); i > 0; --i)
                            std::this_thread::yield();
                    }
                        break;
                    case 3:
                    {
                        // Counted inside one hold of the CPU: only this run may advance the frames meanwhile
                        execution::CommandOptions driving;
                        driving.drivesZ80 = true;
                        execution::HoldToken hold = emulator->GetArbiter().BeginHold(
                            who, "fuzz.count", execution::Clock::now() + std::chrono::seconds(5), nullptr, driving);
                        const unsigned frames = 1 + rng() % 3;
                        const uint64_t before = Frame(emulator);
                        emulator->RunNFrames(frames);
                        if (hold.Valid() && Frame(emulator) - before != frames)
                            frameErrors.fetch_add(1);
                        emulator->GetArbiter().EndHold(hold);
                        break;
                    }
                    case 4: emulator->RunNFrames(1 + rng() % 3); break;
                    case 5: emulator->RunTStates(1 + rng() % 20000); break;
                    case 6: emulator->RunSingleCPUCycle(true); break;
                    case 7:
                        emulator->GetArbiter().Execute(who, "poke", [&](execution::CommandContext&) {
                            emulator->GetContext()->pMemory->DirectWriteToZ80Memory(0x5800, static_cast<uint8_t>(op));
                        });
                        break;
                    case 8:
                        (void)emulator->IsPaused();
                        (void)emulator->IsEmulationParked();
                        (void)emulator->GetArbiter().GetSnapshot();
                        break;
                    // X3: these hold the CPU (Emulator::CpuHold) instead of a Pause() / Resume() bracket
                    case 10: emulator->Reset(); break;
                    case 11: emulator->SetOverscanMode(rng() % 2 == 0); break;
                    case 12: emulator->RequestNMI(); break;
                    case 13:
                        if (emulator->SaveSnapshot(snapshotPath(t)))
                            emulator->LoadSnapshot(snapshotPath(t));
                        break;
                    default: std::this_thread::yield(); break;
                }
            }
            finished.fetch_add(1);
        });
    }

    if (!TestWait::For([&]() { return finished.load() == threads; }, 15000ms))
    {
        const auto snapshot = emulator->GetArbiter().GetSnapshot();
        std::ostringstream o;
        o << "hang: intent " << execution::IntentName(snapshot.intent) << ", serving "
          << (snapshot.serving ? snapshot.serving->tag : std::string("none")) << ", queue " << snapshot.queue.size()
          << ", loop parked " << snapshot.loopParked;
        std::fprintf(stderr, "ExecutionArbiterMachineFuzz seed %u: %s - unrecoverable, exiting\n", seed,
                     o.str().c_str());
        std::fflush(stderr);
        std::_Exit(1);
    }
    for (auto& w : workers)
        w.join();

    if (frameErrors.load() != 0)
        problems.push_back(std::to_string(frameErrors.load()) + " RunNFrames calls advanced a different frame count");
    if (emulator->DriverViolations() != 0)
        problems.push_back(std::to_string(emulator->DriverViolations()) + " moments with two Z80 drivers");

    // Quiescent: a pause parks it, a resume runs it
    emulator->Pause();
    if (!emulator->IsPaused() || !emulator->WaitForPauseConfirmation(1000))
        problems.push_back("a pause after the run did not park the machine");
    const uint64_t frame = Frame(emulator);
    emulator->Resume();
    if (!TestWait::For([&]() { return Frame(emulator) > frame; }))
        problems.push_back("a resume after the run did not run the machine");
    EmulatorTestHelper::CleanupEmulator(emulator);
    return problems;
}

}  // namespace

// Boot-bound: each seed starts a Pentagon (turbo), about 100 ms; the default set is 3 seeds
TEST(ExecutionArbiterMachineFuzz_Functional, ConcurrentControlKeepsOneDriverAndExactSteps)
{
    SKIP_UNLESS_FUNCTIONAL();
    const char* one = std::getenv("UNREAL_EXEC_FUZZ_SEED");
    const uint32_t first = one ? EnvU32("UNREAL_EXEC_FUZZ_SEED", 0) : 1;
    const uint32_t count = one ? 1 : std::min<uint32_t>(EnvU32("UNREAL_EXEC_FUZZ_ITERATIONS", 3), 200);
    for (uint32_t seed = first; seed < first + count; ++seed)
    {
        const std::vector<std::string> problems = RunMachineScenario(seed);
        if (!problems.empty())
        {
            std::ostringstream o;
            for (const std::string& p : problems)
                o << "\n  " << p;
            ADD_FAILURE() << "seed " << seed << " (UNREAL_EXEC_FUZZ_SEED=" << seed << ")" << o.str();
        }
    }
}

namespace
{

/// The machine's RAM and CPU registers, hashed (FNV-1a)
uint64_t MachineHash(Emulator* emulator)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const uint8_t* data, size_t size) {
        for (size_t i = 0; i < size; ++i)
            h = (h ^ data[i]) * 1099511628211ull;
    };
    Memory* memory = emulator->GetContext()->pMemory;
    for (uint16_t page = 0; page < 8; ++page)
        mix(memory->RAMPageAddress(page), 0x4000);
    const Z80State* z = emulator->GetZ80State();
    const uint16_t regs[] = {z->pc, z->sp, z->af, z->bc, z->de, z->hl, z->ix, z->iy, z->memptr};
    mix(reinterpret_cast<const uint8_t*>(regs), sizeof(regs));
    const uint32_t t = z->t;
    mix(reinterpret_cast<const uint8_t*>(&t), sizeof(t));
    return h;
}

}  // namespace

/// Determinism (design §8.2): the same 40 frames from the same snapshot, once quiet and once while other threads keep
/// submitting commands that change the machine and put it back (FRAMES flipped, then restored - net nothing, unless one
/// is let into the step, where the ROM's interrupt increments the flipped value). The two end states must be equal.
/// Boot-bound: one Pentagon boot, about 80 ms
TEST(ExecutionArbiterMachineFuzz_Functional, CommandsFromOtherThreadsNeverReachIntoAStep)
{
    SKIP_UNLESS_FUNCTIONAL();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    emulator->EnableTurboMode();
    emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&]() { return Frame(emulator) > 60; }));  // into the 128K menu's idle loop
    emulator->Pause();
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("exec-determinism.szx");
    ASSERT_TRUE(emulator->SaveSnapshot(path));

    auto runFrom = [&](int noiseThreads) {
        EXPECT_TRUE(emulator->LoadSnapshot(path));
        std::atomic<bool> stop{false};
        std::atomic<unsigned> commands{0};
        std::vector<std::thread> noise;
        // Each noise thread is bounded by both sides: it ends when the run is over, and after kNoiseCommands of its own.
        // A thread that only stopped with the run could starve it: under a 6-shard load on MinGW three noise threads kept
        // the arbiter busy for good and RunNFrames never got in (a livelock, 25 minutes of CPU)
        constexpr unsigned kNoiseCommands = 400;
        for (int i = 0; i < noiseThreads; ++i)
            noise.emplace_back([&, i]() {
                const execution::Requester who{i % 2 ? execution::Origin::Gui : execution::Origin::Ttd, {}};
                for (unsigned n = 0; n < kNoiseCommands && !stop.load(); ++n)
                {
                    emulator->GetArbiter().Execute(who, "noise", [&](execution::CommandContext&) {
                        Memory* memory = emulator->GetContext()->pMemory;
                        const uint8_t frames = memory->DirectReadFromZ80Memory(23672);
                        memory->DirectWriteToZ80Memory(23672, static_cast<uint8_t>(frames ^ 0xFF));
                        std::this_thread::yield();
                        memory->DirectWriteToZ80Memory(23672, frames);
                        commands++;
                    });
                    {
                        Emulator::CpuHold hold(*emulator, "noise.hold");  // a hold as well
                    }
                }
            });
        // The noise is running before the frames start (a test that nothing disturbed proves nothing)
        if (noiseThreads > 0)
            EXPECT_TRUE(TestWait::For([&] { return commands.load() > 0; }));
        emulator->RunNFrames(40);
        stop = true;
        for (auto& n : noise)
            n.join();
        // The noise was there: the commands ran (a test that nothing disturbed proves nothing)
        if (noiseThreads > 0)
            EXPECT_GT(commands.load(), 0u) << "no noise command ran: the check would be vacuous";
        return MachineHash(emulator);
    };

    const uint64_t quiet = runFrom(0);
    EXPECT_EQ(runFrom(0), quiet) << "the run itself is not deterministic: the check below would mean nothing";
    EXPECT_EQ(runFrom(3), quiet) << "a command from another thread changed the machine during the step";
    EXPECT_EQ(emulator->DriverViolations(), 0u);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// The mode matrix (design §8.3), TTD: the same mix while one thread records - start, write-journal switch on / off,
/// stop, in a seeded order. These are the paths the bug ran through (the GUI's journal switch on adoption) and that X3
/// turned into CPU holds. Checked: no second driver, every counted RunNFrames(k) exact, no hang.
/// Boot-bound: each seed starts a Pentagon (turbo) with TTD on, about 100 ms; 3 seeds by default
TEST(ExecutionArbiterMachineFuzz_Functional, TtdRecordingUnderConcurrentControl)
{
    SKIP_UNLESS_FUNCTIONAL();
    const char* one = std::getenv("UNREAL_EXEC_FUZZ_SEED");
    const uint32_t first = one ? EnvU32("UNREAL_EXEC_FUZZ_SEED", 0) : 1;
    const uint32_t count = one ? 1 : std::min<uint32_t>(EnvU32("UNREAL_EXEC_FUZZ_ITERATIONS", 3), 200);
    for (uint32_t seed = first; seed < first + count; ++seed)
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr);
        emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        emulator->GetContext()->pMemory->UpdateFeatureCache();
        ttd::TimeTravelController* ttdc = emulator->GetContext()->pTimeTravelController;
        if (!ttdc)
        {
            EmulatorTestHelper::CleanupEmulator(emulator);
            GTEST_SKIP() << "the TTD engine is not the backend (UNREAL_TTD_BACKEND=v1)";
        }
        emulator->EnableTurboMode();
        emulator->StartAsync();
        TestWait::For([&]() { return Frame(emulator) > 1; });

        std::atomic<int> frameErrors{0};
        std::atomic<int> finished{0};
        const int threads = 3;
        std::vector<std::thread> workers;
        for (int t = 0; t < threads; ++t)
            workers.emplace_back([&, t]() {
                std::mt19937 rng(seed * 104729u + static_cast<uint32_t>(t));
                const execution::Requester who{execution::Origin::WebApi, {}};
                for (int op = 0; op < 25; ++op)
                {
                    if (t == 0)
                    {
                        // The recorder
                        switch (rng() % 4)
                        {
                            case 0: ttdc->StartRecording(); break;
                            case 1: ttdc->SwitchWriteJournal(true); break;
                            case 2: ttdc->SwitchWriteJournal(false); break;
                            default: ttdc->StopRecording(); break;
                        }
                        continue;
                    }
                    switch (rng() % 6)
                    {
                        case 0: emulator->Pause(who); break;
                        case 1: emulator->Resume(who); break;
                        case 2:
                        {
                            execution::CommandOptions driving;
                            driving.drivesZ80 = true;
                            execution::HoldToken hold = emulator->GetArbiter().BeginHold(
                                who, "fuzz.count", execution::Clock::now() + std::chrono::seconds(5), nullptr, driving);
                            const unsigned frames = 1 + rng() % 2;
                            const uint64_t before = Frame(emulator);
                            emulator->RunNFrames(frames);
                            if (hold.Valid() && Frame(emulator) - before != frames)
                                frameErrors.fetch_add(1);
                            emulator->GetArbiter().EndHold(hold);
                            break;
                        }
                        case 3:
                        {
                            Emulator::CpuHold hold(*emulator, "fuzz.hold");
                            std::this_thread::yield();
                            break;
                        }
                        case 4: emulator->RunTStates(1 + rng() % 20000); break;
                        default: (void)DeviceStateProbe(emulator); break;
                    }
                }
                finished.fetch_add(1);
            });
        if (!TestWait::For([&]() { return finished.load() == threads; }, 15000ms))
        {
            std::fprintf(stderr, "ExecutionArbiterMachineFuzz TTD seed %u: hang - unrecoverable, exiting\n", seed);
            std::fflush(stderr);
            std::_Exit(1);
        }
        for (auto& w : workers)
            w.join();
        ttdc->StopRecording();
        EXPECT_EQ(frameErrors.load(), 0) << "seed " << seed;
        EXPECT_EQ(emulator->DriverViolations(), 0u) << "seed " << seed;
        emulator->Pause();
        EXPECT_TRUE(emulator->WaitForPauseConfirmation(1000)) << "seed " << seed;
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

namespace
{
/// The control threads of the ZX-Poly / RZX matrices: pauses, resumes, holds, direct runs on the machine
void ControlOp(Emulator* emulator, std::mt19937& rng, const execution::Requester& who, bool exactFrames,
               std::atomic<int>& frameErrors)
{
    switch (rng() % 6)
    {
        case 0: emulator->Pause(who); break;
        case 1: emulator->Resume(who); break;
        case 2:
        {
            execution::CommandOptions driving;
            driving.drivesZ80 = true;
            execution::HoldToken hold = emulator->GetArbiter().BeginHold(
                who, "fuzz.count", execution::Clock::now() + std::chrono::seconds(5), nullptr, driving);
            const unsigned frames = 1 + rng() % 2;
            const uint64_t before = Frame(emulator);
            emulator->RunNFrames(frames);
            if (exactFrames && hold.Valid() && Frame(emulator) - before != frames)
            {
                frameErrors.fetch_add(1);
                std::fprintf(stderr, "frame error: before %llu after %llu, ran %u, paused %d\n",
                             static_cast<unsigned long long>(before), static_cast<unsigned long long>(Frame(emulator)),
                             frames, emulator->IsPaused() ? 1 : 0);
            }
            emulator->GetArbiter().EndHold(hold);
            break;
        }
        case 3:
        {
            Emulator::CpuHold hold(*emulator, "fuzz.hold");
            std::this_thread::yield();
            break;
        }
        case 4: emulator->RunTStates(1 + rng() % 20000); break;
        default: (void)DeviceStateProbe(emulator); break;
    }
}

uint32_t SeedCount()
{
    return std::getenv("UNREAL_EXEC_FUZZ_SEED") ? 1 : std::min<uint32_t>(EnvU32("UNREAL_EXEC_FUZZ_ITERATIONS", 3), 200);
}

uint32_t FirstSeed()
{
    return std::getenv("UNREAL_EXEC_FUZZ_SEED") ? EnvU32("UNREAL_EXEC_FUZZ_SEED", 0) : 1;
}
}  // namespace

/// ZX-Poly in the matrix (design §8.3): a running group (the master's loop runs the three slaves) driven at once by
/// one thread through the group (recording, its stop / seek / resume, keys, parallel slaves) and two through the
/// master's control (pauses, resumes, holds, direct runs counted exactly). Never two drivers on any module, every
/// counted run exact, no hang
TEST(ExecutionArbiterMachineFuzz_Functional, ZXPolyGroupUnderConcurrentControl)
{
    SKIP_UNLESS_FUNCTIONAL();
    EmulatorManager* manager = EmulatorManager::GetInstance();
    const uint32_t first = FirstSeed();
    for (uint32_t seed = first; seed < first + SeedCount(); ++seed)
    {
        std::string error;
        std::shared_ptr<Emulator> master = manager->CreateZXPolyMachine(
            "zxpoly-fuzz-" + std::to_string(seed), "PENTAGON",
            TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/Alien8.zxp"), &error);
        ASSERT_TRUE(master) << error;
        ZXPolyGroup* group = manager->GetZXPolyGroup(master->GetId());
        ASSERT_NE(group, nullptr);
        for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
            group->GetInstance(m)->EnableTurboMode();
        if (!master->IsRunning())
            master->StartAsync();
        TestWait::For([&]() { return Frame(master.get()) > 1; });

        std::atomic<int> frameErrors{0};
        std::atomic<int> finished{0};
        const int threads = 3;
        std::vector<std::thread> workers;
        for (int t = 0; t < threads; ++t)
            workers.emplace_back([&, t]() {
                std::mt19937 rng(seed * 15485863u + static_cast<uint32_t>(t));
                const execution::Requester who{t == 1 ? execution::Origin::WebApi : execution::Origin::Gui, {}};
                for (int op = 0; op < 25; ++op)
                {
                    if (t == 0)
                    {
                        // The group's own operations
                        switch (rng() % 7)
                        {
                            case 0: group->StartRecording(); break;
                            case 1: group->StopRecording(); break;
                            case 2:
                            {
                                const uint64_t now = Frame(master.get());
                                group->SeekToFrame(now > 4 ? now - 1 - rng() % 4 : now);
                                break;
                            }
                            case 3: group->ResumeRecording(); break;
                            case 4:
                                group->PressKey(ZXKEY_SPACE);
                                group->ReleaseKey(ZXKEY_SPACE);
                                break;
                            case 5: group->SetParallelSlaves(rng() % 2 == 0); break;
                            default: (void)group->GetStatus(); break;
                        }
                        continue;
                    }
                    ControlOp(master.get(), rng, who, true, frameErrors);
                }
                finished.fetch_add(1);
            });
        if (!TestWait::For([&]() { return finished.load() == threads; }, 20000ms))
        {
            std::fprintf(stderr, "ExecutionArbiterMachineFuzz ZX-Poly seed %u: hang - unrecoverable, exiting\n", seed);
            std::fflush(stderr);
            std::_Exit(1);
        }
        for (auto& w : workers)
            w.join();
        group->StopRecording();
        EXPECT_EQ(frameErrors.load(), 0) << "seed " << seed;
        for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
            EXPECT_EQ(group->GetInstance(m)->DriverViolations(), 0u) << "seed " << seed << ", module " << m;
        master->Pause();
        const std::string id = master->GetId();
        master.reset();
        EXPECT_TRUE(manager->RemoveEmulator(id));
    }
}

/// RZX playback in the matrix: a running 48K playing a recording, one thread through the playback (play, stop, seek,
/// status) and two through the control. Never two drivers, no hang (a direct run under a playback ends a little
/// past its frame boundary: frames are not counted here)
TEST(ExecutionArbiterMachineFuzz_Functional, RzxPlaybackUnderConcurrentControl)
{
    SKIP_UNLESS_FUNCTIONAL();
    const std::string rzx =
        (TestPathHelper::FindProjectRoot() / "testdata" / "loaders" / "rzx" / "archive" / "garfield.rzx").string();
    const uint32_t first = FirstSeed();
    for (uint32_t seed = first; seed < first + SeedCount(); ++seed)
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr);
        emulator->EnableTurboMode();
        const rzx::PlayResult played = emulator->PlayRzx(rzx);
        ASSERT_TRUE(played.Ok()) << played.message;
        emulator->StartAsync();
        TestWait::For([&]() { return Frame(emulator) > 1; });

        std::atomic<int> frameErrors{0};
        std::atomic<int> finished{0};
        const int threads = 3;
        std::vector<std::thread> workers;
        for (int t = 0; t < threads; ++t)
            workers.emplace_back([&, t]() {
                std::mt19937 rng(seed * 32452843u + static_cast<uint32_t>(t));
                const execution::Requester who{t == 1 ? execution::Origin::WebApi : execution::Origin::Gui, {}};
                for (int op = 0; op < 25; ++op)
                {
                    if (t == 0)
                    {
                        switch (rng() % 5)
                        {
                            case 0: emulator->StopRzx(); break;
                            case 1: (void)emulator->PlayRzx(rzx); break;
                            case 2: emulator->SeekRzx(rng() % 200); break;
                            default: (void)emulator->GetRzxStatus(); break;
                        }
                        continue;
                    }
                    ControlOp(emulator, rng, who, false, frameErrors);
                }
                finished.fetch_add(1);
            });
        if (!TestWait::For([&]() { return finished.load() == threads; }, 20000ms))
        {
            std::fprintf(stderr, "ExecutionArbiterMachineFuzz RZX seed %u: hang - unrecoverable, exiting\n", seed);
            std::fflush(stderr);
            std::_Exit(1);
        }
        for (auto& w : workers)
            w.join();
        emulator->StopRzx();
        EXPECT_EQ(emulator->DriverViolations(), 0u) << "seed " << seed;
        emulator->Pause();
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

/// Probe (not in the default run): RunNFrames on a ZX-Poly master from one thread, group operations in between
TEST(ExecutionArbiterMachineFuzz_Functional, DISABLED_ZXPolyMasterRunNFramesIsExactSingleThreaded)
{
    SKIP_UNLESS_FUNCTIONAL();
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string error;
    std::shared_ptr<Emulator> master = manager->CreateZXPolyMachine(
        "zxpoly-probe", "PENTAGON", TestPathHelper::GetTestDataPath("machines/zxpoly/zxp/Alien8.zxp"), &error);
    ASSERT_TRUE(master) << error;
    ZXPolyGroup* group = manager->GetZXPolyGroup(master->GetId());
    for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
        group->GetInstance(m)->EnableTurboMode();
    if (!master->IsRunning())
        master->StartAsync();
    TestWait::For([&]() { return Frame(master.get()) > 1; });
    std::mt19937 rng(7);
    int errors = 0;
    for (int i = 0; i < 3000; i++)
    {
        switch (rng() % 8)
        {
            case 6:
                group->PressKey(ZXKEY_SPACE);
                group->ReleaseKey(ZXKEY_SPACE);
                break;
            case 7: group->SetParallelSlaves(rng() % 2 == 0); break;
            case 0: group->StartRecording(); break;
            case 1: group->StopRecording(); break;
            case 2: { const uint64_t now = Frame(master.get()); group->SeekToFrame(now > 4 ? now - 1 - rng() % 4 : now); break; }
            case 3: group->ResumeRecording(); break;
            case 4:
            {
                // Let the loop run on from where the group left it (after a seek: the detached replay)
                const uint64_t from = Frame(master.get());
                master->Resume(execution::Requester{execution::Origin::Gui, {}});
                TestWait::For([&]() { return master->IsPaused() || Frame(master.get()) > from + 3; }, 2000ms);
                break;
            }
            default: break;
        }
        execution::CommandOptions driving;
        driving.drivesZ80 = true;
        execution::HoldToken hold = master->GetArbiter().BeginHold(
            execution::Requester{execution::Origin::WebApi, {}}, "probe", execution::Clock::now() + std::chrono::seconds(5),
            nullptr, driving);
        const unsigned frames = 1 + rng() % 2;
        const uint64_t before = Frame(master.get());
        master->RunNFrames(frames);
        if (hold.Valid() && Frame(master.get()) - before != frames && ++errors < 10)
            std::fprintf(stderr, "probe %d: before %llu after %llu ran %u, ttd state %d\n", i, (unsigned long long)before,
                         (unsigned long long)Frame(master.get()), frames,
                         static_cast<int>(master->GetContext()->pTimeTravelController->GetState()));
        master->GetArbiter().EndHold(hold);
    }
    EXPECT_EQ(errors, 0);
    group->StopRecording();
    master->Pause();
    const std::string id = master->GetId();
    master.reset();
    manager->RemoveEmulator(id);
}
