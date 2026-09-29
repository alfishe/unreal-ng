#include "stdafx.h"
#include "pch.h"

#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

/// Per-model timing fingerprints: the regression net for the contention rework (M1 contention, design
/// docs/inprogress/2026-09-28-m1-contention/design.md §8 suite F).
///
/// Every creatable model runs one fixed instruction mix from a fixed T-state (just before the paper, with
/// interrupts off) in several placements: code and data in uncontended RAM, data in contended RAM, code in
/// contended RAM, code in a paged-in page 7, and code in the +2A/+3 all-RAM layout. Each run records the
/// total T-states, a hash of the per-instruction T-state sequence and a hash of the resulting registers and
/// data memory. The expectations below were recorded on the code BEFORE the rework; a placement may only
/// change where the rework intends it to (contended code on a contended model) and must say so here.
///
/// Re-record (prints the table in C++ form): UNREAL_CONTENTION_GOLDEN_PRINT=1 core-tests
///     --gtest_filter='ContentionRegression_Test.*'

namespace
{
/// The instruction mix (position independent; runs forever, the test counts instructions):
/// data reads and writes, stack, EX (SP),HL, INC (HL), indexed read and write, LDI, port in/out
/// (#40FE: contended high byte, even port) and a relative jump
const uint8_t kProgram[] = {
    0x7E,                    // LD A,(HL)
    0x12,                    // LD (DE),A
    0x23,                    // INC HL
    0x13,                    // INC DE
    0xC5,                    // PUSH BC
    0xC1,                    // POP BC
    0xE3,                    // EX (SP),HL
    0xE3,                    // EX (SP),HL
    0x34,                    // INC (HL)
    0xDD, 0xCB, 0x01, 0x46,  // BIT 0,(IX+1)
    0xFD, 0x77, 0x02,        // LD (IY+2),A
    0xED, 0xA0,              // LDI
    0x01, 0xFE, 0x40,        // LD BC,#40FE
    0xED, 0x78,              // IN A,(C)
    0xED, 0x79,              // OUT (C),A
    0x18, 0xE5,              // JR start
};

constexpr uint32_t kInstructions = 1500;       // ~35-45 kT: stays inside one frame on every model
constexpr uint32_t kStartAfterInt = 14000;     // just before the first contended T of any model

enum class Layout
{
    Standard,  // the model's power-on mapping
    Page7,     // RAM page 7 at #C000 (models with #7FFD paging)
    AllRam1,   // +2A/+3 all-RAM layout 1: pages 4,5,6,7
};

struct Scenario
{
    const char* name;
    Layout layout;
    uint16_t code;
    uint16_t data;   // HL; DE = data + #1000, IX = data + #100, IY = data + #200
    uint16_t stack;  // SP
};

const Scenario kScenarios[] = {
    { "free", Layout::Standard, 0x8000, 0x9000, 0xB000 },
    { "dataContended", Layout::Standard, 0x8000, 0x4000, 0x5F00 },
    { "codeContended", Layout::Standard, 0x6000, 0x9000, 0xB000 },
    { "codePage7", Layout::Page7, 0xC000, 0x9000, 0xB000 },
    { "allRam1", Layout::AllRam1, 0x0000, 0x9000, 0xB000 },
};

/// Every model the emulator can create (GET /api/v1/emulator/models, creatable = true)
const char* const kModels[] = {
    "PENTAGON", "48K", "128K", "PLUS2", "PLUS2A", "PLUS3", "ATM710", "ATM3", "SCORPION", "PROFSCORP", "PROFI",
};

struct Golden
{
    const char* model;
    const char* scenario;
    uint32_t totalT;
    uint64_t timingHash;
    uint64_t stateHash;
};

/// Recorded on 2b6b7d47, before the contention rework, then updated by the M1 contention rework, by phase 2
/// (internal-cycle contention on the Ferranti ULA models) and by phase 3 (multi-point I/O contention: the mix's
/// #40FE port has a contended high byte, C:1, C:3); the changed rows keep their last former values in a comment.
/// Reading guide: on 48K / 128K / +2 "free" is longer than on the Pentagon only by
/// the I/O contention of the #40FE port accesses. Before the rework "codeContended", "codePage7" and "allRam1"
/// equalled "free" on the ULA / gate array models (opcode fetches and operand bytes were not contended); the
/// rework changed exactly those rows, and only their timing - every state hash and every other row is
/// unchanged
const std::vector<Golden> kGolden = {
    { "PENTAGON", "free", 18472, 0xda498385dc3d51b5ull, 0x9656ea3c0af12b0ull },
    { "PENTAGON", "dataContended", 18472, 0xda498385dc3d51b5ull, 0x96d5e7201636f1f5ull },
    { "PENTAGON", "codeContended", 18472, 0xda498385dc3d51b5ull, 0xa082cdbbf9f71b90ull },
    { "PENTAGON", "codePage7", 18472, 0xda498385dc3d51b5ull, 0xd3fb2cce03135670ull },
    { "48K", "free", 19089, 0x415f4c19a0b97f26ull, 0x9656ea3c0af12b0ull },  // phase 3 (multi-point I/O): was 18675, 0xa31a04e8c4376db4ull
    { "48K", "dataContended", 20983, 0xfce88e51c4b9e13aull, 0x96d5e7201636f1f5ull },  // phase 3 (multi-point I/O): was 20983, 0x814440bc31b64aaull
    { "48K", "codeContended", 23580, 0x7865ddf37ce49607ull, 0xa082cdbbf9f71b90ull },  // phase 3 (multi-point I/O): was 23580, 0xe1579470c3943069ull
    { "128K", "free", 18894, 0xa40e70e4a780dfe7ull, 0x9656ea3c0af12b0ull },  // phase 3 (multi-point I/O): was 19002, 0x7c4752cc09b9aa21ull
    { "128K", "dataContended", 21332, 0x89cfa2b88f70cf87ull, 0x96d5e7201636f1f5ull },  // phase 3 (multi-point I/O): was 21332, 0x76b9bd6bbc8ec6f3ull
    { "128K", "codeContended", 21518, 0x159483e3a865abbfull, 0xa082cdbbf9f71b90ull },  // phase 3 (multi-point I/O): was 23527, 0x19226770f606d2d4ull
    { "128K", "codePage7", 21518, 0x159483e3a865abbfull, 0xd3fb2cce03135670ull },  // phase 3 (multi-point I/O): was 23527, 0x19226770f606d2d4ull
    { "PLUS2", "free", 18894, 0xa40e70e4a780dfe7ull, 0x9656ea3c0af12b0ull },  // phase 3 (multi-point I/O): was 19002, 0x7c4752cc09b9aa21ull
    { "PLUS2", "dataContended", 21332, 0x89cfa2b88f70cf87ull, 0x96d5e7201636f1f5ull },  // phase 3 (multi-point I/O): was 21332, 0x76b9bd6bbc8ec6f3ull
    { "PLUS2", "codeContended", 21518, 0x159483e3a865abbfull, 0xa082cdbbf9f71b90ull },  // phase 3 (multi-point I/O): was 23527, 0x19226770f606d2d4ull
    { "PLUS2", "codePage7", 21518, 0x159483e3a865abbfull, 0xd3fb2cce03135670ull },  // phase 3 (multi-point I/O): was 23527, 0x19226770f606d2d4ull
    { "PLUS2A", "free", 18472, 0xda498385dc3d51b5ull, 0x9656ea3c0af12b0ull },
    { "PLUS2A", "dataContended", 21328, 0xcd35793f95f18839ull, 0x96d5e7201636f1f5ull },
    { "PLUS2A", "codeContended", 21323, 0x8af80abb538938ccull, 0xa082cdbbf9f71b90ull },  // M1 rework: was 18472, 0xda498385dc3d51b5ull
    { "PLUS2A", "codePage7", 21323, 0x8af80abb538938ccull, 0xd3fb2cce03135670ull },  // M1 rework: was 18472, 0xda498385dc3d51b5ull
    { "PLUS2A", "allRam1", 25133, 0x48c07781c7346f7eull, 0xd6de33ccfc7d2330ull },  // M1 rework: was 21328, 0xcd35793f95f18839ull
    { "PLUS3", "free", 18472, 0xda498385dc3d51b5ull, 0x9656ea3c0af12b0ull },
    { "PLUS3", "dataContended", 21328, 0xcd35793f95f18839ull, 0x96d5e7201636f1f5ull },
    { "PLUS3", "codeContended", 21323, 0x8af80abb538938ccull, 0xa082cdbbf9f71b90ull },  // M1 rework: was 18472, 0xda498385dc3d51b5ull
    { "PLUS3", "codePage7", 21323, 0x8af80abb538938ccull, 0xd3fb2cce03135670ull },  // M1 rework: was 18472, 0xda498385dc3d51b5ull
    { "PLUS3", "allRam1", 25133, 0x48c07781c7346f7eull, 0xd6de33ccfc7d2330ull },  // M1 rework: was 21328, 0xcd35793f95f18839ull
    { "ATM710", "free", 18472, 0xda498385dc3d51b5ull, 0xfdd8f5de856c2a7eull },
    { "ATM710", "dataContended", 18472, 0xda498385dc3d51b5ull, 0x626b20c80e522358ull },
    { "ATM710", "codeContended", 18472, 0xda498385dc3d51b5ull, 0x297c8af6be67ca86ull },
    { "ATM710", "codePage7", 18472, 0xda498385dc3d51b5ull, 0xb23aa361896c573eull },
    { "ATM3", "free", 18472, 0xda498385dc3d51b5ull, 0xfdd8f5de856c2a7eull },
    { "ATM3", "dataContended", 18472, 0xda498385dc3d51b5ull, 0x626b20c80e522358ull },
    { "ATM3", "codeContended", 18472, 0xda498385dc3d51b5ull, 0x297c8af6be67ca86ull },
    { "ATM3", "codePage7", 18472, 0xda498385dc3d51b5ull, 0xb23aa361896c573eull },
    { "SCORPION", "free", 19130, 0x8a36e1fde0d39ab5ull, 0x9656ea3c0af12b0ull },  // Even M1: was 18472, 0xda498385dc3d51b5ull
    { "SCORPION", "dataContended", 19130, 0x8a36e1fde0d39ab5ull, 0x96d5e7201636f1f5ull },  // Even M1: was 18472, 0xda498385dc3d51b5ull
    { "SCORPION", "codeContended", 19130, 0x8a36e1fde0d39ab5ull, 0xa082cdbbf9f71b90ull },  // Even M1: was 18472, 0xda498385dc3d51b5ull
    { "SCORPION", "codePage7", 19130, 0x8a36e1fde0d39ab5ull, 0xd3fb2cce03135670ull },  // Even M1: was 18472, 0xda498385dc3d51b5ull
    { "PROFSCORP", "free", 19130, 0x8a36e1fde0d39ab5ull, 0x9656ea3c0af12b0ull },  // Even M1: was 18472, 0xda498385dc3d51b5ull
    { "PROFSCORP", "dataContended", 19130, 0x8a36e1fde0d39ab5ull, 0x96d5e7201636f1f5ull },  // Even M1: was 18472, 0xda498385dc3d51b5ull
    { "PROFSCORP", "codeContended", 19130, 0x8a36e1fde0d39ab5ull, 0xa082cdbbf9f71b90ull },  // Even M1: was 18472, 0xda498385dc3d51b5ull
    { "PROFSCORP", "codePage7", 19130, 0x8a36e1fde0d39ab5ull, 0xd3fb2cce03135670ull },  // Even M1: was 18472, 0xda498385dc3d51b5ull
    { "PROFI", "free", 18472, 0xda498385dc3d51b5ull, 0x9656ea3c0af12b0ull },
    { "PROFI", "dataContended", 18472, 0xda498385dc3d51b5ull, 0x96d5e7201636f1f5ull },
    { "PROFI", "codeContended", 18472, 0xda498385dc3d51b5ull, 0xa082cdbbf9f71b90ull },
    { "PROFI", "codePage7", 18472, 0xda498385dc3d51b5ull, 0xd3fb2cce03135670ull },
};

struct Fnv
{
    uint64_t h = 1469598103934665603ull;
    void Add(uint64_t v, int bytes)
    {
        for (int i = 0; i < bytes; i++)
        {
            h ^= (v >> (i * 8)) & 0xFF;
            h *= 1099511628211ull;
        }
    }
};

struct Fingerprint
{
    bool applicable = false;
    uint32_t totalT = 0;
    uint64_t timingHash = 0;
    uint64_t stateHash = 0;
};

/// Data memory the mix touches (HL / IX / IY, DE, the stack), hashed into the state fingerprint
constexpr uint16_t kRegionSize = 0x800;
std::array<uint16_t, 3> HashedRegions(const Scenario& s)
{
    return { s.data, static_cast<uint16_t>(s.data + 0x1000), static_cast<uint16_t>(s.stack - 0x10) };
}

bool HasPaging(const std::string& model) { return model != "48K"; }
bool HasAllRamLayouts(const std::string& model) { return model == "PLUS2A" || model == "PLUS3"; }
}  // namespace

