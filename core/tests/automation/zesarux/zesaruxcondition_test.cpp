// zrcp::ConditionEvaluator + extractPcLiteral unit tests.
//
// The evaluator parses the ZEsarUX breakpoint-condition dialect that DeZog
// emits ("PC=08000h and RAM=5", "SP>=65280", user sub-conditions in
// parentheses). Register values and memory come from plain structs, so no
// emulator is involved.

#include <gtest/gtest.h>
#include "zesaruxcondition.h"

using zrcp::ConditionContext;
using zrcp::ConditionEvaluator;
using zrcp::extractPcLiteral;
using Regs = dzrp::IDebugInterface::Registers;

namespace
{

class ZesaruxCondition_test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _regs = Regs{};
        _regs.pc = 0x8000;
        _regs.sp = 0xFF00;
        _regs.af = 0x1234;   // A=0x12, F=0x34
        _regs.bc = 0xBEEF;
        _regs.de = 0xCAFE;
        _regs.hl = 0xDEAD;
        _regs.ix = 0x1111;
        _regs.iy = 0x2222;
        _regs.af2 = 0x3333;
        _regs.bc2 = 0x4444;
        _regs.hl2 = 0x5555;
        _regs.de2 = 0x6666;
        _regs.i = 0x3F;
        _regs.r = 0x21;
        _regs.im = 1;

        // 128K default paging: ROM0, bank 5, bank 2, bank 3
        _slots = {8, 5, 2, 3};

        _memory.assign(0x10000, 0);
        _memory[0x9000] = 0xCD;
        _memory[0x9001] = 0x34;
        _memory[0x9002] = 0x12;
        // Word at (SP-2) = PC so "PC=PEEKW(SP-2)" holds in the step-out tests
        _memory[0xFEFE] = 0x00;
        _memory[0xFEFF] = 0x80;
    }

    bool eval(const std::string& condition, uint16_t breakpointAddr = 0x8000, std::string* error = nullptr)
    {
        ConditionContext ctx;
        ctx.regs = &_regs;
        ctx.slots = &_slots;
        ctx.breakpointAddr = breakpointAddr;
        ctx.readMem = [this](uint16_t a) { return _memory[a]; };
        return ConditionEvaluator::evaluate(condition, ctx, error);
    }

    Regs _regs;
    std::vector<uint8_t> _slots;
    std::vector<uint8_t> _memory;
};

/// region <Numbers and the PC literal>

TEST_F(ZesaruxCondition_test, HexHSuffix)
{
    EXPECT_TRUE(eval("PC=8000h"));
    EXPECT_TRUE(eval("PC=8000H"));
    EXPECT_FALSE(eval("PC=8001h"));
}

TEST_F(ZesaruxCondition_test, DecimalNumbers)
{
    EXPECT_TRUE(eval("PC=32768"));
    EXPECT_FALSE(eval("PC=8000"));
}

TEST_F(ZesaruxCondition_test, Hex0xPrefix)
{
    EXPECT_TRUE(eval("PC=0x8000"));
    EXPECT_FALSE(eval("PC=0x8001"));
}

TEST_F(ZesaruxCondition_test, EmptyConditionStops)
{
    std::string error;
    EXPECT_TRUE(eval("", 0x8000, &error));
    EXPECT_NE(error, "");
}

/// endregion </Numbers and the PC literal>

/// region <Registers>

TEST_F(ZesaruxCondition_test, SixteenBitRegisters)
{
    EXPECT_TRUE(eval("SP=ff00h"));
    EXPECT_TRUE(eval("BC=beefh"));
    EXPECT_TRUE(eval("DE=cafeh"));
    EXPECT_TRUE(eval("HL=deadh"));
    EXPECT_TRUE(eval("IX=1111h"));
    EXPECT_TRUE(eval("IY=2222h"));
    EXPECT_FALSE(eval("HL=deadh and IX=1112h"));
}

TEST_F(ZesaruxCondition_test, PrimedRegisters)
{
    EXPECT_TRUE(eval("AF'=3333h"));
    EXPECT_TRUE(eval("BC'=4444h"));
    EXPECT_TRUE(eval("HL'=5555h"));
    EXPECT_TRUE(eval("DE'=6666h"));
    EXPECT_FALSE(eval("AF'=3334h"));
}

TEST_F(ZesaruxCondition_test, EightBitRegisters)
{
    EXPECT_TRUE(eval("A=12h"));
    EXPECT_TRUE(eval("F=34h"));
    EXPECT_TRUE(eval("B=beh"));
    EXPECT_TRUE(eval("C=efh"));
    EXPECT_TRUE(eval("D=cah"));
    EXPECT_TRUE(eval("E=feh"));
    EXPECT_TRUE(eval("H=deh"));
    EXPECT_TRUE(eval("L=adh"));
    EXPECT_FALSE(eval("A=11h"));
}

