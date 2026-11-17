// unreal-xtensa (core/src/3rdparty/unreal-xtensa): the ESP32-S3 user-mode interpreter running toolchain-assembled
// programs (programs/programs.S, programs/bench.c, newlib's setjmp/longjmp; regenerate xtensaprograms.h with
// programs/genprograms.py). Expected values are computed here independently of the interpreter.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include <3rdparty/unreal-xtensa/xtensa-cpu.h>
#include <3rdparty/unreal-xtensa/xtensa-isa.h>

#include "xtensaprograms.h"

namespace XtensaBenchNative
{
#include "programs/bench.c"
}

namespace
{
using namespace XtensaPrograms;
using xtensa::Cpu;
using xtensa::RunResult;
using xtensa::StopReason;

constexpr uint32_t kDram = 0x3FC80000;
constexpr uint32_t kDramSize = 0x20000;
constexpr uint32_t kIram = 0x40380000;
constexpr uint32_t kIramSize = 0x10000;
constexpr uint32_t kStackTop = kDram + 0x1F000;
constexpr uint32_t kBuf = kDram + 0x1000;
constexpr uint32_t kTrapBase = 0x40000000;
constexpr uint32_t kTrapSize = 0x1000;
constexpr uint32_t kLeafValue = 0x5A5A;

constexpr uint32_t X = 0x87654321u;
constexpr uint32_t Y = 0x0000F00Du;

uint32_t P(uint32_t i, uint32_t n)
{
    return (i << 12) | n;
}

/// Sum of the pattern registers a2..a(last) of a chain frame with argument n.
uint32_t PatternSum(uint32_t n, uint32_t last)
{
    uint32_t s = n;
    for (uint32_t i = 3; i <= last; ++i)
        s += P(i, n);
    return s;
}

class XtensaCpu_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        dram.assign(kDramSize, 0);
        iram.assign(kIramSize, 0);
        std::memcpy(iram.data(), kCode, sizeof(kCode));
        ASSERT_TRUE(cpu.AddRegion(kIram, kIramSize, iram.data(), xtensa::RegionAll));
        ASSERT_TRUE(cpu.AddRegion(kDram, kDramSize, dram.data(), xtensa::RegionRead | xtensa::RegionWrite));
        cpu.SetTrapRange(kTrapBase, kTrapSize);
    }

    uint32_t Mem32(uint32_t addr)
    {
        uint32_t v = 0;
        EXPECT_TRUE(cpu.Read32(addr, v)) << std::hex << addr;
        return v;
    }

    /// Handles a trap of the test programs; false for an unknown one.
    bool HandleTrap()
    {
        const uint32_t pc = cpu.Pc();
        if (pc == kTrapAdd2)
        {
            cpu.ReturnFromTrap(cpu.Arg(0) + cpu.Arg(1));
            return true;
        }
        if (pc == kTrapAdd8)
        {
            uint32_t s = 0;
            for (int i = 0; i < 8; ++i)
                s += cpu.Arg(i);
            cpu.ReturnFromTrap(s);
            return true;
        }
        if (pc == kTrapCb)
        {
            const uint32_t fn = cpu.Arg(0);
            const uint32_t x = cpu.Arg(1);
            const uint32_t r = Call(fn, {x}, cpu.Ar(1) - 256);
            cpu.ReturnFromTrap(r + 1000);
            return true;
        }
        if (pc == kTrapLeaf)
        {
            if (leafHook)
                leafHook();
            cpu.ReturnFromTrap(kLeafValue);
            return true;
        }
        return false;
    }

    /// Runs until the pending host call returns, serving traps. Returns the last RunResult.
    RunResult RunToReturn(uint64_t step = 50000000)
    {
        for (;;)
        {
            const RunResult r = cpu.Run(step);
            totalExecuted += r.executed;
            if (r.reason == StopReason::Trap && cpu.AtCallReturn())
                return r;
            if (r.reason == StopReason::Trap && HandleTrap())
                continue;
            if (r.reason == StopReason::Budget && step < 1000)
                continue; // single stepping
            return r;
        }
    }

    uint32_t Call(uint32_t fn, std::initializer_list<uint32_t> args, uint32_t sp = kStackTop, uint64_t step = 50000000)
    {
        const std::vector<uint32_t> a(args);
        EXPECT_TRUE(cpu.BeginCall(fn, a.data(), static_cast<int>(a.size()), sp));
        const RunResult r = RunToReturn(step);
        EXPECT_EQ(r.reason, StopReason::Trap) << xtensa::StopReasonName(r.reason) << " at 0x" << std::hex << r.pc
                                              << " fault 0x" << r.faultAddress;
        return cpu.EndCall();
    }

    /// Calls a function expected to stop with `reason`; returns the RunResult.
    RunResult CallStop(uint32_t fn, std::initializer_list<uint32_t> args)
    {
        const std::vector<uint32_t> a(args);
        EXPECT_TRUE(cpu.BeginCall(fn, a.data(), static_cast<int>(a.size()), kStackTop));
        return cpu.Run(100000);
    }

    std::vector<uint32_t> Words(uint32_t addr, uint32_t count)
    {
        std::vector<uint32_t> w(count);
        for (uint32_t i = 0; i < count; ++i)
            w[i] = Mem32(addr + 4 * i);
        return w;
    }

    std::vector<uint8_t> dram;
    std::vector<uint8_t> iram;
    Cpu cpu;
    uint64_t totalExecuted = 0;
    std::function<void()> leafHook;
};

