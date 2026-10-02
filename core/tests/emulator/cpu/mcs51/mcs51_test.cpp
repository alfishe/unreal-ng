// MCS-51 core: results, flags and machine cycles from the Intel MCS-51
// user's manual (instruction set, timers, serial port, interrupts), and the
// AT89S52 datasheet for timer 2

#include <gtest/gtest.h>

#include <vector>

#include "emulator/cpu/mcs51/mcs51.h"

using mcs51::Mcs51;

namespace
{
constexpr uint8_t kPsw = Mcs51::kPsw, kAcc = Mcs51::kAcc;
constexpr uint8_t CY = 0x80, AC = 0x40, OV = 0x04, P = 0x01;

/// Runs `code` from address 0 for exactly `instructions` instructions
struct Rig
{
    Mcs51 cpu;
    std::vector<uint16_t> movxWrites;
    std::vector<uint8_t> movxData;
    std::vector<uint8_t> serialOut;

    explicit Rig(std::vector<uint8_t> code, Mcs51::Variant v = Mcs51::Variant::I8051) : cpu(v)
    {
        cpu.SetProgram(std::move(code));
        Mcs51::Bus bus;
        bus.movxRead = [](uint16_t a) { return static_cast<uint8_t>(a ^ 0x5A); };
        bus.movxWrite = [this](uint16_t a, uint8_t d) {
            movxWrites.push_back(a);
            movxData.push_back(d);
        };
        bus.serialOut = [this](uint8_t b, bool) { serialOut.push_back(b); };
        cpu.SetBus(bus);
        cpu.Reset();
    }

    /// Runs n instructions; returns the machine cycles they took
    uint64_t Step(int n = 1)
    {
        const uint64_t start = cpu.Clock();
        const uint64_t target = cpu.Instructions() + static_cast<uint64_t>(n);
        while (cpu.Instructions() < target)
            cpu.Run(cpu.Clock() + 1);
        return (cpu.Clock() - start) / 12;
    }
    uint8_t Flags() const { return static_cast<uint8_t>(cpu.Psw() & (CY | AC | OV | P)); }
};
}  // namespace

TEST(Mcs51_Test, ResetValues)
{
    Rig r({0x00});
    EXPECT_EQ(r.cpu.Sfr(Mcs51::kSp), 0x07);
    EXPECT_EQ(r.cpu.Sfr(Mcs51::kP1), 0xFF);
    EXPECT_EQ(r.cpu.Sfr(Mcs51::kP3), 0xFF);
    EXPECT_EQ(r.cpu.Pc(), 0);
    EXPECT_EQ(r.Step(), 1u) << "NOP: one machine cycle";
}

TEST(Mcs51_Test, AddFlags)
{
    // MOV A,#0C3h; ADD A,#0AAh (manual example: A = 6Dh, CY = 1, AC = 0, OV = 1)
    Rig r({0x74, 0xC3, 0x24, 0xAA});
    r.Step(2);
    EXPECT_EQ(r.cpu.Acc(), 0x6D);
    EXPECT_EQ(r.Flags() & (CY | AC | OV), CY | OV);
    // 0x0F + 0x01: AC only; 0x7F + 0x01: OV and AC
    Rig a({0x74, 0x0F, 0x24, 0x01, 0x74, 0x7F, 0x24, 0x01});
    a.Step(2);
    EXPECT_EQ(a.Flags() & (CY | AC | OV), AC);
    a.Step(2);
    EXPECT_EQ(a.cpu.Acc(), 0x80);
    EXPECT_EQ(a.Flags() & (CY | AC | OV), AC | OV);
}

TEST(Mcs51_Test, AddcUsesCarry)
{
    // SETB C; MOV A,#0C3h; ADDC A,#0AAh -> 6Eh, CY 1, AC 0, OV 1 (manual example)
    Rig r({0xD3, 0x74, 0xC3, 0x34, 0xAA});
    r.Step(3);
    EXPECT_EQ(r.cpu.Acc(), 0x6E);
    EXPECT_EQ(r.Flags() & (CY | AC | OV), CY | OV);
}

