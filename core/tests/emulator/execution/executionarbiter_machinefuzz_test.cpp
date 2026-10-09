// Machine fuzz of the execution arbiter (docs/inprogress/2026-10-09-execution-arbiter/design.md §8.2): several threads
// drive one running Pentagon at once through the public control surface - pauses, resumes, silent pairs, direct runs
// (frames, T-states, single instructions), commands - in a seeded mix. Checked on every run: never two threads
// executing the Z80 (I1), every RunNFrames(k) advances exactly k frames (no main-loop frame slipped in), no hang (I9),
// and the machine ends parked when paused. UNREAL_EXEC_FUZZ_SEED replays one seed, UNREAL_EXEC_FUZZ_ITERATIONS runs
// more (default 3: each seed boots a machine, about 100 ms).

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

using namespace std::chrono_literals;

namespace
{

uint64_t Frame(Emulator* emulator)
{
    return emulator->GetContext()->emulatorState.frame_counter;
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
                        // A silent pair around a short piece of work (the TTD / media brackets)
                        emulator->Pause(false);
                        for (int i = static_cast<int>(rng() % 20); i > 0; --i)
                            std::this_thread::yield();
                        emulator->Resume(false);
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
TEST(ExecutionArbiterMachineFuzz_Test, ConcurrentControlKeepsOneDriverAndExactSteps)
{
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
TEST(ExecutionArbiterMachineFuzz_Test, CommandsFromOtherThreadsNeverReachIntoAStep)
{
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
        std::vector<std::thread> noise;
        for (int i = 0; i < noiseThreads; ++i)
            noise.emplace_back([&, i]() {
                const execution::Requester who{i % 2 ? execution::Origin::Gui : execution::Origin::Ttd, {}};
                while (!stop.load())
                {
                    emulator->GetArbiter().Execute(who, "noise", [&](execution::CommandContext&) {
                        Memory* memory = emulator->GetContext()->pMemory;
                        const uint8_t frames = memory->DirectReadFromZ80Memory(23672);
                        memory->DirectWriteToZ80Memory(23672, static_cast<uint8_t>(frames ^ 0xFF));
                        std::this_thread::yield();
                        memory->DirectWriteToZ80Memory(23672, frames);
                    });
                    emulator->Pause(false);  // a silent pair as well (the machine is paused: the compat no-op)
                    emulator->Resume(false);
                }
            });
        emulator->RunNFrames(40);
        stop = true;
        for (auto& n : noise)
            n.join();
        return MachineHash(emulator);
    };

    const uint64_t quiet = runFrom(0);
    EXPECT_EQ(runFrom(0), quiet) << "the run itself is not deterministic: the check below would mean nothing";
    EXPECT_EQ(runFrom(3), quiet) << "a command from another thread changed the machine during the step";
    EXPECT_EQ(emulator->DriverViolations(), 0u);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
