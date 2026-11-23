// POC 053 (tools/poc/053-xtensa-jit): the region JIT against the interpreter on random code. Never lands as is.
// Two CPUs with the same memory run the same random program (registers pointing into a data region, float registers
// with NaNs, infinities and out-of-range values) with the same budgets; one interprets, the other compiles a region
// at every block's first entry. Run result, the whole CPU state and all memory must be the same after every run.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include <3rdparty/unreal-xtensa/xtensa-cpu.h>
#include <3rdparty/unreal-xtensa/xtensa-isa.h>

#if UNREAL_XTENSA_JIT

namespace xtensa
{
inline uint32_t ToBitsForTest(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
} // namespace xtensa

namespace
{
using xtensa::Cpu;
using xtensa::Insn;
using xtensa::Op;

constexpr uint32_t kCode = 0x40380000;
constexpr uint32_t kCodeSize = 0x1000;
constexpr uint32_t kData = 0x3FC80000;
constexpr uint32_t kDataSize = 0x2000; // two pages: crossings at the boundary and at the end

struct Rng
{
    uint64_t s;
    uint32_t Next()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<uint32_t>(s >> 11);
    }
};

bool Wanted(const Insn& in, uint32_t pc)
{
    switch (in.op)
    {
        case Op::Illegal: case Op::Ill: case Op::IllN: case Op::EeInsn:
            return false;
        // the windowed / looping / stopping kinds are rare: they end the experiment early
        case Op::Entry: case Op::Retw: case Op::RetwN: case Op::Movsp: case Op::Rotw: case Op::Wsr: case Op::Xsr: case Op::Rsr: case Op::Syscall:
        case Op::Simcall: case Op::Break: case Op::BreakN: case Op::Waiti: case Op::Rsil: case Op::Rfe:
        case Op::Rfue: case Op::Rfde: case Op::Rfwo: case Op::Rfwu: case Op::Rfi: case Op::Rer: case Op::Wer:
        case Op::Mac16:
            return false;
        default:
            break;
    }
    // branch / call targets inside the code
    switch (in.op)
    {
        case Op::J: case Op::Call0: case Op::Call4: case Op::Call8: case Op::Call12: case Op::L32r:
            return static_cast<uint32_t>(in.imm) - kCode < kCodeSize - 4;
        case Op::Loop: case Op::Loopnez: case Op::Loopgtz: // short loops, ending inside the code
            return static_cast<uint32_t>(in.imm) - kCode < kCodeSize - 4 && static_cast<uint32_t>(in.imm) - pc < 64;
        default:
            if (in.flags & xtensa::InsnEndsBlock && in.imm != 0 && in.op != Op::Jx && in.op != Op::Callx0 &&
                in.op != Op::Callx4 && in.op != Op::Callx8 && in.op != Op::Callx12 && in.op != Op::Ret &&
                in.op != Op::RetN)
                return static_cast<uint32_t>(in.imm) - kCode < kCodeSize - 4;
            return true;
    }
}

std::vector<uint8_t> RandomCode(Rng& rng)
{
    std::vector<uint8_t> code(kCodeSize, 0);
    uint32_t at = 0;
    while (at + 4 <= kCodeSize)
    {
        uint8_t b[4];
        Insn in;
        for (int tries = 0;; ++tries)
        {
            const uint32_t w = rng.Next();
            b[0] = static_cast<uint8_t>(w);
            b[1] = static_cast<uint8_t>(w >> 8);
            b[2] = static_cast<uint8_t>(w >> 16);
            b[3] = 0;
            if (!xtensa::Decode(kCode + at, b, 4, in) || in.len == 4)
                continue;
            if (Wanted(in, kCode + at) || tries > 200)
                break;
        }
        std::memcpy(&code[at], b, in.len);
        at += in.len;
    }
    return code;
}

struct Machine
{
    Cpu cpu;
    std::vector<uint8_t> code;
    std::vector<uint8_t> data;

    Machine(const std::vector<uint8_t>& c, const std::vector<uint8_t>& d, bool jit) : code(c), data(d)
    {
        cpu.SetJit(jit, 1);
        cpu.AddRegion(kCode, kCodeSize, code.data(), xtensa::RegionAll);
        cpu.AddRegion(kData, kDataSize, data.data(), xtensa::RegionRead | xtensa::RegionWrite);
        cpu.SetTrapRange(0x40000000, 0x1000);
    }
};

} // namespace