TEST(Mcs51_Test, SubbFlags)
{
    // SETB C; MOV A,#0C9h; SUBB A,#54h -> 74h, CY 0, AC 0, OV 1 (manual example)
    Rig r({0xD3, 0x74, 0xC9, 0x94, 0x54});
    r.Step(3);
    EXPECT_EQ(r.cpu.Acc(), 0x74);
    EXPECT_EQ(r.Flags() & (CY | AC | OV), OV);
    // 0x00 - 0x01: borrow everywhere
    Rig b({0xC3, 0xE4, 0x94, 0x01});
    b.Step(3);
    EXPECT_EQ(b.cpu.Acc(), 0xFF);
    EXPECT_EQ(b.Flags() & (CY | AC | OV), CY | AC);
}

TEST(Mcs51_Test, DecimalAdjust)
{
    // MOV A,#56h; ADD A,#67h; DA A -> 23h, CY 1 (manual: 56 + 67 = 123 BCD)
    Rig r({0x74, 0x56, 0x24, 0x67, 0xD4});
    r.Step(3);
    EXPECT_EQ(r.cpu.Acc(), 0x23);
    EXPECT_TRUE(r.Flags() & CY);
    // SETB C first: DA never clears CY
    Rig c({0xD3, 0x74, 0x12, 0xD4});
    c.Step(3);
    EXPECT_EQ(c.cpu.Acc(), 0x72);
    EXPECT_TRUE(c.Flags() & CY);
}

TEST(Mcs51_Test, MulAndDiv)
{
    // MOV A,#50h; MOV B,#0A0h; MUL AB -> 3200h: A 00, B 32, OV 1, CY 0; 4 cycles
    Rig m({0x74, 0x50, 0x75, 0xF0, 0xA0, 0xD3, 0xA4});
    m.Step(3);
    EXPECT_EQ(m.Step(), 4u);
    EXPECT_EQ(m.cpu.Acc(), 0x00);
    EXPECT_EQ(m.cpu.Sfr(Mcs51::kB), 0x32);
    EXPECT_EQ(m.Flags() & (CY | OV), OV);
    // MOV A,#0FBh; MOV B,#12h; DIV AB -> A 0Dh, B 11h
    Rig d({0x74, 0xFB, 0x75, 0xF0, 0x12, 0x84});
    d.Step(2);
    EXPECT_EQ(d.Step(), 4u);
    EXPECT_EQ(d.cpu.Acc(), 0x0D);
    EXPECT_EQ(d.cpu.Sfr(Mcs51::kB), 0x11);
    EXPECT_EQ(d.Flags() & (CY | OV), 0);
    // Division by zero: OV
    Rig z({0x74, 0x10, 0x75, 0xF0, 0x00, 0x84});
    z.Step(3);
    EXPECT_TRUE(z.Flags() & OV);
}

TEST(Mcs51_Test, ParityFollowsTheAccumulator)
{
    Rig r({0x74, 0x03, 0x74, 0x07});
    r.Step();
    EXPECT_FALSE(r.Flags() & P) << "two ones: even";
    r.Step();
    EXPECT_TRUE(r.Flags() & P) << "three ones: odd";
}

TEST(Mcs51_Test, RegisterBanksAndIndirect)
{
    // MOV PSW,#08h (bank 1); MOV R0,#30h; MOV @R0,#99h; MOV A,30h
    Rig r({0x75, 0xD0, 0x08, 0x78, 0x30, 0x76, 0x99, 0xE5, 0x30});
    r.Step(4);
    EXPECT_EQ(r.cpu.Ram(0x08), 0x30) << "R0 of bank 1 is at 08h";
    EXPECT_EQ(r.cpu.Acc(), 0x99);
}

TEST(Mcs51_Test, UpperRamOnlyOnThe8052)
{
    // MOV R0,#90h; MOV @R0,#77h; MOV A,@R0
    const std::vector<uint8_t> code = {0x78, 0x90, 0x76, 0x77, 0xE6};
    Rig a(code, Mcs51::Variant::I8051);
    a.Step(3);
    EXPECT_NE(a.cpu.Acc(), 0x77);
    Rig b(code, Mcs51::Variant::I8052);
    b.Step(3);
    EXPECT_EQ(b.cpu.Acc(), 0x77);
    EXPECT_EQ(b.cpu.Sfr(0x90), 0xFF) << "direct 90h is still P1";
}