// ---------------------------------------------------------------------------------------------------------------------

TEST_F(XtensaCpu_Test, AluShiftBooleanLoadStore)
{
    const int32_t sx = static_cast<int32_t>(X);
    const int32_t sy = static_cast<int32_t>(Y);
    const std::vector<uint32_t> expected = {
        X + Y, X + Y, (X << 1) + Y, (X << 2) + Y, (X << 3) + Y,                     // 0
        X - Y, (X << 1) - Y, (X << 2) - Y, (X << 3) - Y, X & Y,                     // 5
        X | Y, X ^ Y, 0u - X, 0u - X, Y,                                            // 10
        X - 128, X - 1, X + 15, X - 32768, X + 32512,                               // 15
        0xFFFFF800u, 2047, 0xFFFFFFE0u, 95, Y,                                      // 20
        1, 0, X, Y, Y,                                                              // 25
        X, 5, X, X, 5,                                                              // 30
        0x21, 0x4321, 0xFFFFF00Du, 0x0D, 0xFFFFFF80u,                               // 35
        127, 5, 0, 15, 31,                                                          // 40
        16, 32, 0, 0x4321u * 0xF00Du,                                               // 45
        static_cast<uint32_t>(0x4321 * (0xF00D - 0x10000)), // MUL16S: 0xF00D is negative
        X * Y,                                                                      // 50
        static_cast<uint32_t>((static_cast<uint64_t>(X) * Y) >> 32),
        static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(sx) * -16) >> 32),
        X / Y, static_cast<uint32_t>(sx / sy), X % Y,                               // 53
        static_cast<uint32_t>(sx % sy), 0x80000000u, 0,                             // 56
        X << 1, X << 31, static_cast<uint32_t>(sx >> 4), 0xFFFFFFFFu, X >> 4,       // 59
        X >> 15,                                                                    // 64
        static_cast<uint32_t>(((static_cast<uint64_t>(X) << 32) | Y) >> 5),         // 65
        X >> 5, static_cast<uint32_t>(sx >> 5), X << 5, X >> 8, X << 8,             // 66
        static_cast<uint32_t>(((static_cast<uint64_t>(X) << 32) | Y) >> 31),        // 71
        31, (X >> 4) & 0xFF, 8, 0x4321, 0x8765,                                     // 72
        Y, X,                                                                       // 77
        0xB9A5, 0xFF01, X, 5, X,                                                    // 79 booleans
        0x01, 0x80, 0x80FF, 0xFFFF80FFu, 0x7F01, 0x80FF7F01u,                       // 84 loads
        0x43210021u, X, 0x65432100u, 0x6543, 0x80FF7F01u, Y, X, Y, 0xDEADBEEFu,     // 90
    };
    const uint32_t bytes = Call(kAlu, {kBuf});
    ASSERT_EQ(bytes, expected.size() * 4);
    const std::vector<uint32_t> got = Words(kBuf, static_cast<uint32_t>(expected.size()));
    for (size_t i = 0; i < expected.size(); ++i)
        EXPECT_EQ(got[i], expected[i]) << "result " << i;
}

