// The Z80N instructions of the unreal-next-z80 library (core/src/3rdparty/unreal-next-z80/opcodes-z80n.inc).
//
// Expected sizes and T-states: the instruction table of https://table.specnext.dev/ (grade B, Ped7g's table; the sizes
// and the cycles column). Expected flags of the block-copy family: the FPGA core's T80 microcode (they differ
// from the table, which lists none): flags as LDI. The bus shapes are derived from the same microcode.
// Every case is a few instructions on a flat 64 KB bus: microseconds.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "z80ntestbus.h"

namespace
{
constexpr uint8_t kS = 0x80, kZ = 0x40, kY = 0x20, kH = 0x10, kX = 0x08, kPv = 0x04, kN = 0x02, kC = 0x01;

class Z80nFixture
{
public:
    Z80nFixture() : cpu(Z80nCpuCreate()), bus(cpu) { bus.TraceInternal(); }
    ~Z80nFixture() { Z80nCpuDestroy(cpu); }

    /// Put the bytes at #8000, PC there, SP #C000
    void Program(const std::vector<uint8_t>& bytes)
    {
        bus.Load(0x8000, bytes);
        Z80nCpuSetReg(cpu, Z80nCpuRegPc, 0x8000);
        Z80nCpuSetReg(cpu, Z80nCpuRegSp, 0xC000);
        bus.events.clear();
    }

    int Step() { return Z80nCpuStep(cpu); }
    uint16_t Reg(Z80nCpuReg r) const { return Z80nCpuGetReg(cpu, r); }
    void Set(Z80nCpuReg r, uint16_t v) { Z80nCpuSetReg(cpu, r, v); }
    uint8_t A() const { return Reg(Z80nCpuRegAf) >> 8; }
    uint8_t F() const { return Reg(Z80nCpuRegAf) & 0xFF; }
    void SetA(uint8_t a) { Set(Z80nCpuRegAf, static_cast<uint16_t>((a << 8) | F())); }
    void SetF(uint8_t f) { Set(Z80nCpuRegAf, static_cast<uint16_t>((A() << 8) | f)); }

    /// The kinds of the bus events as a string: M P R W I O N
    std::string Shape() const
    {
        std::string s;
        for (const Z80nTest::Event& e : bus.events)
            s.push_back(e.type);
        return s;
    }

    Z80nCPU* cpu;
    Z80nTest::TestBus bus;
};
}  // namespace

// ---- sizes and T-states: the whole table -----------------------------------------------------------------------------

struct TimingCase
{
    const char* name;
    std::vector<uint8_t> bytes;
    int size;
    int t;       // the first step, repeat case included
    int tLast;   // the last step of a repeating instruction (0 = not repeating)
};