TEST(Mcs51_Test, BitAddressing)
{
    // SETB 07h (20h.7); SETB 0E0h (ACC.0); CPL 0E1h; MOV C,07h; JBC 07h,+0
    Rig r({0xD2, 0x07, 0xD2, 0xE0, 0xB2, 0xE1, 0xA2, 0x07, 0x10, 0x07, 0x00});
    r.Step(4);
    EXPECT_EQ(r.cpu.Ram(0x20), 0x80);
    EXPECT_EQ(r.cpu.Acc(), 0x03);
    EXPECT_TRUE(r.Flags() & CY);
    EXPECT_EQ(r.Step(), 2u);
    EXPECT_EQ(r.cpu.Ram(0x20), 0x00) << "JBC cleared the bit";
}

TEST(Mcs51_Test, PortsReadPinsButReadModifyWriteReadsTheLatch)
{
    // MOV A,P1; ORL P1,#00h
    Rig r({0xE5, 0x90, 0x43, 0x90, 0x00});
    r.cpu.SetPin(1, 3, false);   // something outside pulls P1.3 low
    r.Step();
    EXPECT_EQ(r.cpu.Acc(), 0xF7) << "a read sees the pin";
    r.Step();
    EXPECT_EQ(r.cpu.Latch(1), 0xFF) << "ORL read the latch: the pulled-down pin does not stick";
}

TEST(Mcs51_Test, CallsReturnAndStack)
{
    // 0000: LCALL 0010h; 0003: SJMP $; 0010: ACALL 0020h; 0012: RET; 0020: RET
    std::vector<uint8_t> code(0x30, 0x00);
    code[0] = 0x12; code[1] = 0x00; code[2] = 0x10;
    code[3] = 0x80; code[4] = 0xFE;
    code[0x10] = 0x11; code[0x11] = 0x20;
    code[0x12] = 0x22;
    code[0x20] = 0x22;
    Rig r(code);
    EXPECT_EQ(r.Step(), 2u);
    EXPECT_EQ(r.cpu.Pc(), 0x10);
    EXPECT_EQ(r.cpu.Sfr(Mcs51::kSp), 0x09);
    EXPECT_EQ(r.cpu.Ram(0x08), 0x03) << "the low byte is pushed first";
    r.Step();
    EXPECT_EQ(r.cpu.Pc(), 0x20);
    r.Step(2);
    EXPECT_EQ(r.cpu.Pc(), 0x03);
    EXPECT_EQ(r.cpu.Sfr(Mcs51::kSp), 0x07);
}

TEST(Mcs51_Test, CjneAndDjnz)
{
    // MOV A,#10h; CJNE A,#20h,+2; NOP; NOP; MOV R2,#2; DJNZ R2,$ (loops once)
    Rig r({0x74, 0x10, 0xB4, 0x20, 0x02, 0x00, 0x00, 0x7A, 0x02, 0xDA, 0xFE});
    r.Step(2);
    EXPECT_EQ(r.cpu.Pc(), 0x07) << "jumped over the NOPs";
    EXPECT_TRUE(r.Flags() & CY) << "10h < 20h";
    r.Step();
    EXPECT_EQ(r.Step(2), 4u);
    EXPECT_EQ(r.cpu.Pc(), 0x0B);
}

TEST(Mcs51_Test, MovcAndMovx)
{
    // MOV DPTR,#0100h; MOV A,#2; MOVC A,@A+DPTR; MOVX @DPTR,A; MOVX A,@DPTR
    std::vector<uint8_t> code(0x110, 0x00);
    const uint8_t prog[] = {0x90, 0x01, 0x00, 0x74, 0x02, 0x93, 0xF0, 0xE0};
    std::copy(std::begin(prog), std::end(prog), code.begin());
    code[0x102] = 0xAB;
    Rig r(code);
    r.Step(3);
    EXPECT_EQ(r.cpu.Acc(), 0xAB);
    EXPECT_EQ(r.Step(), 2u);
    ASSERT_EQ(r.movxWrites.size(), 1u);
    EXPECT_EQ(r.movxWrites[0], 0x0100);
    EXPECT_EQ(r.movxData[0], 0xAB);
    r.Step();
    EXPECT_EQ(r.cpu.Acc(), 0x5A) << "read callback: the address's low byte ^ 5Ah";
}

