// bench-z80ex.cpp - head-to-head benchmark: extracted Z80 core vs z80ex.
//
// Runs the same synthetic workloads (mixed ALU/memory loop, 16K LDIR copy,
// arithmetic loop, NOP-sled dispatch probe) on both cores through identical
// callback buses and prints sustained T-states/sec side by side. z80ex
// (scratch/z80ex-dl, GPL v2) is the structural reference for this extraction
// and a widely used baseline.
//
// Fairness: both cores run byte-identical guest code and their callbacks
// index file-scope 64K arrays of the same shape (no hidden pointer chases on
// either side). Memory prep happens outside the timed region.
//
// Note: z80ex_step() executes one opcode per call and reports prefixes as
// separate steps (z80ex_last_op_type), so instructions/sec are not directly
// comparable; T-states/sec is the common denominator used here.
//
// Build & run:  ninja -C build z80bench-z80ex && ./build/z80bench-z80ex

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "hostmachine.h"
#include "z80cpu.h"
#include "z80ex.h"

using Clock = std::chrono::steady_clock;

namespace
{
uint8_t gMem[0x10000];      // z80ex guest RAM (behind its callbacks)
uint8_t gOurMem[0x10000];   // extracted-core guest RAM (behind our callbacks)

// --- shared workloads (identical bytes to benchmarks/bench-main.cpp) -------

const uint8_t kMixed[] = {
    0x06, 0x00, 0x7E, 0x86, 0x23, 0x19, 0xF5, 0xF1, 0x10, 0xF8, 0x76,
};
const uint8_t kLdir[] = {
    0x21, 0x00, 0x80, 0x11, 0x00, 0xC0, 0x01, 0x00, 0x40, 0xED, 0xB0, 0x76,
};
// matches bench-main.cpp: LD A,0 / LD B,0 / { ADD A,7; ADC A,3; SUB 5; CP 80h;
// DJNZ } / HALT - 256 iterations per pass
const uint8_t kArith[] = {
    0x3E, 0x00, 0x06, 0x00, 0xC6, 0x07, 0xCE, 0x03, 0xD6, 0x05, 0xFE, 0x80,
    0x10, 0xF7, 0x76,
};
// "nops": 64 consecutive NOPs + HALT. Pure opcode-fetch + dispatch probe -
// no operand fetches, no data access, 4 T per step.
uint8_t kNops[65];
const bool kNopsInit = [] {
    for (int i = 0; i < 64; ++i)
        kNops[i] = 0x00;
    kNops[64] = 0x76;
    return true;
}();

struct Workload
{
    const char* name;
    const uint8_t* code;
    size_t len;
    int repeats;
};

const Workload kWorkloads[] = {
    {"mixed", kMixed, sizeof(kMixed), 10000},
    {"ldir", kLdir, sizeof(kLdir), 300},
    {"arith", kArith, sizeof(kArith), 20000},
    {"nops", kNops, sizeof(kNops), 400000},
};

// --- z80ex callbacks (flat RAM behind the callback interface) --------------

Z80EX_BYTE ZexRead(Z80EX_CONTEXT*, Z80EX_WORD addr, int, void*)
{
    return gMem[addr];
}

void ZexWrite(Z80EX_CONTEXT*, Z80EX_WORD addr, Z80EX_BYTE value, void*)
{
    gMem[addr] = value;
}

Z80EX_BYTE ZexPortIn(Z80EX_CONTEXT*, Z80EX_WORD, void*)
{
    return 0xFF;
}

void ZexPortOut(Z80EX_CONTEXT*, Z80EX_WORD, Z80EX_BYTE, void*) {}

Z80EX_BYTE ZexIntVector(Z80EX_CONTEXT*, void*)
{
    return 0xFF;
}

// --- extracted-core callbacks (same flat RAM shape as the z80ex side) ------

uint8_t OurRead(Z80CPU*, uint16_t addr, int, void*)
{
    return gOurMem[addr];
}

void OurWrite(Z80CPU*, uint16_t addr, uint8_t value, void*)
{
    gOurMem[addr] = value;
}

double RunZ80Ex(const Workload& w)
{
    Z80EX_CONTEXT* ctx = z80ex_create(ZexRead, nullptr, ZexWrite, nullptr, ZexPortIn, nullptr,
                                      ZexPortOut, nullptr, ZexIntVector, nullptr);

    // Prepare guest memory once; the timed loop below only resets CPU state
    // (none of the workloads overwrite their own code or branch on RAM data).
    std::memset(gMem, 0, sizeof(gMem));
    std::memcpy(gMem + 0x8000, w.code, w.len);

    const Clock::time_point start = Clock::now();
    for (int r = 0; r < w.repeats; ++r)
    {
        z80ex_reset(ctx);
        z80ex_set_reg(ctx, regPC, 0x8000);
        z80ex_set_reg(ctx, regSP, 0xFFF0);
        while (!z80ex_doing_halt(ctx))
            z80ex_step(ctx);
    }

    const double sec = std::chrono::duration<double>(Clock::now() - start).count();
    z80ex_destroy(ctx);
    return sec;
}

double RunOurs(const Workload& w)
{
    // Bare core on the callback bus - same integration shape as the z80ex run
    // (a HostMachine here would only add its flat-memory machinery).
    Z80CPU* cpu = Z80CpuCreate();
    Z80CpuAttachMemory(cpu, nullptr);  // both cores on equal terms: callbacks
    Z80CpuSetMemoryBus(cpu, OurRead, nullptr, OurWrite, nullptr);
    std::memset(gOurMem, 0, sizeof(gOurMem));
    std::memcpy(gOurMem + 0x8000, w.code, w.len);

    const Clock::time_point start = Clock::now();
    for (int r = 0; r < w.repeats; ++r)
    {
        Z80CpuReset(cpu);
        Z80CpuSetReg(cpu, Z80CpuRegPc, 0x8000);
        Z80CpuSetReg(cpu, Z80CpuRegSp, 0xFFF0);
        while (!Z80CpuHalted(cpu))
            Z80CpuStep(cpu);
    }
    const double sec = std::chrono::duration<double>(Clock::now() - start).count();
    Z80CpuDestroy(cpu);
    return sec;
}

// T-states executed by one pass of a workload (independent of speed).
uint64_t WorkloadTstates(const Workload& w)
{
    HostMachine host;
    host.Reset();
    host.LoadCode(0x8000, w.code, w.len);
    Z80CpuSetReg(host.cpu, Z80CpuRegPc, 0x8000);
    Z80CpuSetReg(host.cpu, Z80CpuRegSp, 0xFFF0);
    while (!Z80CpuHalted(host.cpu))
        host.StepCapture();
    return Z80CpuTstates(host.cpu) - 3;  // minus reset T
}
}  // namespace