TEST_F(XtensaCpu_Test, Branches)
{
    const std::vector<uint32_t> expected = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0,
                                            1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 1};
    const uint32_t bytes = Call(kBranches, {kBuf});
    ASSERT_EQ(bytes, expected.size() * 4);
    const std::vector<uint32_t> got = Words(kBuf, static_cast<uint32_t>(expected.size()));
    for (size_t i = 0; i < expected.size(); ++i)
        EXPECT_EQ(got[i], expected[i]) << "branch " << i;
}

TEST_F(XtensaCpu_Test, ZeroOverheadLoops)
{
    const std::vector<uint32_t> expected = {5, 0, 0, 5, 1, 3, 1, 10, 210, (10u << 8) | 4, 0, 9, 4};
    const uint32_t bytes = Call(kLoops, {kBuf});
    ASSERT_EQ(bytes, expected.size() * 4);
    const std::vector<uint32_t> got = Words(kBuf, static_cast<uint32_t>(expected.size()));
    for (size_t i = 0; i < expected.size(); ++i)
        EXPECT_EQ(got[i], expected[i]) << "loop case " << i;
}

TEST_F(XtensaCpu_Test, LoopsSingleStepped)
{
    // Run(1) splits every block: the loop-back must still happen exactly once per iteration
    const std::vector<uint32_t> expected = {5, 0, 0, 5, 1, 3, 1, 10, 210, (10u << 8) | 4, 0, 9, 4};
    const uint32_t bytes = Call(kLoops, {kBuf}, kStackTop, 1);
    ASSERT_EQ(bytes, expected.size() * 4);
    EXPECT_EQ(Words(kBuf, static_cast<uint32_t>(expected.size())), expected);
}

TEST_F(XtensaCpu_Test, Fpu)
{
    const std::vector<uint32_t> expected = {
        0x40400000, 0x7FC00000, 0x7FC00000, 0x7FC00000, 0x00000000,             // 0
        0x80000000, 0x80000000, 0x00000000, 0xFFA00001, 0x7F800000,             // 5
        0x7FA00001, 0x3A000400, 0xBA000400, 0x3A000400, 0x3A000000,             // 10 fused vs unfused
        2, 4, 0xFFFFFFFE, 2, 0xFFFFFFFF,                                        // 15 round / trunc
        0x7FFFFFFF, 0x80000000, 0x7FFFFFFF, 0x7FFFFFFF, 0x80000000,             // 20 saturation
        0xFFFFFFFE, 0xFFFFFFFF, 2, 3000000000u, 0xFFFFFFFF,                     // 25 floor / ceil / utrunc
        0xFFFFFFFB, 0xFFFFFFFF, 0xFFFFFFFF, 0xBFC00000, 0x4F800000,             // 30
        0x4F000000, 0x4B800000, 24, 0x5578, 0xE6,                               // 35
        0x40000000, 0x3F800000, 0x40000000, 0x3F800000, 0x40000000,             // 40 conditional moves
        0x3F800000, 0x40000000, 0x40000000, 0x3F800000, 4,                      // 45 loads
        0x40000000, 8, 16, 0x7F800000, 0xFF800000,                              // 50
        0x3F800000, 3,                                                          // 55
    };
    const uint32_t bytes = Call(kFpu, {kBuf});
    ASSERT_EQ(bytes, expected.size() * 4);
    const std::vector<uint32_t> got = Words(kBuf, static_cast<uint32_t>(expected.size()));
    for (size_t i = 0; i < expected.size(); ++i)
        EXPECT_EQ(got[i], expected[i]) << "fpu result " << i;
}

// ---- register windows ----

