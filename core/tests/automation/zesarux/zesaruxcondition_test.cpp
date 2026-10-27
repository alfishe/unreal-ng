// zrcp::ConditionEvaluator + extractPcLiteral + translateToCore unit tests.
//
// The evaluator parses the ZEsarUX breakpoint-condition dialect that DeZog
// emits ("PC=08000h and RAM=5", "SP>=65280", user sub-conditions in
// parentheses). Register values and memory come from plain structs, so no
// emulator is involved. The translation tests run the core program of each
// condition against the evaluator on random registers and a Memory.

#include <gtest/gtest.h>

#include <cstring>
#include <random>

#include "zesaruxcondition.h"
#include "debugger/breakpoints/breakpointcondition.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

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

TEST_F(ZesaruxCondition_test, RomRamAreThePagingNotTheBreakpointAddress)
{
    // ZEsarUX (remote.c, debug.c): RAM is the bank at 0xC000, ROM the ROM page in slot 0, wherever the breakpoint is
    EXPECT_TRUE(eval("RAM=3", 0x8000));
    EXPECT_TRUE(eval("RAM=3", 0x0000));
    EXPECT_FALSE(eval("RAM=2", 0x8000));
    EXPECT_TRUE(eval("ROM=0", 0x4000));
    EXPECT_FALSE(eval("ROM=1", 0x0000));
    // The 48K ROM (DeZog bank 9) is ROM 1; the bank at 0xC000 follows a paging change
    _slots = {9, 5, 2, 7};
    EXPECT_TRUE(eval("ROM=1"));
    EXPECT_TRUE(eval("RAM=7"));
    // A ROM page past the two (DeZog bank 0x80 + page) is that page
    _slots = {0x80 + 3, 5, 2, 0};
    EXPECT_TRUE(eval("ROM=3"));
    // A 48K (two slots): no paging, both are 0
    _slots = {0, 1};
    EXPECT_TRUE(eval("RAM=0 and ROM=0"));
}

TEST_F(ZesaruxCondition_test, DeZogFullBreakpointCondition)
{
    // The exact shape DeZog's setBreakpointZesarux emits
    EXPECT_TRUE(eval("PC=08000h and RAM=3"));
    EXPECT_FALSE(eval("PC=08000h and RAM=5"));
    EXPECT_FALSE(eval("PC=08001h and RAM=3"));
}

TEST_F(ZesaruxCondition_test, FlagNames)
{
    _regs.af = 0x12D5;  // F = 1101 0101: S Z H P C set, N clear
    EXPECT_TRUE(eval("FS=1 and FZ=1 and FH=1 and FP=1 and FC=1"));
    EXPECT_TRUE(eval("FN=0"));
    EXPECT_TRUE(eval("FV=FP"));
    _regs.af = 0x1200;
    EXPECT_TRUE(eval("FZ=0 and FC=0 and FS=0"));
    EXPECT_FALSE(eval("FZ"));
}