class ContentionRegression_Test : public ::testing::Test
{
protected:
    Fingerprint Run(const std::string& model, const Scenario& s)
    {
        Fingerprint fp;
        if ((s.layout == Layout::Page7 && !HasPaging(model)) || (s.layout == Layout::AllRam1 && !HasAllRamLayouts(model)))
            return fp;

        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr) << model;
        if (!emulator)
            return fp;

        EmulatorContext* context = emulator->GetContext();
        Z80* z80 = context->pCore->GetZ80();
        Memory* memory = context->pMemory;
        context->pScreen->InitFrame();  // applies the model's video mode and contention rule

        // Paging through the machine's own ports, as software does: a raw Memory bank setter does not
        // reach every model's CPU view (ATM710 keeps fetching from its previous mapping)
        if (s.layout != Layout::Standard)
            context->emulatorState.p7FFD &= static_cast<uint8_t>(~0x20);  // 48 BASIC locks paging; unlock
        if (s.layout == Layout::Page7)
            context->pPortDecoder->DecodePortOut(0x7FFD, 0x17, 0x8000);  // page 7 at #C000, 48 BASIC ROM
        if (s.layout == Layout::AllRam1)
            context->pPortDecoder->DecodePortOut(0x1FFD, 0x03, 0x8000);  // pages 4,5,6,7