TEST(Mcs51_Test, Timer0Mode2AutoReloadAndInterrupt)
{
    // MOV TMOD,#02h; MOV TH0,#0FCh; MOV TL0,#0FCh; MOV IE,#82h; SETB TR0; SJMP $
    // ISR at 0Bh: INC 30h; RETI
    std::vector<uint8_t> code(0x20, 0x00);
    const uint8_t prog[] = {0x75, 0x89, 0x02, 0x75, 0x8C, 0xFC, 0x75, 0x8A, 0xFC, 0x75, 0xA8, 0x82, 0xD2, 0x8C, 0x80, 0xFE};
    std::copy(std::begin(prog), std::end(prog), code.begin());
    // ISR at 0Bh would overlap the main code: jump there instead
    std::vector<uint8_t> full(0x40, 0x00);
    full[0] = 0x02; full[1] = 0x00; full[2] = 0x20;   // LJMP 0020h
    full[0x0B] = 0x05; full[0x0C] = 0x30; full[0x0D] = 0x32;   // INC 30h; RETI
    std::copy(std::begin(prog), std::end(prog), full.begin() + 0x20);
    Rig r(full);
    r.Step(6);   // up to SJMP $
    r.cpu.Run(r.cpu.Clock() + 12 * 400);
    // Period 4 machine cycles: about 100 overflows in 400 cycles, each served (2 + 1 + 2 cycles of ISR)
    EXPECT_GT(r.cpu.Ram(0x30), 40);
    EXPECT_EQ(r.cpu.Sfr(Mcs51::kTl0) >= 0xFC, true) << "TL0 reloads from TH0";
}

TEST(Mcs51_Test, InterruptPriorityAndPolling)
{
    // Both INT0 (low) and timer 1 (high) pending: timer 1 is served first
    std::vector<uint8_t> code(0x40, 0x00);
    code[0] = 0x02; code[1] = 0x00; code[2] = 0x30;          // LJMP 30h
    code[0x03] = 0x75; code[0x04] = 0x40; code[0x05] = 0x01; // IE0 ISR: MOV 40h,#1 (3 bytes)
    code[0x06] = 0x32;                                       // RETI
    code[0x1B] = 0x75; code[0x1C] = 0x41; code[0x1D] = 0x02; // TF1 ISR: MOV 41h,#2
    code[0x1E] = 0x32;
    // 30h: MOV IP,#08h; MOV TCON,#8Ah (TF1, IE0, IT0); MOV IE,#89h; SJMP $
    const uint8_t main[] = {0x75, 0xB8, 0x08, 0x75, 0x88, 0x8B, 0x75, 0xA8, 0x89, 0x80, 0xFE};
    std::copy(std::begin(main), std::end(main), code.begin() + 0x30);
    Rig r(code);
    r.Step(4);   // LJMP, MOV IP, MOV TCON, MOV IE
    r.Step(1);   // SJMP: the IE write lets one more instruction run first
    r.Step(1);   // the hardware call, then the first ISR instruction
    EXPECT_EQ(r.cpu.Ram(0x41), 0x02) << "the high-priority request first";
    EXPECT_EQ(r.cpu.Ram(0x40), 0x00);
    r.Step(1);   // RETI
    r.Step(1);   // one instruction after RETI
    r.Step(1);   // then INT0
    EXPECT_EQ(r.cpu.Ram(0x40), 0x01);
}

TEST(Mcs51_Test, ExternalInterruptEdge)
{
    Rig r({0x75, 0x88, 0x01, 0x80, 0xFE});   // MOV TCON,#01h (IT0 edge); SJMP $
    r.Step(2);
    r.cpu.SetPin(3, 2, false);
    EXPECT_TRUE(r.cpu.Sfr(Mcs51::kTcon) & 0x02) << "a falling INT0 edge latches IE0";
}

