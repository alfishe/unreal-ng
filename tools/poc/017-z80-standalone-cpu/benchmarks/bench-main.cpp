// bench-main.cpp - performance benchmark for the standalone Z80 core.
//
// Measures sustained T-states/sec and instructions/sec on three synthetic
// workloads (mixed ALU+memory loop, 16K LDIR block copy, arithmetic loop) in
// both bus modes:
//   - flat memory (Z80CpuAttachMemory): inline array access fast path
//   - callback bus (Z80CpuSetMemoryBus): z80ex-style host integration
// plus a HALT-step microbench of the raw stepping overhead.
//
// Each workload runs best-of-3. Numbers are T-states/sec - the common
// denominator comparable across cores (instructions differ in weight).
//
// Build & run:  ninja -C build && ./build/z80bench

#include <chrono>
#include <cstdio>
#include <cstring>

#include "hostmachine.h"

using Clock = std::chrono::steady_clock;

namespace
{
// --- guest workloads (hand-assembled, terminated by HALT) -----------------

// "mixed": ALU + (HL) memory + 16-bit add + stack + DJNZ, ~65 T / iteration
const uint8_t kMixed[] = {
    0x06, 0x00,  // LD B,0        ; 256 iterations per pass
    0x7E,        // loop: LD A,(HL)
    0x86,        //       ADD A,(HL)
    0x23,        //       INC HL
    0x19,        //       ADD HL,DE
    0xF5,        //       PUSH AF
    0xF1,        //       POP AF
    0x10, 0xF8,  //       DJNZ loop
    0x76,        // HALT
};

// "ldir": copy 16 KiB with LDIR (21 T per byte)
const uint8_t kLdir[] = {
    0x21, 0x00, 0x80,  // LD HL,8000h
    0x11, 0x00, 0xC0,  // LD DE,C000h
    0x01, 0x00, 0x40,  // LD BC,4000h
    0xED, 0xB0,        // LDIR
    0x76,              // HALT
};

// "arith": 8-bit ALU chain + DJNZ (no memory data access), 256 iters/pass
const uint8_t kArith[] = {
    0x3E, 0x00,  // LD A,0
    0x06, 0x00,  // LD B,0        ; 256 iterations
    0xC6, 0x07,  // loop: ADD A,7
    0xCE, 0x03,  //       ADC A,3
    0xD6, 0x05,  //       SUB 5
    0xFE, 0x80,  //       CP 80h
    0x10, 0xF7,  //       DJNZ loop
    0x76,        // HALT
};

// "nops": 64 consecutive NOPs + HALT - pure opcode-fetch + dispatch probe,
// no operand fetches or data access, 4 T per step (same bytes as bench-z80ex).
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
    int repeats;  // outer repetitions of the guest program per measured run
};

const Workload kWorkloads[] = {
    {"mixed", kMixed, sizeof(kMixed), 10000},   // ~166M T per measured run
    {"ldir", kLdir, sizeof(kLdir), 300},       // ~103M T
    {"arith", kArith, sizeof(kArith), 20000},  // ~210M T
    {"nops", kNops, sizeof(kNops), 400000},    // ~104M T
};

// --- callback-bus shims ----------------------------------------------------

HostMachine* gHost = nullptr;

uint8_t CallbackMemRead(Z80CPU*, uint16_t addr, int, void*)
{
    return gHost->mem[addr];
}

void CallbackMemWrite(Z80CPU*, uint16_t addr, uint8_t value, void*)
{
    gHost->mem[addr] = value;
}

struct RunResult
{
    double seconds;
    uint64_t tstates;
    uint64_t instructions;
};

// Runs one workload once (all repeats). Guest memory is prepared once and
// only CPU state is reset per repeat (none of the workloads overwrite their
// own code or branch on RAM data), so the 64K memset never skews the timing.
RunResult RunWorkload(const Workload& w, bool callbackBus)
{
    HostMachine host;
    gHost = &host;
    if (callbackBus)
    {
        Z80CpuAttachMemory(host.cpu, nullptr);
        Z80CpuSetMemoryBus(host.cpu, CallbackMemRead, nullptr, CallbackMemWrite, nullptr);
    }
    host.LoadCode(0x8000, w.code, w.len);

    uint64_t totalT = 0, totalInstr = 0;
    const Clock::time_point start = Clock::now();

    for (int r = 0; r < w.repeats; ++r)
    {
        Z80CpuReset(host.cpu);
        Z80CpuSetReg(host.cpu, Z80CpuRegPc, 0x8000);
        Z80CpuSetReg(host.cpu, Z80CpuRegSp, 0xFFF0);
        // Raw Z80CpuStep - StepCapture() adds a GetReg + soft-ROM probe per
        // step (test-harness features) that would pollute the measurement.
        while (!Z80CpuHalted(host.cpu))
        {
            Z80CpuStep(host.cpu);
            ++totalInstr;
        }
        totalT += Z80CpuTstates(host.cpu);
    }

    RunResult result;
    result.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    result.tstates = totalT;
    result.instructions = totalInstr;
    return result;
}
}  // namespace

// Best-of-N run of one workload/mode; sums T-states across repeats by
// measuring per-repeat counters.
static RunResult MeasureBest(const Workload& w, bool callbackBus, int tries)
{
    RunResult best{};
    best.seconds = 1e30;
    for (int i = 0; i < tries; ++i)
    {
        RunResult r = RunWorkload(w, callbackBus);
        if (r.seconds < best.seconds)
            best = r;
    }
    return best;
}

int main()
{
    printf("PoC 017 Z80 benchmark - %s\n\n", Z80CpuVersion());
    printf("%-8s %-10s %12s %14s %14s\n", "workload", "bus", "T-states", "T-states/s", "instr/s");

    double flatTotal = 0.0, cbTotal = 0.0;

    for (const Workload& w : kWorkloads)
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            const bool cb = mode == 1;
            const RunResult r = MeasureBest(w, cb, 3);
            const double tps = static_cast<double>(r.tstates) / r.seconds;
            const double ips = static_cast<double>(r.instructions) / r.seconds;
            printf("%-8s %-10s %12llu %13.2fM %13.2fM\n", w.name, cb ? "callback" : "flat",
                   static_cast<unsigned long long>(r.tstates), tps / 1e6, ips / 1e6);
            if (cb)
                cbTotal += tps;
            else
                flatTotal += tps;
        }
    }

    // Raw stepping overhead: halted CPU, each Z80CpuStep burns 4 T with no
    // opcode dispatch - isolates the API-loop cost from emulation work.
    {
        HostMachine host;
        host.Reset();
        const uint8_t halt = 0x76;
        host.LoadCode(0x8000, &halt, 1);
        Z80CpuSetReg(host.cpu, Z80CpuRegPc, 0x8000);
        host.StepCapture();  // execute HALT once

        const int kSteps = 20000000;
        Z80CPU* cpu = host.cpu;
        const Clock::time_point start = Clock::now();
        for (int i = 0; i < kSteps; ++i)
            Z80CpuStep(cpu);
        const double sec = std::chrono::duration<double>(Clock::now() - start).count();
        printf("\nhalt-step microbench: %.1f ns per Z80CpuStep (%.0fM halted steps/s)\n",
               sec * 1e9 / kSteps, kSteps / sec / 1e6);
    }

    printf("\naggregate: flat %.2fM T/s, callback %.2fM T/s (flat is %.2fx)\n", flatTotal / 4e6,
           cbTotal / 4e6, flatTotal / cbTotal);
    printf("3.5 MHz real-time needs only 3.5M T/s\n");
    return 0;
}