TEST_F(ZesaruxCondition_test, BitwiseOperatorsAndXor)
{
    // & | ^ bind like * and / (ZEsarUX's calc), above + and the comparisons
    EXPECT_TRUE(eval("A&0Fh=2"));
    EXPECT_TRUE(eval("A|1=13h"));
    EXPECT_TRUE(eval("A^12h=0"));
    EXPECT_TRUE(eval("(F & 10h) = 10h"));
    EXPECT_TRUE(eval("1+A&0Fh=3"));
    // XOR is a logical operator on the OR level
    EXPECT_TRUE(eval("A=12h XOR B=0"));
    EXPECT_FALSE(eval("A=12h XOR B=beh"));
    EXPECT_FALSE(eval("A=0 XOR B=0"));
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

TEST(ZesaruxExtractPc_test, ALiteralNotFirstKeepsTheWholeCondition)
{
    uint16_t addr = 0;
    std::string rest;
    ASSERT_TRUE(extractPcLiteral("B=5 AND PC=8003h", addr, rest));
    EXPECT_EQ(addr, 0x8003);
    EXPECT_EQ(rest, "B=5 AND PC=8003h");
    ASSERT_TRUE(extractPcLiteral("PC=8003h OR A=1", addr, rest));
    EXPECT_EQ(rest, "PC=8003h OR A=1") << "only a leading literal followed by AND is DeZog's address part";
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

/// region <Translation to the core>

namespace
{
/// zrcp::translateToCore against the evaluator: the core program has to give the evaluator's answer for any
/// registers and memory (it filters the hits the evaluator would resume from)
class ZesaruxConditionCore_test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    Z80Registers _cpu{};
    Regs _regs{};
    std::vector<uint8_t> _slots{8, 5, 2, 0};

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        if (_context->pModuleLogger)
            _context->pModuleLogger->TurnOffLoggingForAll();
        _memory = new Memory(_context);
        _memory->Reset();
        _memory->DefaultBanksFor48k();
        std::memset(&_cpu, 0, sizeof(_cpu));
    }

    void TearDown() override
    {
        delete _memory;
        delete _context;
    }

    // The same machine state seen through the adapter's registers (dezogdebugadapter.cpp getRegisters)
    void Randomize(std::mt19937& random)
    {
        _cpu.pc = static_cast<uint16_t>(random() & 0x80FF);
        _cpu.sp = static_cast<uint16_t>(random() | 0x8000);
        _cpu.af = static_cast<uint16_t>(random());
        _cpu.bc = static_cast<uint16_t>(random() & 0x0F0F);
        _cpu.de = static_cast<uint16_t>(random());
        _cpu.hl = static_cast<uint16_t>(random());
        _cpu.ix = static_cast<uint16_t>(random());
        _cpu.iy = static_cast<uint16_t>(random());
        _cpu.alt.af = static_cast<uint16_t>(random());
        _cpu.alt.bc = static_cast<uint16_t>(random());
        _cpu.alt.de = static_cast<uint16_t>(random());
        _cpu.alt.hl = static_cast<uint16_t>(random());
        _cpu.i = static_cast<uint8_t>(random());
        _cpu.r_low = static_cast<uint8_t>(random());
        _cpu.r_hi = static_cast<uint8_t>(random() & 0x80);
        _cpu.im = static_cast<uint8_t>(random() % 3);
        if (random() % 3 == 0)
            _cpu.a = 0;  // the rarely true comparisons often enough
        for (int i = 0; i < 8; i++)
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + (random() & 0x7FFF)),
                                            static_cast<uint8_t>(random() & 3));
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(_cpu.sp - 2), static_cast<uint8_t>(_cpu.pc));
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(_cpu.sp - 1), static_cast<uint8_t>(_cpu.pc >> 8));

        _regs.pc = _cpu.pc;
        _regs.sp = _cpu.sp;
        _regs.af = _cpu.af;
        _regs.bc = _cpu.bc;
        _regs.de = _cpu.de;
        _regs.hl = _cpu.hl;
        _regs.ix = _cpu.ix;
        _regs.iy = _cpu.iy;
        _regs.af2 = _cpu.alt.af;
        _regs.bc2 = _cpu.alt.bc;
        _regs.de2 = _cpu.alt.de;
        _regs.hl2 = _cpu.alt.hl;
        _regs.i = _cpu.i;
        _regs.r = static_cast<uint8_t>((_cpu.r_low & 0x7F) | (_cpu.r_hi & 0x80));
        _regs.im = _cpu.im;
    }

    bool Reference(const std::string& condition)
    {
        ConditionContext ctx;
        ctx.regs = &_regs;
        ctx.slots = &_slots;
        ctx.breakpointAddr = _regs.pc;
        ctx.readMem = [this](uint16_t a) { return _memory->DirectReadFromZ80Memory(a); };
        std::string error;
        const bool result = ConditionEvaluator::evaluate(condition, ctx, &error);
        EXPECT_TRUE(error.empty()) << condition << ": " << error;
        return result;
    }

    BreakpointConditionPtr Translate(const std::string& condition)
    {
        std::string core;
        std::string reason;
        EXPECT_TRUE(zrcp::translateToCore(condition, core, reason)) << condition << ": " << reason;
        std::string error;
        BreakpointConditionPtr program = BreakpointConditionProgram::Compile(core, BRK_COND_EXEC, nullptr, error);
        EXPECT_TRUE(program) << condition << " -> " << core << ": " << error;
        return program;
    }

    std::string Refused(const std::string& condition)
    {
        std::string core;
        std::string reason;
        EXPECT_FALSE(zrcp::translateToCore(condition, core, reason)) << condition << " -> " << core;
        return reason;
    }
};
} // namespace

