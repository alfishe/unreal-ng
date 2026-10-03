// MCS-48 core: results, flags and machine cycles from the Intel MCS-48
// user's manual (instruction set chapter), the interrupt, timer and port
// behavior described there, and MAME mcs48.cpp for DA A

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "emulator/cpu/mcs48/mcs48.h"

using mcs48::Mcs48;

namespace
{
constexpr uint8_t CY = Mcs48::kPswCy, AC = Mcs48::kPswAc;

/// Runs `code` from address 0
struct Rig
{
    Mcs48 cpu;
    std::vector<std::pair<uint8_t, uint8_t>> movxWrites;
    std::vector<std::pair<int, uint8_t>> portWrites;

    explicit Rig(std::vector<uint8_t> code, uint16_t ram = 64) : cpu(ram)
    {
        code.resize(4096, 0x00);
        cpu.SetProgram(std::move(code));
        Mcs48::Bus bus;
        bus.movxRead = [](uint8_t a) { return static_cast<uint8_t>(a ^ 0x5A); };
        bus.movxWrite = [this](uint8_t a, uint8_t d) { movxWrites.emplace_back(a, d); };
        bus.portOut = [this](int port, uint8_t latch) { portWrites.emplace_back(port, latch); };
        cpu.SetBus(bus);
        cpu.Reset();
        portWrites.clear();
    }

    /// Runs n instructions; returns the machine cycles they took
    uint64_t Step(int n = 1)
    {
        const uint64_t start = cpu.Clock();
        for (int k = 0; k < n; ++k)
            cpu.Step();
        return (cpu.Clock() - start) / Mcs48::kClocksPerCycle;
    }
    uint8_t Flags() const { return static_cast<uint8_t>(cpu.Psw() & (CY | AC)); }
};

/// A program with `bytes` placed at `address` (the rest NOP)
std::vector<uint8_t> At(uint16_t address, std::vector<uint8_t> bytes, std::vector<uint8_t> base = {})
{
    base.resize(4096, 0x00);
    for (size_t n = 0; n < bytes.size(); ++n)
        base[address + n] = bytes[n];
    return base;
}
}  // namespace

TEST(Mcs48_Test, ResetValues)
{
    Rig r({0x00});
    EXPECT_EQ(r.cpu.Pc(), 0);
    EXPECT_EQ(r.cpu.Psw(), 0x08) << "bit 3 reads 1, SP 0, bank 0";
    EXPECT_EQ(r.cpu.Latch(1), 0xFF);
    EXPECT_EQ(r.cpu.Latch(2), 0xFF);
    EXPECT_FALSE(r.cpu.InterruptsEnabled());
    EXPECT_EQ(r.Step(), 1u) << "NOP: one machine cycle";
    EXPECT_EQ(r.cpu.Clock(), 15u) << "15 oscillator clocks per machine cycle";
}

TEST(Mcs48_Test, MachineCycles)
{
    // One cycle: register / accumulator operations; two: every two-byte instruction, I/O, MOVX, MOVP, RET
    struct Row
    {
        std::vector<uint8_t> code;
        uint64_t cycles;
        const char* name;
    };
    const Row rows[] = {
        {{0x68}, 1, "ADD A,R0"},       {{0x03, 0x01}, 2, "ADD A,#"},   {{0x09}, 2, "IN A,P1"},
        {{0x39}, 2, "OUTL P1,A"},      {{0x80}, 2, "MOVX A,@R0"},      {{0x90}, 2, "MOVX @R0,A"},
        {{0xA3}, 2, "MOVP A,@A"},      {{0xE3}, 2, "MOVP3 A,@A"},      {{0x04, 0x10}, 2, "JMP"},
        {{0x14, 0x10}, 2, "CALL"},     {{0xE8, 0x00}, 2, "DJNZ"},      {{0x57}, 1, "DA A"},
        {{0x47}, 1, "SWAP A"},         {{0x0C}, 2, "MOVD A,P4"},       {{0x08}, 2, "INS A,BUS"},
        {{0x02}, 2, "OUTL BUS,A"},     {{0x99, 0xFF}, 2, "ANL P1,#"},  {{0x42}, 1, "MOV A,T"},
        {{0x01}, 1, "undefined 01"},
    };
    for (const Row& row : rows)
    {
        Rig r(row.code);
        EXPECT_EQ(r.Step(), row.cycles) << row.name;
    }
}