int main()
{
    printf("PoC 017 head-to-head: extracted core (callback bus) vs z80ex %s\n\n",
           z80ex_get_version()->as_string);
    printf("%-8s %12s %14s %14s %9s\n", "workload", "T-states", "z80ex T/s", "poc017 T/s", "ratio");

    double oursSum = 0.0, zexSum = 0.0;
    for (const Workload& w : kWorkloads)
    {
        const uint64_t t = WorkloadTstates(w) * static_cast<uint64_t>(w.repeats);

        double zexSec = 1e30, ourSec = 1e30;
        for (int i = 0; i < 3; ++i)
        {
            zexSec = std::min(zexSec, RunZ80Ex(w));
            ourSec = std::min(ourSec, RunOurs(w));
        }

        const double zexTps = t / zexSec, ourTps = t / ourSec;
        oursSum += ourTps;
        zexSum += zexTps;
        printf("%-8s %12llu %13.2fM %13.2fM %8.2fx\n", w.name,
               static_cast<unsigned long long>(t), zexTps / 1e6, ourTps / 1e6, ourTps / zexTps);
    }

    printf("\naggregate: z80ex %.2fM T/s, poc017(callback) %.2fM T/s -> %.2fx\n", zexSum / 4e6,
           oursSum / 4e6, oursSum / zexSum);
    return 0;
}