TEST(Z80nOpcodes_Test, SizesAndTStatesFollowTheInstructionTable)
{
    const std::vector<TimingCase> cases = {
        {"swapnib", {0xED, 0x23}, 2, 8, 0},       {"mirror a", {0xED, 0x24}, 2, 8, 0},
        {"test n", {0xED, 0x27, 0x55}, 3, 11, 0}, {"bsla de,b", {0xED, 0x28}, 2, 8, 0},
        {"bsra de,b", {0xED, 0x29}, 2, 8, 0},     {"bsrl de,b", {0xED, 0x2A}, 2, 8, 0},
        {"bsrf de,b", {0xED, 0x2B}, 2, 8, 0},     {"brlc de,b", {0xED, 0x2C}, 2, 8, 0},
        {"mul d,e", {0xED, 0x30}, 2, 8, 0},       {"add hl,a", {0xED, 0x31}, 2, 8, 0},
        {"add de,a", {0xED, 0x32}, 2, 8, 0},      {"add bc,a", {0xED, 0x33}, 2, 8, 0},
        {"add hl,nn", {0xED, 0x34, 1, 2}, 4, 16, 0}, {"add de,nn", {0xED, 0x35, 1, 2}, 4, 16, 0},
        {"add bc,nn", {0xED, 0x36, 1, 2}, 4, 16, 0}, {"push nn", {0xED, 0x8A, 0x12, 0x34}, 4, 23, 0},
        {"outinb", {0xED, 0x90}, 2, 16, 0},       {"nextreg n,nn", {0xED, 0x91, 0x07, 0x03}, 4, 20, 0},
        {"nextreg n,a", {0xED, 0x92, 0x07}, 3, 17, 0}, {"pixeldn", {0xED, 0x93}, 2, 8, 0},
        {"pixelad", {0xED, 0x94}, 2, 8, 0},       {"setae", {0xED, 0x95}, 2, 8, 0},
        {"jp (c)", {0xED, 0x98}, 2, 13, 0},       {"ldix", {0xED, 0xA4}, 2, 16, 0},
        {"ldws", {0xED, 0xA5}, 2, 14, 0},         {"lddx", {0xED, 0xAC}, 2, 16, 0},
        {"ldirx", {0xED, 0xB4}, 2, 21, 16},       {"ldpirx", {0xED, 0xB7}, 2, 21, 16},
        {"lddrx", {0xED, 0xBC}, 2, 21, 16},
    };
    for (const TimingCase& c : cases)
    {
        Z80nFixture f;
        f.Program(c.bytes);
        f.Set(Z80nCpuRegBc, c.tLast ? 2 : 0x1234);  // two elements for the repeating forms
        f.Set(Z80nCpuRegHl, 0x9000);
        f.Set(Z80nCpuRegDe, 0x9100);
        f.SetA(0xAA);
        const int t = f.Step();
        EXPECT_EQ(t, c.t) << c.name;
        if (c.tLast)
        {
            EXPECT_EQ(f.Reg(Z80nCpuRegPc), 0x8000) << c.name << ": a repeat leaves PC on the instruction";
            EXPECT_EQ(f.Step(), c.tLast) << c.name << ": the last element";
        }
        if (c.name != std::string("jp (c)"))
            EXPECT_EQ(f.Reg(Z80nCpuRegPc), 0x8000 + c.size) << c.name;
    }
}

// ---- register-only instructions --------------------------------------------------------------------------------------

TEST(Z80nOpcodes_Test, SwapnibAndMirror)
{
    Z80nFixture f;
    f.Program({0xED, 0x23, 0xED, 0x24});
    f.SetA(0xB6);
    f.SetF(0xFF);
    f.Step();
    EXPECT_EQ(f.A(), 0x6B);
    f.SetA(0x01);
    f.Step();
    EXPECT_EQ(f.A(), 0x80);
    EXPECT_EQ(f.F(), 0xFF) << "no flags";
    EXPECT_EQ(f.Shape(), "MMMM") << "no data cycle";
}

TEST(Z80nOpcodes_Test, MirrorIsAnInvolutionOverAllValues)
{
    for (int v = 0; v < 256; v++)
    {
        Z80nFixture f;
        f.Program({0xED, 0x24, 0xED, 0x24});
        f.SetA(static_cast<uint8_t>(v));
        f.Step();
        f.Step();
        EXPECT_EQ(f.A(), v);
    }
}

TEST(Z80nOpcodes_Test, BarrelShifts)
{
    struct Case { uint8_t op; uint16_t de; uint8_t b; uint16_t expect; const char* name; };
    const Case cases[] = {
        {0x28, 0x1234, 4, 0x2340, "bsla by 4"},   {0x28, 0x0001, 15, 0x8000, "bsla by 15"},
        {0x28, 0xFFFF, 16, 0x0000, "bsla by 16 empties"}, {0x28, 0xFFFF, 0xE4, 0xFFF0, "bsla uses B bits 4:0 (E4 = 4)"},
        {0x29, 0x8000, 3, 0xF000, "bsra copies the sign"}, {0x29, 0x4000, 3, 0x0800, "bsra positive"},
        {0x29, 0x8000, 31, 0xFFFF, "bsra by 31 of a negative"}, {0x29, 0x7FFF, 31, 0x0000, "bsra by 31 of a positive"},
        {0x2A, 0x8000, 3, 0x1000, "bsrl zero fill"}, {0x2A, 0xFFFF, 16, 0x0000, "bsrl by 16"},
        {0x2B, 0x00F0, 4, 0xF00F, "bsrf one fill"}, {0x2B, 0x0000, 31, 0xFFFF, "bsrf by 31"}, {0x2B, 0x1234, 0, 0x1234, "bsrf by 0"},
        {0x2C, 0x1234, 4, 0x2341, "brlc by 4"}, {0x2C, 0x8001, 1, 0x0003, "brlc by 1"}, {0x2C, 0x1234, 16, 0x1234, "brlc B bits 3:0: 16 = 0"},
        {0x2C, 0x1234, 15, 0x091A, "brlc by 15"},
    };
    for (const Case& c : cases)
    {
        Z80nFixture f;
        f.Program({0xED, c.op});
        f.Set(Z80nCpuRegDe, c.de);
        f.Set(Z80nCpuRegBc, static_cast<uint16_t>(c.b << 8 | 0x55));
        f.SetF(0xFF);
        f.Step();
        EXPECT_EQ(f.Reg(Z80nCpuRegDe), c.expect) << c.name;
        EXPECT_EQ(f.F(), 0xFF) << c.name << ": no flags";
        EXPECT_EQ(f.Reg(Z80nCpuRegBc), c.b << 8 | 0x55) << c.name << ": B is not changed";
    }
}