TEST(XtensaJit_Test, RandomProgramsMatchTheInterpreter)
{
    uint64_t compared = 0, executed = 0;
    uint64_t jitCalls = 0, jitInsns = 0;
    for (uint64_t seed = 1; seed <= 600; ++seed)
    {
        Rng rng{seed * 0x9E3779B97F4A7C15ull};
        const std::vector<uint8_t> code = RandomCode(rng);
        std::vector<uint8_t> data(kDataSize);
        for (uint8_t& b : data)
            b = static_cast<uint8_t>(rng.Next());
        Machine a(code, data, false);
        Machine b(code, data, true);
        xtensa::CpuState st = a.cpu.State();
        for (uint32_t i = 0; i < 64; ++i)
        {
            const uint32_t r = rng.Next();
            // mostly addresses in the data region (near the page boundary and the end too), else anything
            st.ar[i] = (r & 3) ? kData + (rng.Next() % (kDataSize + 64)) - 32 : rng.Next();
        }
        static const uint32_t kSpecial[] = {0x7FC00000u, 0x7F800001u, 0xFFC00001u, 0x7F800000u, 0xFF800000u,
                                            0x80000000u, 0x00000001u, 0x4F000000u, 0xCF000000u, 0x4F800000u,
                                            0x3F000000u, 0x3FC00000u, 0xBFC00000u, 0x40200000u};
        for (uint32_t i = 0; i < 16; ++i)
        {
            const uint32_t r = rng.Next() % 4;
            st.fr[i] = r == 0 ? kSpecial[rng.Next() % (sizeof(kSpecial) / 4)]
                     : r == 1 ? rng.Next()
                              : xtensa::ToBitsForTest(static_cast<float>(static_cast<int32_t>(rng.Next() % 2000000) - 1000000) / 64.0f);
        }
        st.br = rng.Next() & 0xFFFF;
        if (rng.Next() & 1)
        {
            // a live loop: its end inside the code (other blocks run over it) and a small count
            st.lbeg = kCode + 2 * (rng.Next() % 256);
            st.lend = st.lbeg + 1 + rng.Next() % 48;
            st.lcount = rng.Next() % 6;
        }
        st.sar = rng.Next() & 63;
        st.pc = kCode + 2 * (rng.Next() % 64);
        a.cpu.SetState(st);
        b.cpu.SetState(st);
        // several runs with small budgets: blocks get cut, re-entered and compiled
        for (int run = 0; run < 40; ++run)
        {
            const uint64_t budget = 1 + rng.Next() % 300;
            const xtensa::RunResult ra = a.cpu.Run(budget);
            const xtensa::RunResult rb = b.cpu.Run(budget);
            ASSERT_EQ(static_cast<int>(ra.reason), static_cast<int>(rb.reason)) << "seed " << seed << " run " << run;
            ASSERT_EQ(ra.pc, rb.pc) << "seed " << seed << " run " << run;
            ASSERT_EQ(ra.executed, rb.executed) << "seed " << seed << " run " << run;
            ASSERT_EQ(ra.faultAddress, rb.faultAddress) << "seed " << seed << " run " << run;
            ASSERT_EQ(0, std::memcmp(&a.cpu.State(), &b.cpu.State(), sizeof(xtensa::CpuState)))
                << "seed " << seed << " run " << run;
            ASSERT_EQ(a.data, b.data) << "seed " << seed << " run " << run;
            ASSERT_EQ(a.code, b.code) << "seed " << seed << " run " << run;
            ++compared;
            executed += ra.executed;
            if (ra.reason != xtensa::StopReason::Budget)
            {
                // a stop: move on from a fresh pc so the program keeps running
                xtensa::CpuState s2 = a.cpu.State();
                s2.pc = kCode + 2 * (rng.Next() % 64);
                a.cpu.SetState(s2);
                b.cpu.SetState(s2);
            }
        }
        jitCalls += b.cpu.GetJitStats().calls;
        jitInsns += b.cpu.GetJitStats().insns;
    }
    EXPECT_GT(jitCalls, 1000u);
    RecordProperty("compared", static_cast<int>(compared));
    std::printf("compared %llu runs, %llu instructions, %llu compiled-block calls running %llu of them\n",
                static_cast<unsigned long long>(compared), static_cast<unsigned long long>(executed),
                static_cast<unsigned long long>(jitCalls), static_cast<unsigned long long>(jitInsns));
    EXPECT_GT(jitInsns, executed / 4);
}