TEST(Mcs48_Test, AddSetsCarryAndAuxCarry)
{
    // MOV A,#0F8h; ADD A,#09h -> 01, CY = 1, AC = 1 (8 + 9 > 15)
    Rig r({0x23, 0xF8, 0x03, 0x09});
    r.Step(2);
    EXPECT_EQ(r.cpu.A(), 0x01);
    EXPECT_EQ(r.Flags(), CY | AC);
    // MOV A,#10h; ADD A,#20h -> 30, no flags
    Rig n({0x23, 0x10, 0x03, 0x20});
    n.Step(2);
    EXPECT_EQ(n.cpu.A(), 0x30);
    EXPECT_EQ(n.Flags(), 0);
}

TEST(Mcs48_Test, AddcAddsTheCarry)
{
    // CLR C; CPL C; MOV A,#0FFh; ADDC A,#00h -> 00, CY = 1, AC = 1
    Rig r({0x97, 0xA7, 0x23, 0xFF, 0x13, 0x00});
    r.Step(4);
    EXPECT_EQ(r.cpu.A(), 0x00);
    EXPECT_EQ(r.Flags(), CY | AC);
}

TEST(Mcs48_Test, DecimalAdjust)
{
    // 0x38 + 0x45 = 0x7D -> DA = 0x83 (38 + 45 = 83)
    Rig r({0x23, 0x38, 0x03, 0x45, 0x57});
    r.Step(3);
    EXPECT_EQ(r.cpu.A(), 0x83);
    EXPECT_EQ(r.Flags() & CY, 0);
    // 0x99 + 0x01 = 0x9A -> DA = 0x00, CY = 1 (99 + 1 = 100)
    Rig c({0x23, 0x99, 0x03, 0x01, 0x57});
    c.Step(3);
    EXPECT_EQ(c.cpu.A(), 0x00);
    EXPECT_EQ(c.Flags() & CY, CY);
    // 0x09 + 0x09 = 0x12 with AC -> DA = 0x18
    Rig a({0x23, 0x09, 0x03, 0x09, 0x57});
    a.Step(3);
    EXPECT_EQ(a.cpu.A(), 0x18);
}

TEST(Mcs48_Test, RotatesThroughCarry)
{
    // MOV A,#81h; CLR C; RLC A -> 02, CY = 1; RRC A -> 81, CY = 0; RL A -> 03; RR A -> 81
    Rig r({0x23, 0x81, 0x97, 0xF7, 0x67, 0xE7, 0x77});
    r.Step(3);
    EXPECT_EQ(r.cpu.A(), 0x02);
    EXPECT_EQ(r.Flags() & CY, CY);
    r.Step();
    EXPECT_EQ(r.cpu.A(), 0x81);
    EXPECT_EQ(r.Flags() & CY, 0);
    r.Step();
    EXPECT_EQ(r.cpu.A(), 0x03);
    r.Step();
    EXPECT_EQ(r.cpu.A(), 0x81);
}

TEST(Mcs48_Test, RegisterBanksAndIndirectRam)
{
    // MOV R0,#30h; MOV A,#55h; MOV @R0,A; SEL RB1; MOV R0,#31h; MOV @R0,#66h; SEL RB0; MOV A,R0
    Rig r({0xB8, 0x30, 0x23, 0x55, 0xA0, 0xD5, 0xB8, 0x31, 0xB0, 0x66, 0xC5, 0xF8});
    r.Step(8);
    EXPECT_EQ(r.cpu.Ram(0x30), 0x55);
    EXPECT_EQ(r.cpu.Ram(0x31), 0x66);
    EXPECT_EQ(r.cpu.Ram(0), 0x30) << "bank 0 R0";
    EXPECT_EQ(r.cpu.Ram(24), 0x31) << "bank 1 R0 is RAM 24";
    EXPECT_EQ(r.cpu.A(), 0x30);
}

