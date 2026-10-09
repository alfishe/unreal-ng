// The unreal-next-z80 library's CPU core (core/src/3rdparty/unreal-next-z80/z80ncpu.h): the FUSE vectors with the
// NMOS expectations (the baseline of the fork), the two NMOS/CMOS switches, the bus-cycle kinds of the read
// callback, the external /WAIT from a callback and the halted M1. The Z80N instructions have their own file
// (z80nopcodes_test.cpp).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "_helpers/fusevectors.h"
#include "z80ntestbus.h"

namespace
{
class Z80nCpuFixture
{
public:
    Z80nCpuFixture() : cpu(Z80nCpuCreate()), bus(cpu) {}
    ~Z80nCpuFixture() { Z80nCpuDestroy(cpu); }

    /// Run instructions from `pc` until `t` T-states have passed (or `maxSteps`)
    uint32_t RunFor(uint16_t pc, uint32_t t, int maxSteps = 64)
    {
        Z80nCpuSetReg(cpu, Z80nCpuRegPc, pc);
        const uint32_t t0 = Z80nCpuTstates(cpu);
        for (int i = 0; i < maxSteps && Z80nCpuTstates(cpu) - t0 < t; i++)
            Z80nCpuStep(cpu);
        return Z80nCpuTstates(cpu) - t0;
    }

    Z80nCPU* cpu;
    Z80nTest::TestBus bus;
};
}  // namespace

// The FUSE vectors (testdata/z80/fuse, every documented and undocumented opcode): registers,
// memory, T-states and the bus trace (memory and port events at their T, internal cycles), as
// the native core passes them (FusePhase_Test): the NMOS baseline.
// ~1 300 short cases on a flat bus: a few ms
TEST(Z80nCpu_Test, FuseVectorsNmos)
{
    auto cases = FuseVectors::LoadCases();
    ASSERT_GT(cases.size(), 1000u) << "FUSE tests.in not found or truncated";
    const std::set<std::string>& skipAF = FuseVectors::SkipAFCases();
    // Repeating block I/O: MEMPTR differs from the vectors (see below), inherited from unreal-z80 0.5.0
    const std::set<std::string> repeatIoMemptr = {"edb2_1", "edb3_1", "edba_1", "edbb_1"};

    int passed = 0;
    int failed = 0;
    std::string report;
    for (auto& [name, tc] : cases)
    {
        if (tc.expTotal == 0)
            continue;

        Z80nCpuFixture f;
        f.bus.TraceInternal();
        for (const auto& block : tc.memory)
            f.bus.Load(block.first, block.second);
        Z80nCpuRegisters regs{};
        regs.af = tc.regs[0];
        regs.bc = tc.regs[1];
        regs.de = tc.regs[2];
        regs.hl = tc.regs[3];
        regs.afAlt = tc.regs[4];
        regs.bcAlt = tc.regs[5];
        regs.deAlt = tc.regs[6];
        regs.hlAlt = tc.regs[7];
        regs.ix = tc.regs[8];
        regs.iy = tc.regs[9];
        regs.sp = tc.regs[10];
        regs.pc = tc.regs[11];
        regs.memptr = tc.regs[12];
        regs.i = tc.i;
        regs.r = tc.r;
        regs.iff1 = tc.iff1;
        regs.iff2 = tc.iff2;
        regs.im = tc.im;
        regs.halted = tc.halted ? 1 : 0;
        Z80nCpuSetRegisters(f.cpu, &regs);
        Z80nCpuSetTstates(f.cpu, 0);

        int steps = 0;
        while (Z80nCpuTstates(f.cpu) < tc.expTotal && steps++ < 64)
            Z80nCpuStep(f.cpu);

        std::vector<std::string> issues;
        if (Z80nCpuTstates(f.cpu) != tc.expTotal)
            issues.push_back("total " + std::to_string(Z80nCpuTstates(f.cpu)) + " != " + std::to_string(tc.expTotal));

        // The trace in the FUSE helper's terms: memory data events at the T of the access, port events at IORQ
        std::vector<FuseVectors::BusEvent> trace;
        for (const Z80nTest::Event& e : f.bus.events)
        {
            const char type = (e.type == 'M' || e.type == 'P') ? 'R' : e.type;
            trace.push_back({type, e.addr, e.value, e.t});
        }
        FuseVectors::CompareTrace(trace, tc, issues);
        FuseVectors::CompareIdle(trace, tc, issues);

        Z80nCpuGetRegisters(f.cpu, &regs);
        const uint16_t got[13] = {regs.af,    regs.bc, regs.de, regs.hl, regs.afAlt, regs.bcAlt, regs.deAlt,
                                  regs.hlAlt, regs.ix, regs.iy, regs.sp, regs.pc,    regs.memptr};
        for (int i = 0; i < 13; i++)
        {
            if (i == 0 && skipAF.count(name))
                continue;  // the documented block-flag divergence (FusePhase_Test)
            if (i == 12 && repeatIoMemptr.count(name))
            {
                // unreal-z80's MEMPTR after a repeating INIR / INDR / OTIR / OTDR is PC + 1 (z80test
                // z80memptr, real silicon); the FUSE vectors (and the native core) use the older BC formula
                if (got[i] != static_cast<uint16_t>(tc.expRegs[11] + 1))
                    issues.push_back("MEMPTR " + std::to_string(got[i]) + " != PC + 1");
                continue;
            }
            if (got[i] != tc.expRegs[i])
                issues.push_back("reg " + std::to_string(i) + " " + std::to_string(got[i]) + " != " + std::to_string(tc.expRegs[i]));
        }
        if (regs.r != tc.expR || regs.i != tc.expI || regs.iff1 != tc.expIff1 || regs.iff2 != tc.expIff2 ||
            regs.im != tc.expIm || (regs.halted != 0) != tc.expHalted)
            issues.push_back("I/R/IFF/IM/HALT");
        for (const auto& block : tc.expMemory)
        {
            uint16_t addr = block.first;
            for (uint8_t expected : block.second)
            {
                if (f.bus.memory[addr] != expected)
                    issues.push_back("mem " + std::to_string(addr));
                addr++;
            }
        }

        if (issues.empty())
        {
            passed++;
        }
        else
        {
            failed++;
            if (report.size() < 6000)
            {
                report += "\n[" + name + "] ";
                for (size_t i = 0; i < issues.size() && i < 4; i++)
                    report += issues[i] + "; ";
            }
        }
    }
    EXPECT_EQ(failed, 0) << passed << " passed, " << failed << " failed" << report;
}