TEST(Z80nOpcodes_Test, MulDeIsUnsigned)
{
    for (auto [d, e] : {std::pair<int, int>{0xFF, 0xFF}, {0x12, 0x34}, {0, 0xFF}, {0x80, 2}})
    {
        Z80nFixture f;
        f.Program({0xED, 0x30});
        f.Set(Z80nCpuRegDe, static_cast<uint16_t>(d << 8 | e));
        f.SetF(0xFF);
        f.Step();
        EXPECT_EQ(f.Reg(Z80nCpuRegDe), d * e);
        EXPECT_EQ(f.F(), 0xFF);
    }
}

TEST(Z80nOpcodes_Test, AddRrAClearsTheCarryAndKeepsTheRest)
{
    struct Case { uint8_t op; Z80nCpuReg reg; };
    for (Case c : {Case{0x31, Z80nCpuRegHl}, Case{0x32, Z80nCpuRegDe}, Case{0x33, Z80nCpuRegBc}})
    {
        Z80nFixture f;
        f.Program({0xED, c.op});
        f.Set(c.reg, 0xFFF0);
        f.SetA(0x20);
        f.SetF(0xFF);
        f.Step();
        EXPECT_EQ(f.Reg(c.reg), 0x0010) << "wraps";
        EXPECT_EQ(f.F() & kC, 0) << "the carry is not preserved (it is cleared; the table marks it undefined)";
        EXPECT_EQ(f.F() | kC, 0xFF) << "S Z H P/V N X Y stay";
    }
}

TEST(Z80nOpcodes_Test, AddRrNnKeepsAllFlags)
{
    struct Case { uint8_t op; Z80nCpuReg reg; };
    for (Case c : {Case{0x34, Z80nCpuRegHl}, Case{0x35, Z80nCpuRegDe}, Case{0x36, Z80nCpuRegBc}})
    {
        Z80nFixture f;
        f.Program({0xED, c.op, 0x34, 0x12});
        f.Set(c.reg, 0xFFF0);
        f.SetF(0xD5);
        f.Step();
        EXPECT_EQ(f.Reg(c.reg), 0x1224);
        EXPECT_EQ(f.F(), 0xD5);
        EXPECT_EQ(f.Shape(), "MMPNPN") << "two operand reads, each followed by one idle T";
    }
}

TEST(Z80nOpcodes_Test, PixeladAddressesTheScreen)
{
    struct Case { uint8_t y, x; uint16_t addr; };
    for (Case c : {Case{0, 0, 0x4000}, Case{0, 255, 0x401F}, Case{1, 0, 0x4100}, Case{8, 0, 0x4020}, Case{63, 17, 0x47E2},
                   Case{64, 0, 0x4800}, Case{191, 255, 0x57FF}, Case{128, 8, 0x5001}})
    {
        Z80nFixture f;
        f.Program({0xED, 0x94});
        f.Set(Z80nCpuRegDe, static_cast<uint16_t>(c.y << 8 | c.x));
        f.Step();
        EXPECT_EQ(f.Reg(Z80nCpuRegHl), c.addr) << "y " << +c.y << " x " << +c.x;
    }
}

