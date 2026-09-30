// The host machine's execution, pinned: every shipped model boots its ROM and
// runs a demo, in fast and in debug mode, and must end in exactly the state
// recorded before the memory-interface selector and the host bus overlay slot
// were introduced (neogs-zxdma-design.md §5.2-§5.3, §8.1).
//
// Fast and debug mode must also agree with each other: the debug memory path
// adds tracking only, never behaviour.
//
// To re-record after an intended change of host behaviour (timing, a model's
// memory map), run with UNREALNG_RECORD_CORE_GOLDEN=1 and paste the printed
// table below.
//
// Runtime justification: 10 runs x 2 modes x 150 frames; about a second on
// the development machine.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_atm3.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"

namespace
{
struct Golden
{
    const char* model;
    const char* snapshot; // run after the boot, or nullptr
    uint64_t ramHash;
    uint64_t cpuHash;
    uint64_t tStates;
};

// Re-recorded 2026-09-30 when the runs switched from srand(0x4E47) power-on
// noise to zeroed power-on RAM ([MISC] RAMPowerOn=ZERO). Only the RAM hashes
// of machines whose ROM keeps screen pages 5 / 7 moved, plus the ATM3 CPU
// state (BaseConf reads that RAM while it boots); T-states and every other
// CPU state are unchanged. Earlier history: rows moved with master's changes
// to the machines themselves (contention, Profi/ATM ports, ZX-Evo BaseConf,
// the ATM IDE board status on the #7FFD class), not with the devices tested
const Golden kGolden[] = {
    {"48K", nullptr, 0xF6F7645FFB8D32EEull, 0x30CF0C783D0E991Full, 10483200ull},
    {"128k", nullptr, 0xEA31963B89AC26CDull, 0x99282911875249E1ull, 10636200ull},
    {"PLUS3", nullptr, 0x190BA788495BE1C7ull, 0x2A4FE2DFB3C7F009ull, 10636200ull},
    {"PENTAGON", nullptr, 0xDF565E9821C0C00Eull, 0x0AF8E05A04352BC0ull, 10752000ull},
    {"PENTAGON", "testdata/loaders/sna/eyeache1.sna", 0xBE8FEFDC569D139Aull, 0xE0C7CB5F6C6AB285ull, 10752000ull},
    {"SCORPION", nullptr, 0xF60D982DD39FBB7Aull, 0xA0ACA621D375DA98ull, 10483200ull},
    {"PROFSCORP", nullptr, 0xF60D982DD39FBB7Aull, 0xA0ACA621D375DA98ull, 10483200ull},
    {"PROFI", nullptr, 0xECE6582A89C2FD05ull, 0xA0ACA621D375DA98ull, 10483200ull},
    {"ATM710", nullptr, 0x32E968D662FA8C22ull, 0xE360A9F00E0771B9ull, 10483200ull},
    // ATM3 re-recorded 2026-09-30 (branch not-modeled-waits): its BIOS runs at 14 MHz, where the DRAM's cache
    // misses now wait (EvoTurboOverlay, docs/inprogress/2026-09-29-machine-waits)
    {"ATM3", nullptr, 0x6824EF056B8D72E9ull, 0xBAFAE3005C816E0Dull, 9434880ull},
    // TSL (TS-Conf): no row yet - the boot is covered by tsconf_boot_test (BOOT-1/2).
};

constexpr unsigned kFrames = 150;

struct Result
{
    bool created = false;
    uint64_t ramHash = 0, cpuHash = 0, tStates = 0;
};

Result run(const Golden& g, bool debug)
{
    Result r;
    // Every run starts from the same RAM: zero instead of the power-on noise
    // from the process-wide rand()
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(g.model, LoggerLevel::LogError, RamPowerOn::Zero);
    if (!emulator)
        return r;
    r.created = true;
    EmulatorContext* ctx = emulator->GetContext();
    // Real-time clocks read the host's wall clock: freeze them, as the
    // Scorpion and ZX-Evo boot tests do
    if (ctx->config.mem_model == MM_ATM3)
        static_cast<PortDecoder_ATM3*>(ctx->pPortDecoder)->GetRtc().SetFixedTime(1767268830);
    if (ctx->config.mem_model == MM_SCORP || ctx->config.mem_model == MM_PROFSCORP)
        static_cast<PortDecoder_Scorpion256*>(ctx->pPortDecoder)->GetRtc().SetFixedTime(1767268830);
    ctx->pFeatureManager->setFeature(Features::kDebugMode, debug);
    ctx->pMemory->UpdateFeatureCache();
    if (g.snapshot)
    {
        emulator->RunNFrames(50); // past the ROM's own start-up
        EXPECT_TRUE(emulator->LoadSnapshot((TestPathHelper::FindProjectRoot() / g.snapshot).string())) << g.snapshot;
    }
    emulator->RunNFrames(kFrames);

    const size_t ramBytes = static_cast<size_t>(ctx->config.ramsize) * 1024u;
    r.ramHash = ttd::HashBytes(ctx->pMemory->RAMBase(), ramBytes);
    const ttd::TTDCpuState cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(ctx->pCore->GetZ80()));
    r.cpuHash = ttd::HashBytes(reinterpret_cast<const uint8_t*>(&cpu), sizeof cpu);
    r.tStates = ctx->emulatorState.t_states;
    EmulatorTestHelper::CleanupEmulator(emulator);
    return r;
}
} // namespace

TEST(CoreGolden, EveryModelRunsExactlyAsRecordedInFastAndDebugMode)
{
    const bool record = std::getenv("UNREALNG_RECORD_CORE_GOLDEN") != nullptr;
    for (const Golden& g : kGolden)
    {
        const std::string what = std::string(g.model) + (g.snapshot ? std::string(" + ") + g.snapshot : "");
        SCOPED_TRACE(what);
        const Result fast = run(g, false);
        const Result debug = run(g, true);
        ASSERT_TRUE(fast.created) << "model not creatable in this build";

        EXPECT_EQ(debug.ramHash, fast.ramHash) << "debug mode changed the RAM";
        EXPECT_EQ(debug.cpuHash, fast.cpuHash) << "debug mode changed the CPU state";
        EXPECT_EQ(debug.tStates, fast.tStates) << "debug mode changed the timing";

        if (record)
        {
            printf("    {\"%s\", %s%s%s, 0x%016llXull, 0x%016llXull, %lluull},\n", g.model, g.snapshot ? "\"" : "",
                   g.snapshot ? g.snapshot : "nullptr", g.snapshot ? "\"" : "", static_cast<unsigned long long>(fast.ramHash),
                   static_cast<unsigned long long>(fast.cpuHash), static_cast<unsigned long long>(fast.tStates));
            continue;
        }
        EXPECT_EQ(fast.ramHash, g.ramHash) << "RAM differs from the recorded run";
        EXPECT_EQ(fast.cpuHash, g.cpuHash) << "CPU state differs from the recorded run";
        EXPECT_EQ(fast.tStates, g.tStates) << "T-state count differs from the recorded run";
    }
}