        // The hashed regions start from a known pattern: RAM keeps whatever earlier emulator instances in the
        // process left there, which must not leak into the fingerprint
        for (uint16_t base : HashedRegions(s))
            for (uint16_t k = 0; k < kRegionSize; k++)
                memory->DirectWriteToZ80Memory(static_cast<uint16_t>(base + k), static_cast<uint8_t>(k * 7 + (base >> 8)));

        for (size_t i = 0; i < sizeof(kProgram); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(s.code + i), kProgram[i]);

        z80->pc = s.code;
        z80->hl = s.data;
        z80->de = static_cast<uint16_t>(s.data + 0x1000);
        z80->ix = static_cast<uint16_t>(s.data + 0x100);
        z80->iy = static_cast<uint16_t>(s.data + 0x200);
        z80->sp = s.stack;
        z80->bc = 0x40FE;
        z80->af = 0x0000;
        z80->iff1 = 0;
        z80->iff2 = 0;
        z80->t = context->config.intstart + 1 + kStartAfterInt;

        Fnv timing;
        const uint32_t t0 = z80->t;
        for (uint32_t i = 0; i < kInstructions; i++)
        {
            const uint32_t before = z80->t;
            z80->Z80Step();
            timing.Add(z80->t - before, 2);
        }

        // The mix loops forever inside itself: a PC outside it means the placement did not map what the
        // scenario says (the CPU ran other bytes) and the fingerprint would be meaningless
        EXPECT_GE(z80->pc, s.code) << model << " / " << s.name << ": ran outside the program";
        EXPECT_LT(z80->pc, s.code + sizeof(kProgram)) << model << " / " << s.name << ": ran outside the program";

