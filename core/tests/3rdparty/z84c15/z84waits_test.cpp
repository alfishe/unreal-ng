// The Z84C15's wait-state generator (core/src/3rdparty/z84c15/z84waits.cpp): the programmed waits
// per bus cycle kind, the MWBR range, the power-on window, the INTA and RETI extensions, and the
// external /WAIT adding up with them (PS0182 p. 318-320; research-cpu-z84c15.md section 4.1;
// docs/inprogress/2026-10-01-z84c15-cpu-library/design.md section 6).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <3rdparty/z84c15/z84c15.h>

#include <vector>

#include "z84testbus.h"

namespace
{
class Z84Waits_Test : public ::testing::Test
{
protected:
    Z84Lib::Z84C15 _chip;
    Z84Test::TestBus _bus{_chip.Cpu()};
    Z84CPU* _cpu = _chip.Cpu();

    void SetUp() override
    {
        _chip.PowerOn();
        Z84CpuReset(_cpu);
        Z84CpuSetReg(_cpu, Z84CpuRegSp, 0xC000);
    }

    /// Program a system control register the way software does (OUT (#EE),n : OUT (#EF),v)
    void SetSystem(uint8_t pointer, uint8_t value)
    {
        _chip.Write(0xEE, pointer);
        _chip.Write(0xEF, value);
    }

    /// T-states of one instruction at `pc`
    int Step(uint16_t pc)
    {
        Z84CpuSetReg(_cpu, Z84CpuRegPc, pc);
        return Z84CpuStep(_cpu);
    }
};
}  // namespace

// Worked example 1: WCR acts as #FF for the first 15 M1 cycles after power-on - a NOP is
// 4 + 3 memory waits + 1 M1 extension = 8 T - and reads #FF meanwhile; then the waits stop
TEST_F(Z84Waits_Test, PowerOnWindowIsFifteenM1Cycles)
{
    _chip.Write(0xEE, 0x00);
    EXPECT_EQ(_chip.Read(0xEF), 0xFF) << "WCR reads #FF in the window";

    Z84CpuSetReg(_cpu, Z84CpuRegPc, 0x0000);  // NOPs (memory is zero)
    std::vector<int> t;
    for (int i = 0; i < 20; i++)
        t.push_back(Z84CpuStep(_cpu));
    for (int i = 0; i < 15; i++)
        EXPECT_EQ(t[i], 8) << "NOP " << i + 1;
    for (int i = 15; i < 20; i++)
        EXPECT_EQ(t[i], 4) << "NOP " << i + 1;
    EXPECT_EQ(_chip.Read(0xEF), 0x00) << "after the window: the reset value";
}

// Software that writes WCR in the window ends it at once (the loader does, at its 8th M1)
TEST_F(Z84Waits_Test, WcrWriteEndsThePowerOnWindow)
{
    EXPECT_EQ(Step(0x0000), 8);
    SetSystem(0x00, 0x04);
    EXPECT_EQ(_chip.Read(0xEF), 0x04);
    EXPECT_EQ(Step(0x0000), 5) << "NOP with one memory wait in its M1";
}

// Worked example 2: WCR = #04, MWBR = #F0: one wait in every memory cycle, M1s included.
// LD (DE),A = 9 T; the PLD loader's stream loop (bios304-pc-loader.asm:139-158) 113 -> 142 T
// per bitstream byte (29 memory cycles)
TEST_F(Z84Waits_Test, OneMemoryWaitStretchesTheLoaderLoop)
{
    SetSystem(0x00, 0x04);
    _bus.Load(0x8000, {0x12});  // LD (DE),A
    Z84CpuSetReg(_cpu, Z84CpuRegDe, 0xFE00);
    EXPECT_EQ(Step(0x8000), 9);

    // loop: LD A,(HL) ; 8 x LD (DE),A with 7 RRCA between ; INC E ; INC HL ; JR loop
    std::vector<uint8_t> loop = {0x7E};
    for (int i = 0; i < 8; i++)
    {
        loop.push_back(0x12);
        if (i < 7)
            loop.push_back(0x0F);
    }
    loop.insert(loop.end(), {0x1C, 0x23, 0x18, static_cast<uint8_t>(-static_cast<int>(loop.size()) - 4)});
    _bus.Load(0x0100, loop);
    Z84CpuSetReg(_cpu, Z84CpuRegHl, 0x4000);

    auto oneByte = [&] {
        Z84CpuSetReg(_cpu, Z84CpuRegPc, 0x0100);
        int t = 0;
        do
            t += Z84CpuStep(_cpu);
        while (Z84CpuGetReg(_cpu, Z84CpuRegPc) != 0x0100);
        return t;
    };
    EXPECT_EQ(oneByte(), 142);
    SetSystem(0x00, 0x00);
    EXPECT_EQ(oneByte(), 113) << "MAME's number: it stores WCR and never applies it";
}