TEST_F(XtensaCpu_Test, WindowChainsOverflowAndUnderflowAllRegisters)
{
    struct Case
    {
        uint32_t fn;
        uint32_t depth;
        uint32_t last;
    };
    for (const Case c : {Case{kChain4, 40, 3}, Case{kChain8, 30, 7}, Case{kChain12, 20, 11}})
    {
        uint32_t expected = kLeafValue;
        for (uint32_t n = 1; n <= c.depth; ++n)
            expected += PatternSum(n, c.last);
        EXPECT_EQ(Call(c.fn, {c.depth}), expected) << std::hex << c.fn;
        EXPECT_EQ(cpu.State().hostDepth, 0u);
    }
}

TEST_F(XtensaCpu_Test, WindowSpillBytesMatchEspIdfHandlers)
{
    constexpr uint32_t depth = 25;
    auto callInc = [](uint32_t n) -> uint32_t { return n == 0 ? 2 : (n % 3 == 0 ? 3 : (n % 3 == 1 ? 1 : 2)); };
    bool checked = false;
    leafHook = [&] {
        ASSERT_TRUE(cpu.SpillAllWindows());
        // Frame k has n = depth - k and sp_k = host sp - 64 (k + 1); the trap frame was entered at sp_depth - 16
        auto sp = [&](int k) { return k < 0 ? kStackTop : kStackTop - 64u * static_cast<uint32_t>(k + 1); };
        for (int k = 0; k <= static_cast<int>(depth); ++k)
        {
            const uint32_t n = depth - static_cast<uint32_t>(k);
            const uint32_t calleeSp = k == static_cast<int>(depth) ? sp(k) - 16 : sp(k + 1);
            const uint32_t callerInc = k == 0 ? 1 : callInc(n + 1);
            EXPECT_EQ(Mem32(calleeSp - 16) >> 30, callerInc) << "a0 of frame " << k;
            EXPECT_EQ(Mem32(calleeSp - 12), sp(k)) << "a1 of frame " << k;
            EXPECT_EQ(Mem32(calleeSp - 8), n) << "a2 of frame " << k;
            EXPECT_EQ(Mem32(calleeSp - 4), P(3, n)) << "a3 of frame " << k;
            const uint32_t inc = callInc(n);
            if (inc >= 2)
            {
                const uint32_t words = inc == 2 ? 4 : 8;
                const uint32_t base = sp(k - 1) - 16 - 4 * words;
                for (uint32_t i = 0; i < words; ++i)
                    EXPECT_EQ(Mem32(base + 4 * i), P(4 + i, n)) << "a" << (4 + i) << " of frame " << k;
            }
        }
        // the host frame's base save area: its a1 below frame 0's sp
        EXPECT_EQ(Mem32(sp(0) - 12), kStackTop);
        EXPECT_EQ(cpu.State().windowStart, 1u << cpu.State().windowBase);
        checked = true;
    };
    uint32_t expected = kLeafValue + PatternSum(0, 7);
    for (uint32_t n = 1; n <= depth; ++n)
        expected += PatternSum(n, callInc(n) == 1 ? 3 : (callInc(n) == 2 ? 7 : 11));
    EXPECT_EQ(Call(kMix, {depth}), expected);
    EXPECT_TRUE(checked);
}

TEST_F(XtensaCpu_Test, WindowsSingleSteppedMatchBlocks)
{
    uint32_t expected = kLeafValue + PatternSum(0, 7);
    auto callInc = [](uint32_t n) -> uint32_t { return n % 3 == 0 ? 11 : (n % 3 == 1 ? 3 : 7); };
    for (uint32_t n = 1; n <= 12; ++n)
        expected += PatternSum(n, callInc(n));
    EXPECT_EQ(Call(kMix, {12}, kStackTop, 1), expected);
    EXPECT_EQ(Call(kMix, {12}, kStackTop, 7), expected);
}

TEST_F(XtensaCpu_Test, MovspAllocaUnderflowsSpilledCaller)
{
    uint32_t chain = kLeafValue;
    for (uint32_t n = 1; n <= 8; ++n)
        chain += PatternSum(n, 11);
    EXPECT_EQ(Call(kAllocatest, {8}), chain + 0x77 + PatternSum(8, 7));
}