TEST(Mcs51_Test, SerialMode1ThroughTimer1)
{
    // 9600 baud at 11.0592 MHz: TMOD = 20h, TH1 = FDh, SCON = 50h; MOV SBUF,#55h; JNB TI,$
    Rig r({0x75, 0x89, 0x20, 0x75, 0x8D, 0xFD, 0x75, 0x8B, 0xFD, 0xD2, 0x8E, 0x75, 0x98, 0x50,
           0x75, 0x99, 0x55, 0x30, 0x99, 0xFD, 0x80, 0xFE});
    r.Step(5);
    EXPECT_EQ(r.cpu.SerialBitClocks(false), 16ull * 3 * 12 * 2) << "1152 clocks = 9600 baud at 11.0592 MHz";
    const uint64_t start = r.cpu.Clock();
    r.Step();   // MOV SBUF
    while (!(r.cpu.Sfr(Mcs51::kScon) & 0x02))
        r.Step();
    const uint64_t toTi = r.cpu.Clock() - start;
    EXPECT_NEAR(static_cast<double>(toTi), 9.0 * 1152, 1152.0 / 4) << "TI as the stop bit starts";
    r.cpu.Run(r.cpu.Clock() + 2 * 1152);
    ASSERT_EQ(r.serialOut.size(), 1u);
    EXPECT_EQ(r.serialOut[0], 0x55);
}

TEST(Mcs51_Test, SerialReceiveAndOverrun)
{
    Rig r({0x75, 0x98, 0x50, 0x80, 0xFE});   // SCON = 50h (mode 1, REN)
    r.Step(2);
    EXPECT_TRUE(r.cpu.SerialIn(0x41));
    EXPECT_TRUE(r.cpu.Sfr(Mcs51::kScon) & 0x01) << "RI";
    EXPECT_FALSE(r.cpu.SerialIn(0x42)) << "RI still set: the frame is lost";
}

TEST(Mcs51_Test, Timer2BaudGeneratorOn8052)
{
    // T2CON = 34h (RCLK, TCLK, TR2); RCAP2 = FFFDh -> 11059200 / 32 / 3 = 115200 baud
    Rig r({0x75, 0xCB, 0xFF, 0x75, 0xCA, 0xFD, 0x75, 0xCD, 0xFF, 0x75, 0xCC, 0xFD, 0x75, 0xC8, 0x34, 0x75, 0x98, 0x50,
           0x75, 0x99, 0xA5, 0x80, 0xFE},
          Mcs51::Variant::I8052);
    r.Step(6);
    EXPECT_EQ(r.cpu.SerialBitClocks(false), 96u) << "11059200 / 96 = 115200";
    r.Step();
    r.cpu.Run(r.cpu.Clock() + 11 * 96);
    ASSERT_EQ(r.serialOut.size(), 1u);
    EXPECT_EQ(r.serialOut[0], 0xA5);
    EXPECT_FALSE(r.cpu.Sfr(Mcs51::kT2con) & 0x80) << "no TF2 in baud mode";
}

TEST(Mcs51_Test, StateRoundTrip)
{
    Rig r({0x74, 0x42, 0x75, 0x30, 0x99, 0x80, 0xFE});
    r.Step(2);
    Mcs51::State s;
    r.cpu.SaveState(s);
    r.Step(5);
    r.cpu.SetRam(0x30, 0);
    r.cpu.LoadState(s);
    EXPECT_EQ(r.cpu.Acc(), 0x42);
    EXPECT_EQ(r.cpu.Ram(0x30), 0x99);
    EXPECT_EQ(r.cpu.Pc(), 0x05);
}

TEST(Mcs51_Test, InterruptLatencyLatchThenPoll)
{
    // An INT0 edge is latched in the cycle it happens in and polled in the
    // next one: with NOPs running, the hardware call starts two machine
    // cycles after the edge (user's manual, "Response time": 3..9 cycles)
    std::vector<uint8_t> code(0x40, 0x00);
    code[0] = 0x02; code[1] = 0x00; code[2] = 0x20;   // LJMP 20h
    code[0x03] = 0x80; code[0x04] = 0xFE;              // ISR: SJMP $
    const uint8_t main[] = {0x75, 0x88, 0x01, 0x75, 0xA8, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::copy(std::begin(main), std::end(main), code.begin() + 0x20);
    Rig r(code);
    r.Step(4);   // LJMP, MOV TCON, MOV IE, the NOP the IE write lets through
    const uint64_t edge = r.cpu.Clock();
    r.cpu.SetPin(3, 2, false);
    while (r.cpu.Pc() != 0x03)
        r.cpu.Run(r.cpu.Clock() + 1);
    EXPECT_EQ((r.cpu.Clock() - edge) / 12, 4u) << "latch cycle, poll cycle, then the 2-cycle LCALL";
}