TEST(Z80nOpcodes_Test, PixeldnGoesOneLineDownThroughTheCellBoundaries)
{
    struct Case { uint16_t from, to; const char* name; };
    for (Case c : {Case{0x4000, 0x4100, "inside a character cell"}, Case{0x4700, 0x4020, "next character row of the third"},
                   Case{0x47E0, 0x4800, "next third"}, Case{0x4F00 + 0xE0, 0x5000, "last row of the second third"}})
    {
        Z80nFixture f;
        f.Program({0xED, 0x93});
        f.Set(Z80nCpuRegHl, c.from);
        f.Step();
        EXPECT_EQ(f.Reg(Z80nCpuRegHl), c.to) << c.name;
    }
}

TEST(Z80nOpcodes_Test, SetaeMasksTheXCoordinate)
{
    for (int e = 0; e < 256; e++)
    {
        Z80nFixture f;
        f.Program({0xED, 0x95});
        f.Set(Z80nCpuRegDe, static_cast<uint16_t>(0x0100 | e));
        f.Step();
        EXPECT_EQ(f.A(), 0x80 >> (e & 7)) << "E = " << e;
    }
}

// ---- operands, stack, flags ------------------------------------------------------------------------------------------

TEST(Z80nOpcodes_Test, TestSetsTheFlagsOfAndAndKeepsA)
{
    struct Case { uint8_t a, n; uint8_t flags; };
    for (Case c : {Case{0xF0, 0x0F, kZ | kH | kPv}, Case{0xFF, 0x80, kS | kH | 0x00}, Case{0xFF, 0x03, kH | kPv}, Case{0x0F, 0x28, kH | kX | 0x00}})
    {
        Z80nFixture f;
        f.Program({0xED, 0x27, c.n});
        f.SetA(c.a);
        f.SetF(0xFF);
        f.Step();
        EXPECT_EQ(f.A(), c.a);
        const uint8_t r = c.a & c.n;
        uint8_t expect = static_cast<uint8_t>((r & (kS | kY | kX)) | kH | (r == 0 ? kZ : 0));
        int bits = __builtin_popcount(r);
        if ((bits & 1) == 0)
            expect |= kPv;
        EXPECT_EQ(f.F(), expect) << std::hex << +c.a << " & " << +c.n;
        EXPECT_EQ(f.F() & (kN | kC), 0);
    }
}

TEST(Z80nOpcodes_Test, PushNnIsBigEndianInTheOpcode)
{
    Z80nFixture f;
    f.Program({0xED, 0x8A, 0x12, 0x34});
    f.SetF(0xFF);
    f.Step();
    EXPECT_EQ(f.Reg(Z80nCpuRegSp), 0xBFFE);
    EXPECT_EQ(f.bus.memory[0xBFFE], 0x34);
    EXPECT_EQ(f.bus.memory[0xBFFF], 0x12);
    EXPECT_EQ(f.F(), 0xFF);
    EXPECT_EQ(f.Shape(), "MMPPNNNWW") << "two operand reads, 3 idle T at SP, high byte first, then the low byte";
    ASSERT_EQ(f.bus.events.size(), 9u);
    EXPECT_EQ(f.bus.events[7].addr, 0xBFFF);
    EXPECT_EQ(f.bus.events[8].addr, 0xBFFE);
}

// ---- NEXTREG ---------------------------------------------------------------------------------------------------------

struct NextRegLog
{
    std::vector<std::pair<uint8_t, uint8_t>> writes;
    std::vector<uint32_t> t;
};

static void NextRegHook(Z80nCPU* cpu, uint8_t reg, uint8_t value, void* user)
{
    auto* log = static_cast<NextRegLog*>(user);
    log->writes.push_back({reg, value});
    log->t.push_back(Z80nCpuTstates(cpu));
}