TEST_F(XtensaCpu_Test, NewlibSetjmpLongjmp)
{
    // The exact esp32s3 newlib code: setjmp's SYSCALL 0 spills the windows, longjmp rewrites WINDOWSTART, RSYNC, and
    // RETW underflows the setjmp caller from the copied save areas
    EXPECT_EQ(Call(kSjtest, {kBuf}), 42u + 0x44 + 0x55 + 0x66 + 0x77);
    EXPECT_EQ(cpu.State().windowStart, 1u << cpu.State().windowBase);
}

// ---- traps and host calls ----

TEST_F(XtensaCpu_Test, TrapArgumentsForAllCallKinds)
{
    // CALLX8 with 8 arguments (two on the stack) + CALLX4 + CALLX12 + CALLX0
    EXPECT_EQ(Call(kTrapargs, {}), 36u + 30 + 300 + 3000);
}

TEST_F(XtensaCpu_Test, TrapStopsBeforeExecuting)
{
    std::vector<uint32_t> a = {10, 20};
    ASSERT_TRUE(cpu.BeginCall(kTrapAdd2, a.data(), 2, kStackTop));
    const RunResult r = cpu.Run(1000);
    EXPECT_EQ(r.reason, StopReason::Trap);
    EXPECT_EQ(r.pc, kTrapAdd2);
    EXPECT_EQ(r.executed, 0u);
    EXPECT_EQ(cpu.Arg(0), 10u);
    EXPECT_EQ(cpu.Arg(1), 20u);
    cpu.ReturnFromTrap(30);
    EXPECT_EQ(cpu.Run(1000).reason, StopReason::Trap);
    EXPECT_TRUE(cpu.AtCallReturn());
    EXPECT_EQ(cpu.EndCall(), 30u);
}

TEST_F(XtensaCpu_Test, HostCallsNestThroughTraps)
{
    // cbtest(3): TRAP_CB -> BeginCall(cbfn) -> TRAP_CB -> ... four host calls deep, plus TRAP_ADD2 at the bottom
    EXPECT_EQ(Call(kCbtest, {3}), 5017u);
    EXPECT_EQ(cpu.State().hostDepth, 0u);
    EXPECT_EQ(Call(kAdd3, {1, 2, 3}), 6u);
    EXPECT_EQ(Call(kSum8, {1, 2, 3, 4, 5, 6, 7, 8}), 36u);
}

TEST_F(XtensaCpu_Test, BeginCallDepthIsBounded)
{
    std::vector<uint32_t> a = {0, 0, 0};
    for (int i = 0; i < xtensa::kMaxHostFrames; ++i)
        EXPECT_TRUE(cpu.BeginCall(kAdd3, a.data(), 3, kStackTop - 1024u * static_cast<uint32_t>(i)));
    EXPECT_FALSE(cpu.BeginCall(kAdd3, a.data(), 3, kStackTop - 0x8000));
}

// ---- memory ----

TEST_F(XtensaCpu_Test, S32c1i)
{
    ASSERT_TRUE(cpu.Write32(kBuf, 100));
    EXPECT_EQ(Call(kAtomic, {kBuf, 100, 7}), 100u);
    EXPECT_EQ(Mem32(kBuf), 7u);
    EXPECT_EQ(Call(kAtomic, {kBuf, 5, 9}), 7u);
    EXPECT_EQ(Mem32(kBuf), 7u);
    const RunResult r = CallStop(kAtomic, {kBuf + 2, 7, 9});
    EXPECT_EQ(r.reason, StopReason::MemoryFault);
    EXPECT_EQ(r.faultAddress, kBuf + 2);
}