// NMOS baseline: OUT (C),0 writes 0; a host can switch to a CMOS part's #FF
TEST(Z80nCpu_Test, OutC0WritesZeroByDefault)
{
    Z80nCpuFixture f;
    f.bus.Load(0x8000, {0xED, 0x71, 0xED, 0x71});  // OUT (C),0 x 2
    Z80nCpuSetReg(f.cpu, Z80nCpuRegBc, 0x00FE);
    Z80nCpuSetReg(f.cpu, Z80nCpuRegPc, 0x8000);
    Z80nCpuStep(f.cpu);
    Z80nCpuSetOutC0Value(f.cpu, 0xFF);
    Z80nCpuStep(f.cpu);

    std::vector<uint8_t> written;
    for (const Z80nTest::Event& e : f.bus.events)
        if (e.type == 'O')
            written.push_back(e.value);
    EXPECT_EQ(written, (std::vector<uint8_t>{0x00, 0xFF}));
}

// NMOS: LD A,I / LD A,R followed at once by an accepted INT clears P/V (the acknowledge clears IFF2 before the
// copy settles); Z80nCpuSetLdAirQuirk(0) keeps it as a CMOS part does
TEST(Z80nCpu_Test, LdAIThenIntParityFollowsTheQuirkSwitch)
{
    for (int quirk : {1, 0})
        for (uint8_t second : {0x57, 0x5F})  // LD A,I / LD A,R
        {
            Z80nCpuFixture f;
            Z80nCpuSetLdAirQuirk(f.cpu, quirk);
            f.bus.Load(0x8000, {0xED, second});
            Z80nCpuSetReg(f.cpu, Z80nCpuRegPc, 0x8000);
            Z80nCpuSetReg(f.cpu, Z80nCpuRegSp, 0xC000);
            Z80nCpuSetReg(f.cpu, Z80nCpuRegIff1, 1);
            Z80nCpuSetReg(f.cpu, Z80nCpuRegIff2, 1);
            Z80nCpuSetReg(f.cpu, Z80nCpuRegIm, 1);
            Z80nCpuStep(f.cpu);
            ASSERT_EQ(Z80nCpuGetReg(f.cpu, Z80nCpuRegAf) & 0x04, 0x04) << "P/V = IFF2 after the instruction";
            EXPECT_EQ(Z80nCpuGetReg(f.cpu, Z80nCpuRegBoundary), Z80nCpuBoundaryLdAIr);

            ASSERT_EQ(Z80nCpuInt(f.cpu), 13) << "IM1 acknowledge";
            EXPECT_EQ(Z80nCpuGetReg(f.cpu, Z80nCpuRegAf) & 0x04, quirk ? 0x00 : 0x04)
                << "ED " << std::hex << +second << ", quirk " << quirk;
            EXPECT_EQ(Z80nCpuGetReg(f.cpu, Z80nCpuRegPc), 0x0038);
        }
}