TEST(Z80nOpcodes_Test, NextregCallsTheHostWithoutAPortCycle)
{
    Z80nFixture f;
    NextRegLog log;
    Z80nCpuSetNextRegFn(f.cpu, &NextRegHook, &log);
    f.Program({0xED, 0x91, 0x07, 0x03,   // nextreg #07,3
               0xED, 0x92, 0x56});       // nextreg #56,a
    f.SetA(0x21);
    const uint32_t t0 = Z80nCpuTstates(f.cpu);
    EXPECT_EQ(f.Step(), 20);
    EXPECT_EQ(f.Shape(), "MMPPNNNNNN");
    EXPECT_EQ(f.Step(), 17);
    ASSERT_EQ(log.writes.size(), 2u);
    EXPECT_EQ(log.writes[0], (std::pair<uint8_t, uint8_t>{0x07, 0x03}));
    EXPECT_EQ(log.writes[1], (std::pair<uint8_t, uint8_t>{0x56, 0x21}));
    for (const Z80nTest::Event& e : f.bus.events)
        EXPECT_TRUE(e.type != 'I' && e.type != 'O') << "no port cycle";
    EXPECT_EQ(log.t[0] - t0, 14u + 3u) << "after the operands (14 T) and 3 idle T";
}

TEST(Z80nOpcodes_Test, NextregWithoutAHostIsHarmless)
{
    Z80nFixture f;
    f.Program({0xED, 0x92, 0x07});
    EXPECT_EQ(f.Step(), 17);
}

// ---- I/O -------------------------------------------------------------------------------------------------------------

TEST(Z80nOpcodes_Test, OutinbWritesMemoryToThePortAndKeepsB)
{
    Z80nFixture f;
    f.Program({0xED, 0x90});
    f.bus.memory[0x9000] = 0x5C;
    f.Set(Z80nCpuRegHl, 0x9000);
    f.Set(Z80nCpuRegBc, 0x12FE);
    f.SetF(0xAA);
    EXPECT_EQ(f.Step(), 16);
    EXPECT_EQ(f.Reg(Z80nCpuRegHl), 0x9001);
    EXPECT_EQ(f.Reg(Z80nCpuRegBc), 0x12FE) << "B is not decremented";
    EXPECT_EQ(f.F(), 0xAA);
    EXPECT_EQ(f.Shape(), "MMNRO") << "one idle T, the data read, the port write";
    EXPECT_EQ(f.bus.events.back().addr, 0x12FE);
    EXPECT_EQ(f.bus.events.back().value, 0x5C);
}

TEST(Z80nOpcodes_Test, JpCTakesTheBitsFromThePort)
{
    Z80nFixture f;
    f.Program({0xED, 0x98});
    f.Set(Z80nCpuRegBc, 0x03FE);  // the test bus reads the high byte of the port: #03
    EXPECT_EQ(f.Step(), 13);
    EXPECT_EQ(f.Reg(Z80nCpuRegPc), 0x8000 | (0x03 << 6)) << "(PC after the instruction & #C000) | (value << 6)";
    f.Program({0xED, 0x98});
    f.Set(Z80nCpuRegBc, 0xFFFE);
    f.Step();
    EXPECT_EQ(f.Reg(Z80nCpuRegPc), 0x8000 | 0x3FC0);
}

// ---- block copies with an exclusion byte -----------------------------------------------------------------------------

TEST(Z80nOpcodes_Test, LdixSkipsTheByteEqualToA)
{
    Z80nFixture f;
    f.Program({0xED, 0xA4, 0xED, 0xA4});
    f.bus.memory[0x9000] = 0x11;
    f.bus.memory[0x9001] = 0xAA;  // equals A
    f.Set(Z80nCpuRegHl, 0x9000);
    f.Set(Z80nCpuRegDe, 0xA000);
    f.Set(Z80nCpuRegBc, 5);
    f.SetA(0xAA);
    f.Step();
    f.Step();
    EXPECT_EQ(f.bus.memory[0xA000], 0x11);
    EXPECT_EQ(f.bus.memory[0xA001], 0x00) << "not written";
    EXPECT_EQ(f.Reg(Z80nCpuRegHl), 0x9002);
    EXPECT_EQ(f.Reg(Z80nCpuRegDe), 0xA002);
    EXPECT_EQ(f.Reg(Z80nCpuRegBc), 3);
}