TEST_F(ZesaruxCondition_test, HalfIndexRegisters)
{
    EXPECT_TRUE(eval("IXH=11h"));
    EXPECT_TRUE(eval("IXL=11h"));
    EXPECT_TRUE(eval("IYH=22h"));
    EXPECT_TRUE(eval("IYL=22h"));
    EXPECT_FALSE(eval("IXH=12h"));
}

TEST_F(ZesaruxCondition_test, SpecialRegisters)
{
    EXPECT_TRUE(eval("I=3fh"));
    EXPECT_TRUE(eval("R=21h"));
    EXPECT_TRUE(eval("IM=1"));
    EXPECT_FALSE(eval("IM=2"));
}

/// endregion </Registers>

/// region <ROM / RAM banking terms>

TEST_F(ZesaruxCondition_test, RomRamFromBreakpointAddress)
{
    // Breakpoint at 0x8000 sees slot 2 = RAM bank 2
    EXPECT_TRUE(eval("RAM=2", 0x8000));
    EXPECT_FALSE(eval("RAM=5", 0x8000));
    // Breakpoint at 0x0000 sees slot 0 = ROM0
    EXPECT_TRUE(eval("ROM=0", 0x0000));
    EXPECT_FALSE(eval("ROM=1", 0x0000));
    EXPECT_FALSE(eval("RAM=0", 0x0000));  // ROM-backed: RAM is -1
    // Breakpoint at 0x4000 sees slot 1 = RAM bank 5
    EXPECT_TRUE(eval("RAM=5", 0x4000));
    EXPECT_TRUE(eval("ROM=-1", 0x4000));  // not ROM-backed
}

TEST_F(ZesaruxCondition_test, DeZogFullBreakpointCondition)
{
    // The exact shape DeZog's setBreakpointZesarux emits
    EXPECT_TRUE(eval("PC=08000h and RAM=2"));
    EXPECT_FALSE(eval("PC=08000h and RAM=5"));
    EXPECT_FALSE(eval("PC=08001h and RAM=2"));
}

/// endregion </ROM / RAM banking terms>

/// region <Operators>

TEST_F(ZesaruxCondition_test, ComparisonOperators)
{
    EXPECT_TRUE(eval("A=12h"));
    EXPECT_TRUE(eval("A<>11h"));
    EXPECT_TRUE(eval("A!=11h"));
    EXPECT_TRUE(eval("A>11h"));
    EXPECT_TRUE(eval("A<13h"));
    EXPECT_TRUE(eval("A>=12h"));
    EXPECT_TRUE(eval("A<=12h"));
    EXPECT_FALSE(eval("A>=13h"));
    EXPECT_FALSE(eval("A<=11h"));
    EXPECT_FALSE(eval("A<>12h"));
}

TEST_F(ZesaruxCondition_test, LogicalOperators)
{
    EXPECT_TRUE(eval("A=12h AND B=beh"));
    EXPECT_FALSE(eval("A=12h AND B=bfh"));
    EXPECT_TRUE(eval("A=11h OR B=beh"));
    EXPECT_FALSE(eval("A=11h OR B=bfh"));
    EXPECT_TRUE(eval("NOT A=11h"));
    EXPECT_FALSE(eval("NOT A=12h"));
    // NOT binds tighter than AND (ZEsarUX precedence)
    EXPECT_TRUE(eval("NOT A=11h AND B=beh"));
}

TEST_F(ZesaruxCondition_test, Parentheses)
{
    EXPECT_TRUE(eval("(A=12h)"));
    EXPECT_TRUE(eval("(A=11h or B=beh) and (C=efh)"));
    EXPECT_FALSE(eval("(A=11h or B=bfh) and (C=efh)"));
}

TEST_F(ZesaruxCondition_test, Arithmetic)
{
    EXPECT_TRUE(eval("A+1=13h"));
    EXPECT_TRUE(eval("A-2=10h"));
    EXPECT_TRUE(eval("A*2=24h"));
    EXPECT_TRUE(eval("A/2=09h"));
    EXPECT_FALSE(eval("A+1=12h"));
}

TEST_F(ZesaruxCondition_test, CaseInsensitiveKeywordsAndNames)
{
    EXPECT_TRUE(eval("pc=8000H AND (a=12h)"));
    EXPECT_TRUE(eval("Pc=8000h and (a=12h)"));
    EXPECT_TRUE(eval("not a=11h"));
}