// The kinds whose edge cases random code rarely reaches: zero divisors (the region exits before the instruction and the
// interpreter stops with DivideByZero), INT_MIN / -1, NSA / NSAU of 0 and -1, MOVT.S / MOVF.S on both boolean values
TEST(XtensaJit_Test, DivisionsAndBitCountsMatchTheInterpreter)
{
    auto rrr = [](uint32_t op2, uint32_t op1, uint32_t r, uint32_t s, uint32_t t) {
        return (op2 << 20) | (op1 << 16) | (r << 12) | (s << 8) | (t << 4);
    };
    struct Expect
    {
        uint32_t word;
        Op op;
    };
    const Expect insns[] = {
        {rrr(0xC, 2, 2, 3, 4), Op::Quou},  {rrr(0xD, 2, 5, 3, 4), Op::Quos},  {rrr(0xE, 2, 6, 3, 4), Op::Remu},
        {rrr(0xF, 2, 7, 3, 4), Op::Rems},  {rrr(4, 0, 0xE, 3, 8), Op::Nsa},   {rrr(4, 0, 0xF, 3, 9), Op::Nsau},
        {rrr(4, 0, 0xE, 4, 10), Op::Nsa},  {rrr(4, 0, 0xF, 4, 11), Op::Nsau}, {rrr(0xD, 0xB, 0, 1, 0), Op::MovtS},
        {rrr(0xC, 0xB, 2, 3, 0), Op::MovfS}, {rrr(0xD, 2, 3, 3, 3), Op::Quos}, {rrr(0xF, 2, 4, 4, 3), Op::Rems},
    };
    std::vector<uint8_t> code(kCodeSize, 0);
    uint32_t at = 0;
    for (const Expect& e : insns)
    {
        uint8_t b[4] = {static_cast<uint8_t>(e.word), static_cast<uint8_t>(e.word >> 8),
                        static_cast<uint8_t>(e.word >> 16), 0};
        Insn in;
        ASSERT_TRUE(xtensa::Decode(kCode + at, b, 4, in));
        ASSERT_EQ(static_cast<int>(e.op), static_cast<int>(in.op)) << xtensa::Mnemonic(in.op);
        std::memcpy(&code[at], b, 3);
        at += 3;
    }
    const uint8_t loop[] = {0x06, 0xFF, 0xFF}; // j . (the block ends; the next run starts the program again)
    std::memcpy(&code[at], loop, 3);

    const uint32_t kValues[] = {0u, 1u, 2u, 7u, 0xFFFFFFFFu, 0xFFFFFFFEu, 0xFFFFFFF9u, 0x80000000u, 0x7FFFFFFFu,
                                0x80000001u, 0x00010000u, 0x12345678u, 0xCAFEBABEu};
    const std::vector<uint8_t> data(kDataSize, 0);
    uint64_t jitInsns = 0;
    for (uint32_t i = 0; i < sizeof(kValues) / 4; ++i)
        for (uint32_t j = 0; j < sizeof(kValues) / 4; ++j)
        {
            Machine a(code, data, false);
            Machine b(code, data, true);
            xtensa::CpuState st = a.cpu.State();
            st.ar[3] = kValues[i];
            st.ar[4] = kValues[j];
            st.fr[0] = 0x11111111u;
            st.fr[1] = 0x7FC00001u;
            st.fr[2] = 0x22222222u;
            st.fr[3] = 0xFF800000u;
            st.br = (i + j) & 1;
            st.pc = kCode;
            a.cpu.SetState(st);
            b.cpu.SetState(st);
            for (int run = 0; run < 3; ++run)
            {
                const xtensa::RunResult ra = a.cpu.Run(13);
                const xtensa::RunResult rb = b.cpu.Run(13);
                ASSERT_EQ(static_cast<int>(ra.reason), static_cast<int>(rb.reason)) << i << " " << j;
                ASSERT_EQ(ra.pc, rb.pc) << i << " " << j;
                ASSERT_EQ(ra.executed, rb.executed) << i << " " << j;
                ASSERT_EQ(0, std::memcmp(&a.cpu.State(), &b.cpu.State(), sizeof(xtensa::CpuState))) << i << " " << j;
                if (ra.reason != xtensa::StopReason::Budget)
                {
                    EXPECT_EQ(xtensa::StopReason::DivideByZero, ra.reason);
                    break;
                }
            }
            jitInsns += b.cpu.GetJitStats().insns;
        }
    EXPECT_GT(jitInsns, 1000u); // the region ran them, not the interpreter
}

#endif // UNREAL_XTENSA_JIT