// The read callback tells an opcode fetch from an operand fetch and a data read:
// LD A,(#9000) = M1 + two operands + a read; DD CB d op reads its operation byte as an operand
TEST(Z80nCpu_Test, ReadCallbackGetsTheCycleKind)
{
    Z80nCpuFixture f;
    f.bus.Load(0x8000, {0x3A, 0x00, 0x90,         // LD A,(#9000)
                        0xDD, 0xCB, 0x02, 0x46});  // BIT 0,(IX+2)
    Z80nCpuSetReg(f.cpu, Z80nCpuRegIx, 0x9000);
    f.RunFor(0x8000, 13 + 20);

    std::string kinds;
    for (const Z80nTest::Event& e : f.bus.events)
        kinds.push_back(e.type);
    EXPECT_EQ(kinds, "MPPR" "MMPPR");
}

// The board's /WAIT from inside a callback stretches the instruction: LD A,(#9000) is 4 memory cycles
TEST(Z80nCpu_Test, ExternalWaitFromTheCallback)
{
    Z80nCpuFixture f;
    f.bus.Load(0x8000, {0x3A, 0x00, 0x90});
    f.bus.externalWaitPerMemoryCycle = 2;
    EXPECT_EQ(f.RunFor(0x8000, 1, 1), 13u + 4 * 2);

    // Each access sees its own cycle's T, after the waits of the cycles before it
    ASSERT_EQ(f.bus.events.size(), 4u);
    EXPECT_EQ(f.bus.events[1].t - f.bus.events[0].t, 2u + 1u + 3u) << "the M1's wait, its refresh T, the operand's 3 T";
    EXPECT_EQ(f.bus.events[2].t - f.bus.events[1].t, 2u + 3u);
}

// A halted CPU runs M1 cycles at the byte after the HALT through the read callback, 4 T each, R ticking; PC stays
// on the HALT
TEST(Z80nCpu_Test, HaltedCpuFetchesTheByteAfterTheHalt)
{
    Z80nCpuFixture f;
    f.bus.Load(0x8000, {0x76});  // HALT
    f.RunFor(0x8000, 1, 1);
    ASSERT_TRUE(Z80nCpuHalted(f.cpu));
    EXPECT_EQ(Z80nCpuGetReg(f.cpu, Z80nCpuRegPc), 0x8000) << "PC stays at the HALT";
    f.bus.events.clear();
    const uint16_t r0 = Z80nCpuGetReg(f.cpu, Z80nCpuRegR);
    for (int i = 0; i < 3; i++)
        EXPECT_EQ(Z80nCpuStep(f.cpu), 4);
    ASSERT_EQ(f.bus.events.size(), 3u);
    for (const Z80nTest::Event& e : f.bus.events)
    {
        EXPECT_EQ(e.type, 'M');
        EXPECT_EQ(e.addr, 0x8001);
    }
    EXPECT_EQ((Z80nCpuGetReg(f.cpu, Z80nCpuRegR) - r0) & 0x7F, 3);
}

// Zero copy: the core executes on a host block with the Z80nCpuRegisterFile layout
TEST(Z80nCpu_Test, AttachedRegisterFileIsTheCpuState)
{
    Z80nCpuFixture f;
    Z80nCpuRegisterFile file{};
    file.pc = 0x8000;
    file.af = 0x0100;
    Z80nCpuAttachRegisterFile(f.cpu, &file);
    f.bus.Load(0x8000, {0x3C, 0x3C});  // INC A x 2
    Z80nCpuStep(f.cpu);
    Z80nCpuStep(f.cpu);
    EXPECT_EQ(file.af >> 8, 0x03);
    EXPECT_EQ(file.pc, 0x8002);
    file.pc = 0x8000;  // the host writes, the core follows
    Z80nCpuStep(f.cpu);
    EXPECT_EQ(file.af >> 8, 0x04);
    Z80nCpuAttachRegisterFile(f.cpu, nullptr);
    EXPECT_EQ(Z80nCpuGetReg(f.cpu, Z80nCpuRegPc), 0x8001) << "detach keeps the state";
}

TEST(Z80nCpu_Test, VersionNamesTheFork)
{
    EXPECT_STREQ(Z80nCpuVersion(), "0.5.0-z80n.1");
}