TEST(Mcs48_Test, IndirectAddressWrapsToTheRamSize)
{
    // MOV R0,#45h; MOV @R0,#77h: on a 64-byte part the cell is 05h
    Rig r({0xB8, 0x45, 0xB0, 0x77});
    r.Step(2);
    EXPECT_EQ(r.cpu.Ram(0x05), 0x77);
    Rig big({0xB8, 0x45, 0xB0, 0x77}, 128);
    big.Step(2);
    EXPECT_EQ(big.cpu.Ram(0x45), 0x77);
}

TEST(Mcs48_Test, ExchangeDigit)
{
    // MOV R1,#20h; MOV @R1,#0ABh; MOV A,#0CDh; XCHD A,@R1 -> A = CB, @R1 = AD
    Rig r({0xB9, 0x20, 0xB1, 0xAB, 0x23, 0xCD, 0x31});
    r.Step(4);
    EXPECT_EQ(r.cpu.A(), 0xCB);
    EXPECT_EQ(r.cpu.Ram(0x20), 0xAD);
}

TEST(Mcs48_Test, DjnzLoops)
{
    // MOV R2,#03h; loop: DJNZ R2,loop -> 3 DJNZ executions
    Rig r({0xBA, 0x03, 0xEA, 0x02, 0x00});
    r.Step(4);
    EXPECT_EQ(r.cpu.R(2), 0);
    EXPECT_EQ(r.cpu.Pc(), 0x04);
}

TEST(Mcs48_Test, CallAndRetKeepThePsw)
{
    // CALL 100h; at 100h: CLR C; CPL C; RET -> carry stays set (RET restores only the PC)
    Rig r(At(0x100, {0x97, 0xA7, 0x83}, {0x34, 0x00, 0x00}));
    EXPECT_EQ(r.Step(), 2u);
    EXPECT_EQ(r.cpu.Pc(), 0x100);
    EXPECT_EQ(r.cpu.Psw() & 0x07, 1) << "SP 1";
    EXPECT_EQ(r.cpu.Ram(8), 0x02) << "return address low";
    r.Step(3);
    EXPECT_EQ(r.cpu.Pc(), 0x02);
    EXPECT_EQ(r.cpu.Psw() & 0x07, 0);
    EXPECT_EQ(r.Flags() & CY, CY);
}

TEST(Mcs48_Test, RetrRestoresThePswAndEndsTheInterrupt)
{
    // EN I; then /INT low: the routine at 3 sets CY and selects bank 1, RETR restores both
    Rig r(At(0x003, {0xD5, 0x97, 0xA7, 0x93}, {0x05, 0x00, 0x00}));
    r.Step();   // EN I
    r.cpu.SetInt(false);
    EXPECT_EQ(r.Step(), 2u) << "interrupt entry: a CALL to 3";
    EXPECT_EQ(r.cpu.Pc(), 0x003);
    EXPECT_TRUE(r.cpu.InInterrupt());
    r.cpu.SetInt(true);
    r.Step(4);
    EXPECT_FALSE(r.cpu.InInterrupt());
    EXPECT_EQ(r.cpu.Pc(), 0x001);
    EXPECT_EQ(r.Flags() & CY, 0) << "the carry as before the interrupt";
    EXPECT_EQ(r.cpu.Psw() & Mcs48::kPswBs, 0) << "register bank as before";
}

TEST(Mcs48_Test, InterruptIsLevelSensitiveAndNotNested)
{
    // The routine waits on JNI while /INT is low (as the PROFI-XT firmware does), then returns
    Rig r(At(0x003, {0x86, 0x03, 0x93}, {0x05, 0x00, 0x00, 0x00}));
    r.Step();   // EN I
    r.cpu.SetInt(false);
    r.Step();   // entry
    r.Step(3);
    EXPECT_EQ(r.cpu.Pc(), 0x003) << "JNI loops while the pin is low, no nested entry";
    EXPECT_EQ(r.cpu.Psw() & 0x07, 1) << "one stack level";
    r.cpu.SetInt(true);
    r.Step(2);
    EXPECT_FALSE(r.cpu.InInterrupt());
    // DIS I: a low pin is ignored
    Rig d({0x15, 0x00, 0x00});
    d.Step();
    d.cpu.SetInt(false);
    d.Step();
    EXPECT_EQ(d.cpu.Pc(), 0x02);
}