TEST_F(ZesaruxConditionCore_test, TranslationAgreesWithTheEvaluator)
{
    const char* const conditions[] = {
        "SP>=32768",
        "PC=PEEKW(SP-2) AND SP>=40000",
        "A=0",
        "A<>0 or B=3",
        "not A=0 and (C=1 or C=2)",
        "HL + DE * 2 > 0x8000",
        "-A + 5 < 3",
        "C / 4 = 3h",
        "PEEK(HL) = PEEKB(DE) or PEEK(8000h)=1",
        "a = 0 and pc >= 8000h",
        "AF' <> BC' and HL' > DE'",
        "IXH = IXL or IYH < IYL",
        "I > 80h and R < 40h and IM = 1",
        "F = 0 or D + E + H + L = 0",
        "HL",
        "NOT NOT A",
        "+A = A",
        "1 - 2 * 3 = -5",
        "FZ = 1 or FC",
        "(A & 0Fh) = 3 or (B | C) = 0Fh",
        "A ^ B = 0 or E = 0",
        "A = 0 XOR B > 8",
    };
    std::mt19937 random(39);
    for (const char* condition : conditions)
    {
        BreakpointConditionPtr program = Translate(condition);
        ASSERT_TRUE(program) << condition;
        int truths = 0;
        for (int i = 0; i < 1500; i++)
        {
            Randomize(random);
            BreakpointConditionContext x;
            x.cpu = &_cpu;
            x.memory = _memory;
            const BreakpointConditionResult core = program->Evaluate(x);
            ASSERT_NE(core, BreakpointConditionResult::Error) << condition;
            const bool reference = Reference(condition);
            truths += reference ? 1 : 0;
            ASSERT_EQ(core == BreakpointConditionResult::True, reference)
                << condition << " -> " << program->Canonical() << " #" << i << " PC " << _cpu.pc << " SP " << _cpu.sp;
        }
        EXPECT_GT(truths, 0) << condition << ": the comparison never held, nothing was compared";
    }
}

TEST_F(ZesaruxConditionCore_test, WhatTheCoreAnswersDifferentlyIsRefused)
{
    EXPECT_NE(Refused("ROM=1").find("ROM"), std::string::npos) << "the paging is not in the core language";
    EXPECT_NE(Refused("A=1 and RAM=5").find("RAM"), std::string::npos);
    EXPECT_NE(Refused("HL / A = 2").find("divisor"), std::string::npos) << "a zero divisor stops in the evaluator";
    EXPECT_NE(Refused("HL / 0 = 2").find("divisor"), std::string::npos);
    EXPECT_FALSE(Refused("FOO = 1").empty());
    EXPECT_FALSE(Refused("A ==").empty());
    EXPECT_FALSE(Refused("A = 1)").empty());
    EXPECT_FALSE(Refused("").empty());
}

TEST_F(ZesaruxCondition_test, NamesAreCaseInsensitive)
{
    EXPECT_TRUE(eval("pc=8000h and a=12h and peekw(9001h)=1234h"));
    EXPECT_TRUE(eval("Hl=0DEADh"));
}

/// endregion </Translation to the core>