/// endregion </Operators>

/// region <PEEK functions>

TEST_F(ZesaruxCondition_test, PeekFunctions)
{
    // _memory[0x9000..0x9002] = CD 34 12: PEEKW reads the little-endian word
    EXPECT_TRUE(eval("PEEK(9000h)=0cdh"));
    EXPECT_TRUE(eval("PEEKB(9001h)=34h"));
    EXPECT_TRUE(eval("PEEKW(9000h)=34cdh"));
    EXPECT_FALSE(eval("PEEKW(9000h)=cd34h"));
    // Address expressions inside PEEK
    EXPECT_TRUE(eval("PEEKW(9000h+0)=34cdh"));
}

/// endregion </PEEK functions>

/// region <Step-over / step-out raw conditions>

TEST_F(ZesaruxCondition_test, StepOverSpCondition)
{
    // DeZog step-over sends "SP>=<current SP>" as the whole condition
    EXPECT_TRUE(eval("SP>=65280"));
    EXPECT_TRUE(eval("SP>=60000"));
    EXPECT_FALSE(eval("SP>=65281"));
}

TEST_F(ZesaruxCondition_test, StepOutPekewCondition)
{
    // DeZog step-out sends "PC=PEEKW(SP-2) AND SP>=<bpSp>"
    EXPECT_TRUE(eval("PC=PEEKW(SP-2) AND SP>=60000"));
    EXPECT_FALSE(eval("PC=PEEKW(SP-2) AND SP>=65281"));
}

/// endregion </Step-over / step-out raw conditions>

/// region <Parse failures stop conservatively>

TEST_F(ZesaruxCondition_test, UnparseableConditionStops)
{
    std::string error;
    EXPECT_TRUE(eval("value=5", 0x8000, &error));
    EXPECT_NE(error, "");

    error.clear();
    EXPECT_TRUE(eval("A=?3", 0x8000, &error));
    EXPECT_NE(error, "");

    error.clear();
    EXPECT_TRUE(eval("FOO=1", 0x8000, &error));
    EXPECT_NE(error, "");

    error.clear();
    EXPECT_TRUE(eval("PC=", 0x8000, &error));
    EXPECT_NE(error, "");

    // Trailing garbage
    error.clear();
    EXPECT_TRUE(eval("A=12h trailing", 0x8000, &error));
    EXPECT_NE(error, "");
}

}  // namespace

/// endregion </Parse failures stop conservatively>

/// region <extractPcLiteral>

TEST(ZesaruxExtractPc_test, DeZogAddressBreakpoint)
{
    uint16_t addr = 0;
    std::string rest;
    ASSERT_TRUE(extractPcLiteral("PC=08006h", addr, rest));
    EXPECT_EQ(addr, 0x8006);
    EXPECT_EQ(rest, "");
}

TEST(ZesaruxExtractPc_test, DeZogBreakpointWithBanksAndUserCondition)
{
    uint16_t addr = 0;
    std::string rest;
    ASSERT_TRUE(extractPcLiteral("PC=08000h and RAM=5 and (A<>0)", addr, rest));
    EXPECT_EQ(addr, 0x8000);
    EXPECT_EQ(rest, "RAM=5 and (A<>0)");
}

TEST(ZesaruxExtractPc_test, PlainHexAddress)
{
    uint16_t addr = 0;
    std::string rest;
    ASSERT_TRUE(extractPcLiteral("PC=8006h", addr, rest));
    EXPECT_EQ(addr, 0x8006);
    EXPECT_EQ(rest, "");
}

TEST(ZesaruxExtractPc_test, RomCondition)
{
    uint16_t addr = 0;
    std::string rest;
    ASSERT_TRUE(extractPcLiteral("PC=00038h and ROM=0", addr, rest));
    EXPECT_EQ(addr, 0x0038);
    EXPECT_EQ(rest, "ROM=0");
}

TEST(ZesaruxExtractPc_test, NoPcLiteralReturnsFalse)
{
    uint16_t addr = 0;
    std::string rest;
    // Step-over
    EXPECT_FALSE(extractPcLiteral("SP>=65280", addr, rest));
    // Step-out (PC=PEEKW, not a literal)
    EXPECT_FALSE(extractPcLiteral("PC=PEEKW(SP-2) AND SP>=65280", addr, rest));
    // No h suffix: not the DeZog literal shape
    EXPECT_FALSE(extractPcLiteral("PC=32768", addr, rest));
}

/// endregion </extractPcLiteral>