TEST(Mcs48_Test, JumpsUseTheOperandsPage)
{
    // A conditional jump with its opcode at 0FFh takes its target in page 1 (the operand's page)
    std::vector<uint8_t> code = At(0x00FF, {0xC6, 0x40});   // JZ 40h
    code[0] = 0x04;                                          // JMP 0FFh
    code[1] = 0xFF;
    Rig r(code);
    r.Step();
    EXPECT_EQ(r.cpu.Pc(), 0x0FF);
    r.Step();
    EXPECT_EQ(r.cpu.Pc(), 0x140);
}

TEST(Mcs48_Test, MovpReadsTheCurrentPage)
{
    // At 2F0h: MOV A,#05h; MOVP A,@A -> the byte at 205h; MOVP3 A,@A -> the byte at 3xxh
    std::vector<uint8_t> code = At(0x2F0, {0x23, 0x05, 0xA3, 0xE3});
    code[0] = 0x44;   // JMP 2F0h
    code[1] = 0xF0;
    code[0x205] = 0x21;
    code[0x321] = 0x99;
    Rig r(code);
    r.Step(3);
    EXPECT_EQ(r.cpu.A(), 0x21);
    r.Step();
    EXPECT_EQ(r.cpu.A(), 0x99);
}

TEST(Mcs48_Test, JmppJumpsThroughATableInThePage)
{
    // MOV A,#10h; JMPP @A with the byte 40h at 010h -> PC 040h
    std::vector<uint8_t> code = At(0x010, {0x40}, {0x23, 0x10, 0xB3});
    Rig r(code);
    r.Step(2);
    EXPECT_EQ(r.cpu.Pc(), 0x040);
}

TEST(Mcs48_Test, MemoryBankSelectsBit11)
{
    // SEL MB1; JMP 010h -> 810h; the PC increment keeps bit 11; inside an interrupt routine JMP stays in bank 0
    Rig r({0xF5, 0x04, 0x10});
    r.Step(2);
    EXPECT_EQ(r.cpu.Pc(), 0x810);
    r.Step();
    EXPECT_EQ(r.cpu.Pc(), 0x811);
    EXPECT_TRUE(r.cpu.Mb());

    // An instruction at 7FFh: the PC wraps to 000h within the bank (bit 11 stays 0)
    std::vector<uint8_t> code = At(0x7FF, {0x00});
    code[0] = 0xE4;   // JMP 7FFh
    code[1] = 0xFF;
    Rig w(code);
    w.Step(2);
    EXPECT_EQ(w.cpu.Pc(), 0x000);

    // EN I; SEL MB1; /INT: the routine's JMP 020h goes to 020h, not 820h
    Rig i(At(0x003, {0x04, 0x20}, {0x05, 0xF5, 0x00}));
    i.Step(2);
    i.cpu.SetInt(false);
    i.Step();   // entry
    i.cpu.SetInt(true);
    i.Step();   // JMP 020h
    EXPECT_EQ(i.cpu.Pc(), 0x020);
}

TEST(Mcs48_Test, FetchPastTheImageReadsFf)
{
    // A 2 KB image: page 8 is not selected (A11 = /CS of the EPROM) and reads #FF (MOV A,R7)
    Mcs48 cpu;
    std::vector<uint8_t> image(2048, 0x00);
    image[0] = 0xF5;   // SEL MB1
    image[1] = 0x04;   // JMP 800h
    image[2] = 0x00;
    cpu.SetProgram(image);
    cpu.Reset();
    cpu.Step();
    cpu.Step();
    EXPECT_EQ(cpu.Pc(), 0x800);
    EXPECT_EQ(cpu.Code(0x800), 0xFF);
}