TEST_F(XtensaCpu_Test, MemoryFaults)
{
    // unmapped load: stops at the load, the destination keeps its value
    RunResult r = CallStop(kLoad32, {0x50000000});
    EXPECT_EQ(r.reason, StopReason::MemoryFault);
    EXPECT_EQ(r.faultAddress, 0x50000000u);
    EXPECT_EQ(r.pc, kLoad32 + 6);
    EXPECT_EQ(cpu.Ar(3), 0x123u);

    // write to a read-only page
    std::vector<uint8_t> rom(4096, 0xEE);
    ASSERT_TRUE(cpu.AddRegion(0x3FF00000, 4096, rom.data(), xtensa::RegionRead));
    cpu.Reset();
    r = CallStop(kStore32, {0x3FF00010, 1});
    EXPECT_EQ(r.reason, StopReason::MemoryFault);
    EXPECT_EQ(r.faultAddress, 0x3FF00010u);
    EXPECT_EQ(rom[0x10], 0xEE);

    // fetch from a page without Exec
    cpu.Reset();
    r = CallStop(kJumpto, {kDram + 0x100});
    EXPECT_EQ(r.reason, StopReason::MemoryFault);
    EXPECT_EQ(r.pc, kDram + 0x100);
    EXPECT_EQ(r.faultAddress, kDram + 0x100);

    // a word access crossing into an unmapped page faults without a partial write
    cpu.Reset();
    r = CallStop(kStore32, {kDram + kDramSize - 2, 0x11223344});
    EXPECT_EQ(r.reason, StopReason::MemoryFault);
    EXPECT_EQ(dram[kDramSize - 2], 0);
    EXPECT_EQ(dram[kDramSize - 1], 0);
}

TEST_F(XtensaCpu_Test, PsramAliasesShareOneBuffer)
{
    // The same buffer on the D-bus (0x3C000000, data) and the I-bus (0x42000000, code + byte data)
    std::vector<uint8_t> psram(0x10000, 0);
    ASSERT_TRUE(cpu.AddRegion(0x3C000000, 0x10000, psram.data(), xtensa::RegionRead | xtensa::RegionWrite));
    ASSERT_TRUE(cpu.AddRegion(0x42000000, 0x10000, psram.data(), xtensa::RegionAll));
    std::memcpy(&psram[0x100], &kCode[kAdd3 - kCodeBase], 12);
    EXPECT_EQ(Call(0x42000100, {1, 2, 3}), 6u);
    // guest byte store through the D-bus alias turns the first ADD into SUB: the I-bus copy is re-decoded
    EXPECT_EQ(Call(kStorebyte, {0x3C000105, 0xC0}), 0xC0u);
    EXPECT_EQ(Call(0x42000100, {1, 2, 3}), 2u);
    // byte data on the I-bus alias
    EXPECT_EQ(Call(kStorebyte, {0x42000800, 0xAB}), 0xABu);
    EXPECT_EQ(psram[0x800], 0xAB);
    uint8_t b = 0;
    EXPECT_TRUE(cpu.Read8(0x3C000800, b));
    EXPECT_EQ(b, 0xAB);
}

TEST_F(XtensaCpu_Test, SelfModifyingCodeIsRedecoded)
{
    EXPECT_EQ(Call(kSelfmod, {}), 0x0102u);
    // host write into decoded code
    EXPECT_EQ(Call(kIncrement, {5}), 6u);
    ASSERT_TRUE(cpu.Write8(kIncrement + 5, 10)); // ADDI imm8
    EXPECT_EQ(Call(kIncrement, {5}), 15u);
}

// ---- stops, counters, state ----

TEST_F(XtensaCpu_Test, StopReasons)
{
    struct Case
    {
        uint32_t fn;
        StopReason reason;
        uint32_t offset; ///< of the stopping instruction from the function start
    };
    for (const Case c : {Case{kSyscallnz, StopReason::Syscall, 6}, Case{kBrk, StopReason::Break, 3},
                         Case{kIllegal, StopReason::IllegalInstruction, 3}, Case{kPrivileged, StopReason::Privileged, 3},
                         Case{kDivzero, StopReason::DivideByZero, 6}, Case{kEeinsn, StopReason::Unimplemented, 3}})
    {
        cpu.Reset();
        const RunResult r = CallStop(c.fn, {7});
        EXPECT_EQ(r.reason, c.reason) << std::hex << c.fn << " " << xtensa::StopReasonName(r.reason);
        EXPECT_EQ(r.pc, c.fn + c.offset) << std::hex << c.fn;
    }
}

