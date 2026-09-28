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

// Recorded 2026-09-27 on neogs d34f2c56 (master 95fce44d merged), before phase 5a
const Golden kGolden[] = {
    {"48K", nullptr, 0xBA0556D09395EC44ull, 0x40BC7C9AAC0FAA36ull, 10483200ull},
    {"128k", nullptr, 0x78042E47C3DBDD4Bull, 0xE3A4F38236C6092Aull, 10636200ull},
    {"PLUS3", nullptr, 0xFD6BDBD869C26760ull, 0xCBAB763DF221C3A5ull, 10636200ull},
    {"PENTAGON", nullptr, 0xCE7802019C39F3DCull, 0x0AF8E05A04352BC0ull, 10752000ull},
    {"PENTAGON", "testdata/loaders/sna/eyeache1.sna", 0xBE8FEFDC569D139Aull, 0xE0C7CB5F6C6AB285ull, 10752000ull},
    {"SCORPION", nullptr, 0xB62AC29F6C8595C8ull, 0xA0ACA621D375DA98ull, 10483200ull},
    {"PROFSCORP", nullptr, 0xB62AC29F6C8595C8ull, 0xA0ACA621D375DA98ull, 10483200ull},
    {"PROFI", nullptr, 0xE0FAE946751F0EDFull, 0x1E71318A116D0D18ull, 10483200ull},
    {"ATM710", nullptr, 0x8AB82EB6A4994527ull, 0x27AEF4CA26888579ull, 10483200ull},
    {"ATM3", nullptr, 0x0000000000000000ull, 0x0000000000000000ull, 0ull},
    // TSL (TS-Conf) is not creatable with the shipped ROMs.
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
    // Power-on RAM (pages 5 and 7) is filled with rand(): seed it so every
    // run starts from the same contents
    std::srand(0x4E47);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(g.model, LoggerLevel::LogError);
    if (!emulator)
        return r;
    r.created = true;
    EmulatorContext* ctx = emulator->GetContext();
    // The Scorpion's real-time clock reads the host's wall clock: freeze it,
    // as the Scorpion boot tests do
    if (ctx->config.mem_model == MM_SCORP || ctx->config.mem_model == MM_PROFSCORP)
        static_cast<PortDecoder_Scorpion256*>(ctx->pPortDecoder)->GetSMUCNvram().SetFixedTime(1767268830);
    // ATM3's CMOS/RTC also reads the host wall clock: freeze it too, as the
    // ZX-Evo boot tests do
    if (ctx->config.mem_model == MM_ATM3)
        static_cast<PortDecoder_ATM3*>(ctx->pPortDecoder)->GetCMOS().SetFixedTime(1767268830);
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