TEST(Mcs48_Test, FlagsAndTests)
{
    // CPL F0; JF0 -> taken; CPL F1; JF1 -> taken; JT0 with T0 low -> not taken; JNT1 with T1 low -> taken
    Rig r({0x95, 0xB6, 0x04, 0x00, 0xB5, 0x76, 0x08, 0x00, 0x36, 0x00, 0x46, 0x20});
    r.Step(2);
    EXPECT_EQ(r.cpu.Pc(), 0x04);
    r.Step(2);
    EXPECT_EQ(r.cpu.Pc(), 0x08);
    r.cpu.SetT0(false);
    r.cpu.SetT1(false);
    r.Step();
    EXPECT_EQ(r.cpu.Pc(), 0x0A) << "JT0 not taken";
    r.Step();
    EXPECT_EQ(r.cpu.Pc(), 0x20) << "JNT1 taken";
    // JBb: MOV A,#40h; JB6 -> taken
    Rig b({0x23, 0x40, 0xD2, 0x10});
    b.Step(2);
    EXPECT_EQ(b.cpu.Pc(), 0x10);
}

TEST(Mcs48_Test, PswMove)
{
    // MOV A,#0F3h; MOV PSW,A; MOV A,PSW -> F3 | 08 (bit 3 reads 1); bank 1 selected
    Rig r({0x23, 0xF3, 0xD7, 0xC7});
    r.Step(3);
    EXPECT_EQ(r.cpu.A(), 0xFB);
    EXPECT_EQ(r.cpu.Psw() & 0x07, 3);
    EXPECT_NE(r.cpu.Psw() & Mcs48::kPswBs, 0);
}

TEST(Mcs48_Test, PortsAreQuasiBidirectional)
{
    // IN A,P1 reads latch AND pins; ANL P2,#7Fh / ORL P2,#80h change the latch and report it
    Rig r({0x09, 0x99, 0x0F, 0x09, 0x9A, 0x7F, 0x8A, 0x80});
    r.cpu.SetPins(1, 0xA5);
    r.Step();
    EXPECT_EQ(r.cpu.A(), 0xA5);
    r.Step(2);
    EXPECT_EQ(r.cpu.A(), 0x05) << "latch 0F AND pins A5";
    r.Step(2);
    EXPECT_EQ(r.cpu.Latch(2), 0xFF);
    ASSERT_GE(r.portWrites.size(), 3u);
    EXPECT_EQ(r.portWrites[r.portWrites.size() - 2], std::make_pair(2, uint8_t{0x7F}));
    EXPECT_EQ(r.portWrites.back(), std::make_pair(2, uint8_t{0xFF}));
}

TEST(Mcs48_Test, MovxUsesTheRegisterAsAddress)
{
    // MOV R1,#7Fh; MOV A,#3Ch; MOVX @R1,A; MOVX A,@R1 (the rig reads address ^ 5A)
    Rig r({0xB9, 0x7F, 0x23, 0x3C, 0x91, 0x81});
    r.Step(4);
    ASSERT_EQ(r.movxWrites.size(), 1u);
    EXPECT_EQ(r.movxWrites[0], std::make_pair(uint8_t{0x7F}, uint8_t{0x3C}));
    EXPECT_EQ(r.cpu.A(), static_cast<uint8_t>(0x7F ^ 0x5A));
}

TEST(Mcs48_Test, TimerCountsEvery32Cycles)
{
    // MOV A,#0FEh; MOV T,A; STRT T; then NOPs: 2 timer ticks overflow it
    Rig r({0x23, 0xFE, 0x62, 0x55});
    r.Step(3);
    r.Step(31);
    EXPECT_EQ(r.cpu.Timer(), 0xFE);
    r.Step();
    EXPECT_EQ(r.cpu.Timer(), 0xFF);
    r.Step(32);
    EXPECT_EQ(r.cpu.Timer(), 0x00);
    EXPECT_TRUE(r.cpu.TimerFlag());
}