TEST_F(XtensaCpu_Test, CcountCountsInstructions)
{
    cpu.State().ccount = 12345;
    EXPECT_EQ(Call(kCcount, {}), (1u << 16) | 11u);
    // CCOUNT after the WSR: 1000000 + the WSR itself + the instructions after it (RSR, SUB, SLLI, OR, RETW)
    EXPECT_EQ(cpu.State().ccount, 1000000u + 6);

    cpu.State().ccount = 0;
    totalExecuted = 0;
    const uint32_t r = Call(kLongloop, {100});
    (void)r;
    EXPECT_EQ(cpu.State().ccount, totalExecuted);
    EXPECT_EQ(totalExecuted, 3u + 100 * 4 + 2); // ENTRY, 2 MOVI, body, ADD, RETW
}

TEST_F(XtensaCpu_Test, BudgetAndSnapshot)
{
    uint32_t a3 = 0, a4 = 0;
    for (int i = 0; i < 1000; ++i)
    {
        a3 += 3;
        a4 ^= a3;
    }
    std::vector<uint32_t> arg = {1000};
    ASSERT_TRUE(cpu.BeginCall(kLongloop, arg.data(), 1, kStackTop));
    RunResult r = cpu.Run(1001);
    EXPECT_EQ(r.reason, StopReason::Budget);
    EXPECT_EQ(r.executed, 1001u);
    xtensa::CpuState snap;
    cpu.GetState(snap);
    r = cpu.Run(100000);
    ASSERT_EQ(r.reason, StopReason::Trap);
    const uint64_t cycles = cpu.State().ccount;
    EXPECT_EQ(cpu.EndCall(), a3 + a4);

    cpu.SetState(snap);
    r = cpu.Run(100000);
    ASSERT_EQ(r.reason, StopReason::Trap);
    EXPECT_EQ(cpu.State().ccount, cycles);
    EXPECT_EQ(cpu.EndCall(), a3 + a4);
}

TEST_F(XtensaCpu_Test, CompiledCodeMatchesNative)
{
    // GCC -O2 output (zero-overhead loops, CALL8 recursion, MUL / DIV, literals) against the same C compiled natively
    std::vector<unsigned char> work(1024);
    const uint32_t rounds = 2;
    const uint32_t expected = XtensaBenchNative::BenchMain(work.data(), rounds);
    EXPECT_EQ(Call(kBenchMain, {kBuf, rounds}), expected);
}

// ---- decoder ----

TEST(XtensaIsa_Test, DecodeAndDisassemble)
{
    struct Case
    {
        std::vector<uint8_t> bytes;
        const char* text;
    };
    const Case cases[] = {
        {{0x36, 0x41, 0x00}, "entry\ta1, 32"},
        {{0x1d, 0xf0}, "retw.n"},
        {{0x22, 0xa0, 0x01}, "movi\ta2, 1"},
        {{0x30, 0x22, 0x80}, "add\ta2, a2, a3"},
        {{0x00, 0x50, 0x00}, "syscall"},
        {{0x50, 0x48, 0x03}, "rsr.windowbase\ta5"},
        {{0x40, 0x49, 0x13}, "wsr.windowstart\ta4"},
        {{0x10, 0x20, 0x00}, "rsync"},
    };
    for (const Case& c : cases)
    {
        xtensa::Insn in;
        ASSERT_TRUE(xtensa::Decode(0x40000000, c.bytes.data(), c.bytes.size(), in));
        EXPECT_EQ(static_cast<size_t>(in.len), c.bytes.size());
        EXPECT_EQ(xtensa::Disassemble(in), c.text);
    }
    EXPECT_EQ(xtensa::InsnLength(0x0E), 4);
    EXPECT_EQ(xtensa::InsnLength(0x08), 2);
    EXPECT_EQ(xtensa::InsnLength(0x07), 3);
}

} // namespace