TEST(Z80nOpcodes_Test, BlockCopyFlagsAreLdiFlags)
{
    // S, Z, C stay; H = N = 0; P/V = BC != 0 after the decrement; X and Y from (A + byte) bits 3 and 1
    for (int bc : {2, 1})
    {
        Z80nFixture f;
        f.Program({0xED, 0xA4});
        f.bus.memory[0x9000] = 0x0E;
        f.Set(Z80nCpuRegHl, 0x9000);
        f.Set(Z80nCpuRegDe, 0xA000);
        f.Set(Z80nCpuRegBc, static_cast<uint16_t>(bc));
        f.SetA(0x02);
        f.SetF(0xFF);
        f.Step();
        const uint8_t sum = 0x10;  // 0x0E + 0x02
        const uint8_t xy = static_cast<uint8_t>((sum & kX) + ((sum << 4) & kY));
        const uint8_t expect = static_cast<uint8_t>(kS | kZ | kC | xy | (bc != 1 ? kPv : 0));
        EXPECT_EQ(f.F(), expect) << "BC " << bc;
    }
}

TEST(Z80nOpcodes_Test, LdirxCopiesABlockAndRepeatsByRewindingPc)
{
    Z80nFixture f;
    f.Program({0xED, 0xB4});
    for (int i = 0; i < 8; i++)
        f.bus.memory[0x9000 + i] = static_cast<uint8_t>(0x10 + i);
    f.Set(Z80nCpuRegHl, 0x9000);
    f.Set(Z80nCpuRegDe, 0xA000);
    f.Set(Z80nCpuRegBc, 8);
    f.SetA(0x13);  // skipped value
    int t = 0;
    int steps = 0;
    while (f.Reg(Z80nCpuRegBc) && steps++ < 20)
        t += f.Step();
    EXPECT_EQ(steps, 8);
    EXPECT_EQ(t, 7 * 21 + 16);
    for (int i = 0; i < 8; i++)
        EXPECT_EQ(f.bus.memory[0xA000 + i], i == 3 ? 0x00 : 0x10 + i) << i;
    EXPECT_EQ(f.Reg(Z80nCpuRegPc), 0x8002);
    EXPECT_EQ(f.F() & kPv, 0) << "BC reached 0";
}

TEST(Z80nOpcodes_Test, RepeatLeavesXyFromTheSumNotFromPc)
{
    // unlike LDIR, the FPGA core does not put PC bits into X / Y on a repeat
    Z80nFixture f;
    f.Program({0xED, 0xB4});
    f.bus.memory[0x9000] = 0x00;
    f.Set(Z80nCpuRegHl, 0x9000);
    f.Set(Z80nCpuRegDe, 0xA000);
    f.Set(Z80nCpuRegBc, 2);
    f.SetA(0x00);
    f.bus.memory[0x9000] = 0x55;
    f.Step();
    const uint8_t sum = 0x55;
    EXPECT_EQ(f.F() & (kX | kY), static_cast<uint8_t>((sum & kX) + ((sum << 4) & kY)));
}

TEST(Z80nOpcodes_Test, LddxAndLddrxRunBackwards)
{
    Z80nFixture f;
    f.Program({0xED, 0xBC});
    for (int i = 0; i < 4; i++)
        f.bus.memory[0x9000 + i] = static_cast<uint8_t>(1 + i);
    f.Set(Z80nCpuRegHl, 0x9003);
    f.Set(Z80nCpuRegDe, 0xA000);
    f.Set(Z80nCpuRegBc, 4);
    f.SetA(0xEE);
    while (f.Reg(Z80nCpuRegBc))
        f.Step();
    EXPECT_EQ(f.bus.memory[0xA000], 4);
    EXPECT_EQ(f.bus.memory[0xA003], 1);
    EXPECT_EQ(f.Reg(Z80nCpuRegHl), 0x8FFF) << "HL goes down";
    EXPECT_EQ(f.Reg(Z80nCpuRegDe), 0xA004) << "DE goes up";
}