        Fnv state;
        state.Add(z80->af, 2);
        state.Add(z80->bc, 2);
        state.Add(z80->de, 2);
        state.Add(z80->hl, 2);
        state.Add(z80->ix, 2);
        state.Add(z80->iy, 2);
        state.Add(z80->sp, 2);
        state.Add(z80->pc, 2);
        for (uint16_t base : HashedRegions(s))
            for (uint16_t k = 0; k < kRegionSize; k++)
                state.Add(memory->DirectReadFromZ80Memory(static_cast<uint16_t>(base + k)), 1);

        fp.applicable = true;
        fp.totalT = z80->t - t0;
        fp.timingHash = timing.h;
        fp.stateHash = state.h;

        EmulatorTestHelper::CleanupEmulator(emulator);
        return fp;
    }
};

/// One test for all models (each run is ~1 ms of stepping, the emulator creation dominates: ~11 models x
/// 5 placements, a few hundred ms in total - the suite's whole point is breadth, see the file comment)
TEST_F(ContentionRegression_Test, TimingFingerprintsMatchTheRecordedBaseline)
{
    const bool print = std::getenv("UNREAL_CONTENTION_GOLDEN_PRINT") != nullptr;
    std::ostringstream table;
    size_t compared = 0;

    for (const char* model : kModels)
    {
        for (const Scenario& s : kScenarios)
        {
            const Fingerprint fp = Run(model, s);
            if (!fp.applicable)
                continue;

            if (print)
            {
                table << "    { \"" << model << "\", \"" << s.name << "\", " << std::dec << fp.totalT << ", 0x" << std::hex
                      << fp.timingHash << "ull, 0x" << fp.stateHash << "ull },\n";
                continue;
            }

            const Golden* golden = nullptr;
            for (const Golden& g : kGolden)
                if (model == std::string(g.model) && std::string(s.name) == g.scenario)
                    golden = &g;
            ASSERT_NE(golden, nullptr) << model << " / " << s.name << ": no recorded baseline";

            EXPECT_EQ(fp.totalT, golden->totalT) << model << " / " << s.name << ": total T-states";
            EXPECT_EQ(fp.timingHash, golden->timingHash) << model << " / " << s.name << ": per-instruction timing";
            EXPECT_EQ(fp.stateHash, golden->stateHash) << model << " / " << s.name << ": registers and data memory";
            compared++;
        }
    }

    if (print)
        std::cout << table.str();
    else
        EXPECT_EQ(compared, kGolden.size()) << "a baseline entry has no scenario";
}