// Memory waits only inside MWBR[7:4] >= A15-A12 >= MWBR[3:0]; the M1 extension (WCR bit 4)
// everywhere
TEST_F(Z84Waits_Test, MwbrRangeAndM1Extension)
{
    SetSystem(0x00, 0x08);  // 2 memory waits
    SetSystem(0x01, 0x84);  // #4000-#8FFF
    _bus.Load(0x8000, {0x3A, 0x00, 0x90});  // LD A,(#9000): M1 + 2 operands in range, the read outside
    EXPECT_EQ(Step(0x8000), 13 + 3 * 2);
    _bus.Load(0x9000, {0x3A, 0x00, 0x50});  // all four cycles outside / inside: the read at #5000 waits
    EXPECT_EQ(Step(0x9000), 13 + 2);

    SetSystem(0x00, 0x10);  // M1 extension only
    EXPECT_EQ(Step(0x9000), 13 + 1);
    _bus.Load(0xD000, {0xDD, 0x23});  // INC IX: two M1s
    EXPECT_EQ(Step(0xD000), 10 + 2);
}

// I/O waits 0/2/4/6 (WCR bits 1-0), never on the on-chip ports: OUT (#19),A keeps 11 T,
// OUT (#FE),A gets 6 more
TEST_F(Z84Waits_Test, IoWaitsSkipTheOnChipPorts)
{
    SetSystem(0x00, 0x03);
    _bus.Load(0x8000, {0xD3, 0x19, 0xD3, 0xFE});
    EXPECT_EQ(Step(0x8000), 11);
    EXPECT_EQ(Step(0x8002), 11 + 6);
    SetSystem(0x00, 0x01);
    EXPECT_EQ(Step(0x8002), 11 + 2);
}

// INTA: 0/2/4/6 daisy-chain waits (WCR bits 7-6) + 1 vector wait (bit 5), in every mode; the pushes
// and the IM2 vector reads take the memory waits
TEST_F(Z84Waits_Test, InterruptAcknowledgeWaits)
{
    SetSystem(0x00, 0xE0);
    Z84CpuSetReg(_cpu, Z84CpuRegIff1, 1);
    Z84CpuSetReg(_cpu, Z84CpuRegIm, 1);
    EXPECT_EQ(Z84CpuInt(_cpu), 13 + 6 + 1);

    Z84CpuSetReg(_cpu, Z84CpuRegIff1, 1);
    Z84CpuSetReg(_cpu, Z84CpuRegIm, 2);
    EXPECT_EQ(Z84CpuInt(_cpu), 19 + 6 + 1);

    SetSystem(0x00, 0x04);  // memory waits only: 2 pushes + 2 vector reads
    Z84CpuSetReg(_cpu, Z84CpuRegIff1, 1);
    EXPECT_EQ(Z84CpuInt(_cpu), 19 + 4);
    Z84CpuSetReg(_cpu, Z84CpuRegIff1, 1);
    Z84CpuSetReg(_cpu, Z84CpuRegIm, 1);
    EXPECT_EQ(Z84CpuInt(_cpu), 13 + 2);
}

// RETI extension: ED 4D gets 0/0/2/4 waits by WCR bits 7-6; another byte after ED gets 1 with
// bits 7-6 = 2 or 3
TEST_F(Z84Waits_Test, RetiExtension)
{
    _bus.Load(0x8000, {0xED, 0x4D, 0xED, 0x44});  // RETI ; NEG
    Z84CpuSetReg(_cpu, Z84CpuRegSp, 0xC000);
    _bus.Load(0xC000, {0x02, 0x80});  // RETI returns to #8002

    const struct
    {
        uint8_t wcr;
        int reti;
        int neg;
    } rows[] = {{0x00, 14, 8}, {0x40, 14, 8}, {0x80, 14 + 2, 8 + 1}, {0xC0, 14 + 4, 8 + 1}};
    for (const auto& row : rows)
    {
        SetSystem(0x00, row.wcr);
        Z84CpuSetReg(_cpu, Z84CpuRegSp, 0xC000);
        EXPECT_EQ(Step(0x8000), row.reti) << "WCR " << std::hex << +row.wcr;
        EXPECT_EQ(Step(0x8002), row.neg) << "WCR " << std::hex << +row.wcr;
    }
}

// The board's /WAIT (from the callback) adds to the programmed waits: LD A,(#9000) with one
// programmed and two external waits per memory cycle
TEST_F(Z84Waits_Test, ExternalWaitAddsToProgrammedWaits)
{
    SetSystem(0x00, 0x04);
    _bus.Load(0x8000, {0x3A, 0x00, 0x90});
    _bus.externalWaitPerMemoryCycle = 2;
    EXPECT_EQ(Step(0x8000), 13 + 4 * (1 + 2));

    // The callback sees its cycle start at T - 3: the waits of a cycle follow its access
    const uint32_t m1 = _bus.events[0].t;
    EXPECT_EQ(_bus.events[1].t - m1, 2u + 1u + 1u + 3u) << "external, programmed, the refresh T, then the operand";
}

// A standalone core (no chip powered on) has no programmed waits
TEST(Z84WaitsCore_Test, CoreAloneHasNoWaits)
{
    Z84CPU* cpu = Z84CpuCreate();
    Z84Test::TestBus bus(cpu);
    Z84CpuSetReg(cpu, Z84CpuRegPc, 0x0000);
    EXPECT_EQ(Z84CpuStep(cpu), 4);
    Z84CpuDestroy(cpu);
}