TEST(Z80nOpcodes_Test, LdpirxReadsFromAnEightByteWindowOfHl)
{
    Z80nFixture f;
    f.Program({0xED, 0xB7});
    for (int i = 0; i < 8; i++)
        f.bus.memory[0x9000 + i] = static_cast<uint8_t>(0x20 + i);
    f.Set(Z80nCpuRegHl, 0x9003);  // only HL bits 15:3 count
    f.Set(Z80nCpuRegDe, 0xA005);  // E bits 2:0 pick the byte
    f.Set(Z80nCpuRegBc, 3);
    f.SetA(0xEE);
    while (f.Reg(Z80nCpuRegBc))
        f.Step();
    EXPECT_EQ(f.bus.memory[0xA005], 0x25);
    EXPECT_EQ(f.bus.memory[0xA006], 0x26);
    EXPECT_EQ(f.bus.memory[0xA007], 0x27);
    EXPECT_EQ(f.Reg(Z80nCpuRegHl), 0x9003) << "HL does not move";
    EXPECT_EQ(f.Reg(Z80nCpuRegDe), 0xA008);
}

TEST(Z80nOpcodes_Test, IrqCanInterruptARepeatingBlock)
{
    Z80nFixture f;
    f.Program({0xED, 0xB4});
    f.Set(Z80nCpuRegHl, 0x9000);
    f.Set(Z80nCpuRegDe, 0xA000);
    f.Set(Z80nCpuRegBc, 100);
    f.SetA(0xEE);
    f.Set(Z80nCpuRegIff1, 1);
    f.Set(Z80nCpuRegIm, 1);
    f.Step();
    ASSERT_EQ(f.Reg(Z80nCpuRegPc), 0x8000);
    ASSERT_NE(Z80nCpuInt(f.cpu), 0);
    EXPECT_EQ(f.Reg(Z80nCpuRegPc), 0x0038);
    EXPECT_EQ(f.bus.memory[0xBFFE] | f.bus.memory[0xBFFF] << 8, 0x8000) << "the return address is the instruction itself";
}

TEST(Z80nOpcodes_Test, LdirscaleActsLikeLdirx)
{
    // decoded by the FPGA core, its scaling commented out in the VHDL: ED B6 = LDIRX
    Z80nFixture a, b;
    for (Z80nFixture* f : {&a, &b})
    {
        f->Program({0xED, f == &a ? uint8_t(0xB6) : uint8_t(0xB4)});
        for (int i = 0; i < 6; i++)
            f->bus.memory[0x9000 + i] = static_cast<uint8_t>(i + 1);
        f->Set(Z80nCpuRegHl, 0x9000);
        f->Set(Z80nCpuRegDe, 0xA000);
        f->Set(Z80nCpuRegBc, 6);
        f->SetA(0x03);
        while (f->Reg(Z80nCpuRegBc))
            f->Step();
    }
    EXPECT_EQ(0, std::memcmp(a.bus.memory + 0xA000, b.bus.memory + 0xA000, 6));
    EXPECT_EQ(a.Reg(Z80nCpuRegHl), b.Reg(Z80nCpuRegHl));
    EXPECT_EQ(a.Reg(Z80nCpuRegDe), b.Reg(Z80nCpuRegDe));
    EXPECT_EQ(a.F(), b.F());
}

TEST(Z80nOpcodes_Test, LdwsIncrementsOnlyLAndDWithIncDFlags)
{
    Z80nFixture f;
    f.Program({0xED, 0xA5});
    f.bus.memory[0x90FF] = 0x77;
    f.Set(Z80nCpuRegHl, 0x90FF);
    f.Set(Z80nCpuRegDe, 0x7FA0);
    f.SetF(kC | kN);
    f.Step();
    EXPECT_EQ(f.bus.memory[0x7FA0], 0x77);
    EXPECT_EQ(f.Reg(Z80nCpuRegHl), 0x9000) << "L wraps, H stays";
    EXPECT_EQ(f.Reg(Z80nCpuRegDe), 0x80A0) << "D increments, E stays";
    EXPECT_EQ(f.F(), static_cast<uint8_t>(kC | kS | kH | kPv)) << "INC D of #7F: S, H, P/V; N = 0; the carry stays";
}

// ---- the aliases the FPGA decodes as RETN ----------------------------------------------------------------------------

struct RetxLog
{
    int reti = 0, retn = 0;
};