TEST(Mcs48_Test, TimerFlagIsClearedByJtf)
{
    // Overflow, then JTF jumps once and clears the flag
    std::vector<uint8_t> code = {0x23, 0xFF, 0x62, 0x55};
    code.resize(40, 0x00);
    code.push_back(0x16);   // JTF 50h at 28h
    code.push_back(0x50);
    Rig r(code);
    r.Step(3);
    r.Step(36);   // the NOPs up to 28h; the timer has overflowed after 32 cycles
    EXPECT_TRUE(r.cpu.TimerFlag());
    EXPECT_EQ(r.cpu.Pc(), 0x28);
    r.Step();
    EXPECT_EQ(r.cpu.Pc(), 0x50);
    EXPECT_FALSE(r.cpu.TimerFlag());
}

TEST(Mcs48_Test, TimerInterruptGoesToSeven)
{
    // JMP 10h; there EN TCNTI; MOV A,#0FFh; MOV T,A; STRT T: after 32 cycles the routine at 7 runs
    Rig r(At(0x010, {0x25, 0x23, 0xFF, 0x62, 0x55}, At(0x007, {0x93}, {0x04, 0x10})));
    r.Step(5);
    while (r.cpu.Pc() != 0x007 && r.cpu.Clock() < 100 * Mcs48::kClocksPerCycle)
        r.Step();
    EXPECT_EQ(r.cpu.Pc(), 0x007);
    EXPECT_TRUE(r.cpu.InInterrupt());
}

TEST(Mcs48_Test, CounterCountsT1FallingEdges)
{
    // STRT CNT: every high-to-low edge on T1 counts
    Rig r({0x45, 0x00});
    r.Step();
    for (int n = 0; n < 3; ++n)
    {
        r.cpu.SetT1(false);
        r.cpu.SetT1(true);
    }
    EXPECT_EQ(r.cpu.Timer(), 3);
}

TEST(Mcs48_Test, ExpanderNibbles)
{
    // MOV A,#0F6h; MOVD P5,A writes 6; ORLD P5,A ORs with what the port reads; MOVD A,P4 reads a nibble
    Rig r({0x23, 0xF6, 0x3D, 0x8D, 0x0C});
    std::vector<std::pair<int, uint8_t>> writes;
    Mcs48::Bus bus;
    bus.expanderRead = [](int port) { return static_cast<uint8_t>(0xF0 | port); };
    bus.expanderWrite = [&writes](int port, uint8_t nibble) { writes.emplace_back(port, nibble); };
    r.cpu.SetBus(bus);
    r.Step(3);
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0], std::make_pair(5, uint8_t{0x06}));
    EXPECT_EQ(writes[1], std::make_pair(5, uint8_t{0x07})) << "5 OR 6";
    r.Step();
    EXPECT_EQ(r.cpu.A(), 0x04);
}

TEST(Mcs48_Test, RunStopsOnRequest)
{
    Mcs48 cpu;
    std::vector<uint8_t> image(64, 0x00);
    image[4] = 0x90;   // MOVX @R0,A at 4
    cpu.SetProgram(image);
    Mcs48::Bus bus;
    bus.movxWrite = [&cpu](uint8_t, uint8_t) { cpu.RequestStop(); };
    cpu.SetBus(bus);
    cpu.Reset();
    cpu.Run(1000 * Mcs48::kClocksPerCycle);
    EXPECT_EQ(cpu.Pc(), 5) << "the run ends after the MOVX";
}

TEST(Mcs48_Test, StateRoundTrip)
{
    Rig r({0xB8, 0x30, 0x23, 0x55, 0xA0, 0xD5, 0x05, 0x55, 0xF5});
    r.Step(7);
    auto saved = std::make_unique<Mcs48::State>();
    r.cpu.SaveState(*saved);
    r.Step(10);
    r.cpu.SetRam(0x30, 0x00);
    r.cpu.LoadState(*saved);
    auto again = std::make_unique<Mcs48::State>();
    r.cpu.SaveState(*again);
    EXPECT_EQ(std::memcmp(saved.get(), again.get(), sizeof(Mcs48::State)), 0);
    EXPECT_EQ(r.cpu.Ram(0x30), 0x55);
    EXPECT_TRUE(r.cpu.InterruptsEnabled());
}
