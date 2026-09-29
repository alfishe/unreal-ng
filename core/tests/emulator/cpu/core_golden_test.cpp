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

// Re-recorded 2026-09-28 at the merge of master 9454993c into neogs. The rows
// are exactly what clean master produces; they moved with master's changes
// to the machines themselves (contention, Profi/ATM ports, ZX-Evo BaseConf),
// not with anything NeoGS does
const Golden kGolden[] = {
    {"48K", nullptr, 0xF6F7645FFB8D32EEull, 0x30CF0C783D0E991Full, 10483200ull},
    {"128k", nullptr, 0x06DE8AB61A04BA77ull, 0x99282911875249E1ull, 10636200ull},
    {"PLUS3", nullptr, 0x42083412FAC68EB5ull, 0x2A4FE2DFB3C7F009ull, 10636200ull},
    {"PENTAGON", nullptr, 0xCE7802019C39F3DCull, 0x0AF8E05A04352BC0ull, 10752000ull},
    {"PENTAGON", "testdata/loaders/sna/eyeache1.sna", 0xBE8FEFDC569D139Aull, 0xE0C7CB5F6C6AB285ull, 10752000ull},
    {"SCORPION", nullptr, 0xB62AC29F6C8595C8ull, 0xA0ACA621D375DA98ull, 10483200ull},
    {"PROFSCORP", nullptr, 0xB62AC29F6C8595C8ull, 0xA0ACA621D375DA98ull, 10483200ull},
    {"PROFI", nullptr, 0xE0FAE946751F0EDFull, 0xA0ACA621D375DA98ull, 10483200ull},
    // ATM710 re-recorded 2026-09-28 (branch ide-atapi): its ROM reads the
    // #7FFD class, which is now the ATM IDE board's status (#3F + INTRQ),
    // as in UnrealSpeccy, instead of the floating bus
    {"ATM710", nullptr, 0x9DCBD8315B37FC1Eull, 0xE360A9F00E0771B9ull, 10483200ull},
    {"ATM3", nullptr, 0x6F2CE72E5BE7AC25ull, 0x5D592C8522429BF3ull, 9434880ull},
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
    // Real-time clocks read the host's wall clock: freeze them, as the
    // Scorpion and ZX-Evo boot tests do
    if (ctx->config.mem_model == MM_ATM3)
        static_cast<PortDecoder_ATM3*>(ctx->pPortDecoder)->GetCMOS().SetFixedTime(1767268830);
    if (ctx->config.mem_model == MM_SCORP || ctx->config.mem_model == MM_PROFSCORP)
        static_cast<PortDecoder_Scorpion256*>(ctx->pPortDecoder)->GetSMUCNvram().SetFixedTime(1767268830);
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