TEST(Z80nOpcodes_Test, OnlyEd4DIsRetiTheOtherAliasesAreRetn)
{
    for (int op : {0x45, 0x4D, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D})
    {
        Z80nFixture f;
        RetxLog log;
        Z80nCpuSetRetiFn(f.cpu, [](Z80nCPU*, void* u) { static_cast<RetxLog*>(u)->reti++; }, &log);
        Z80nCpuSetRetnFn(f.cpu, [](Z80nCPU*, void* u) { static_cast<RetxLog*>(u)->retn++; }, &log);
        f.Program({0xED, static_cast<uint8_t>(op)});
        f.bus.memory[0xC000] = 0x34;
        f.bus.memory[0xC001] = 0x12;
        f.Step();
        EXPECT_EQ(f.Reg(Z80nCpuRegPc), 0x1234);
        EXPECT_EQ(log.reti, op == 0x4D ? 1 : 0) << std::hex << op;
        EXPECT_EQ(log.retn, op == 0x4D ? 0 : 1) << std::hex << op;
    }
}

// ---- stackless NMI ---------------------------------------------------------------------------------------------------

struct NmiSink
{
    uint8_t low = 0, high = 0;
    int stores = 0, loads = 0;
};

TEST(Z80nOpcodes_Test, StacklessNmiKeepsTheReturnAddressOutOfMemory)
{
    Z80nFixture f;
    NmiSink sink;
    Z80nCpuSetStacklessNmi(
        f.cpu, 1,
        [](Z80nCPU*, uint8_t lo, uint8_t hi, void* u) { auto* s = static_cast<NmiSink*>(u); s->low = lo; s->high = hi; s->stores++; },
        [](Z80nCPU*, void* u) -> uint16_t { auto* s = static_cast<NmiSink*>(u); s->loads++; return static_cast<uint16_t>(s->low | s->high << 8); },
        &sink);
    f.Program({0x00, 0x00});
    f.bus.memory[0x0066] = 0xED;  // RETN at the NMI vector
    f.bus.memory[0x0067] = 0x45;
    f.bus.memory[0xBFFE] = 0xEE;
    f.bus.memory[0xBFFF] = 0xEE;
    f.Step();                                         // NOP
    f.bus.events.clear();
    const int t = Z80nCpuNmi(f.cpu);
    EXPECT_EQ(t, 11);
    EXPECT_EQ(sink.stores, 1);
    EXPECT_EQ(sink.low, 0x01);
    EXPECT_EQ(sink.high, 0x80);
    EXPECT_EQ(f.Reg(Z80nCpuRegSp), 0xBFFE) << "SP still goes down";
    EXPECT_EQ(f.bus.memory[0xBFFE], 0xEE) << "but no byte is written";
    EXPECT_EQ(f.bus.memory[0xBFFF], 0xEE);
    for (const Z80nTest::Event& e : f.bus.events)
        EXPECT_NE(e.type, 'W');
    EXPECT_EQ(f.Reg(Z80nCpuRegPc), 0x0066);

    EXPECT_EQ(f.Step(), 14) << "RETN: 8 T + two 3 T pops without a memory access";
    EXPECT_EQ(sink.loads, 1);
    EXPECT_EQ(f.Reg(Z80nCpuRegPc), 0x8001);
    EXPECT_EQ(f.Reg(Z80nCpuRegSp), 0xC000);
}

TEST(Z80nOpcodes_Test, WithoutTheModeTheNmiPushesToTheStack)
{
    Z80nFixture f;
    f.Program({0x00});
    f.Step();
    EXPECT_EQ(Z80nCpuNmi(f.cpu), 11);
    EXPECT_EQ(f.bus.memory[0xBFFF], 0x80);
    EXPECT_EQ(f.bus.memory[0xBFFE], 0x01);
}

TEST(Z80nOpcodes_Test, ARetnWithoutAStacklessNmiPopsTheStack)
{
    Z80nFixture f;
    NmiSink sink;
    Z80nCpuSetStacklessNmi(
        f.cpu, 1, [](Z80nCPU*, uint8_t, uint8_t, void*) {}, [](Z80nCPU*, void* u) -> uint16_t { static_cast<NmiSink*>(u)->loads++; return 0xFFFF; }, &sink);
    f.Program({0xED, 0x45});
    f.bus.memory[0xC000] = 0x00;
    f.bus.memory[0xC001] = 0x90;
    f.Step();
    EXPECT_EQ(f.Reg(Z80nCpuRegPc), 0x9000);
    EXPECT_EQ(sink.loads, 0);
}
