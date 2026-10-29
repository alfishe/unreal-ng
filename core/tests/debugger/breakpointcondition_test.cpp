/// @file breakpointcondition_test.cpp
/// @brief The condition language of breakpoints and taps (breakpointcondition.h; conditional-breakpoints F1,
/// breakpoint-taps conditions-and-callbacks.md): the Unreal precedence and literals, the operands and their aliases,
/// the refusals at compile time (with the column), the canonical form, and the fast shapes against the stack machine.

#include <gtest/gtest.h>

#include <cstring>
#include <random>
#include <string>

#include "debugger/breakpoints/breakpointcondition.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{
using Result = BreakpointConditionResult;

class BreakpointCondition_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    Z80Registers _cpu{};
    EmulatorState _state{};
    const IBreakpointConditionMachine* _machine = nullptr;

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

    BreakpointConditionContext Context(BreakpointConditionAccess access = {}) const
    {
        BreakpointConditionContext x;
        x.cpu = &_cpu;
        x.memory = _memory;
        x.state = &_state;
        x.machine = _machine;
        x.access = access;
        return x;
    }

    BreakpointConditionPtr Compile(const std::string& text, uint8_t trigger = BRK_COND_ANY,
                                   BreakpointConditionDialect dialect = BreakpointConditionDialect::Unreal,
                                   const LabelManager* labels = nullptr)
    {
        std::string error;
        BreakpointConditionPtr program = BreakpointConditionProgram::Compile(text, trigger, labels, error, dialect);
        EXPECT_TRUE(program) << text << ": " << error;
        return program;
    }

    /// The condition's value, also checked against the stack machine when a fast shape answered
    bool Holds(const std::string& text, BreakpointConditionAccess access = {},
               BreakpointConditionDialect dialect = BreakpointConditionDialect::Unreal)
    {
        BreakpointConditionPtr program = Compile(text, BRK_COND_ANY, dialect);
        if (!program)
            return false;
        const Result result = program->Evaluate(Context(access));
        EXPECT_EQ(result, program->EvaluateCode(Context(access))) << text;
        EXPECT_NE(result, Result::Error) << text;
        return result == Result::True;
    }

    std::string Refused(const std::string& text, uint8_t trigger = BRK_COND_ANY)
    {
        std::string error;
        EXPECT_FALSE(BreakpointConditionProgram::Compile(text, trigger, nullptr, error)) << text;
        return error;
    }
};
}  // namespace

TEST_F(BreakpointCondition_Test, NumbersAreHexUnlessWrittenDecimal)
{
    _cpu.hl = 0x4000;
    EXPECT_TRUE(Holds("HL == 4000"));
    EXPECT_TRUE(Holds("HL == #4000"));
    EXPECT_TRUE(Holds("HL == $4000"));
    EXPECT_TRUE(Holds("HL == 0x4000"));
    EXPECT_TRUE(Holds("HL == 4000h"));
    EXPECT_TRUE(Holds("HL == 16384d"));
    _cpu.a = 0xED;
    EXPECT_TRUE(Holds("A == 0ED")) << "a hex number starts with a digit";
    EXPECT_TRUE(Holds("A == 237d"));
    _cpu.a = 'x';
    EXPECT_TRUE(Holds("A == 'x'"));
    // The C dialect reads plain digits as decimal
    _cpu.hl = 4000;
    EXPECT_TRUE(Holds("HL == 4000", {}, BreakpointConditionDialect::C));
    EXPECT_TRUE(Holds("HL == 0xFA0", {}, BreakpointConditionDialect::C));
}

TEST_F(BreakpointCondition_Test, UnrealPrecedenceComparisonsBindTighterThanBitwise)
{
    // Unreal (rules.md §5.3): A & 0F == 3 is A & (0F == 3), i.e. A & 0 (0F == 3 is false)
    _cpu.a = 0x13;
    EXPECT_FALSE(Holds("A & 0F == 3"));
    EXPECT_TRUE(Holds("(A & 0F) == 3"));
    // C dialect: comparisons are looser than + but tighter than &, so the same text also reads A & (0F == 3)
    EXPECT_FALSE(Holds("A & 0xF == 3", {}, BreakpointConditionDialect::C));
    // && binds tighter than ||; arithmetic as usual
    _cpu.b = 1;
    EXPECT_TRUE(Holds("B == 1 || B == 2 && B == 3"));
    EXPECT_FALSE(Holds("(B == 1 || B == 2) && B == 3"));
    EXPECT_TRUE(Holds("2 + 3 * 4 == 0E"));
    EXPECT_TRUE(Holds("1 << 4 == 10"));
    EXPECT_TRUE(Holds("10 - 4 - 2 == 0A")) << "left-associative";
    EXPECT_TRUE(Holds("7 / 0 == 7 && 7 % 0 == 7")) << "division by zero leaves the left operand (Unreal)";
    EXPECT_TRUE(Holds("!0 && ~0 == -1 && -1 < 0"));
    EXPECT_TRUE(Holds("A = 13 && A <> 14")) << "= and <> are aliases";
}

TEST_F(BreakpointCondition_Test, RegistersFlagsAndState)
{
    _cpu.af = 0x1234;
    _cpu.bc = 0x5678;
    _cpu.ix = 0x9ABC;
    _cpu.alt.af = 0xCAFE;
    _cpu.alt.hl = 0xBEEF;
    _cpu.i = 0x3F;
    _cpu.r_low = 0x85;
    _cpu.r_hi = 0x80;
    _cpu.im = 2;
    _cpu.iff1 = 1;
    _cpu.memptr = 0x1111;
    EXPECT_TRUE(Holds("A == 12 && F == 34 && B == 56 && C == 78 && BC == 5678"));
    EXPECT_TRUE(Holds("IXH == 9A && IXL == 0BC && IX == 9ABC"));
    EXPECT_TRUE(Holds("AF' == 0CAFE && A' == 0CA && H' == 0BE && L' == 0EF && hl' == 0BEEF"));
    EXPECT_TRUE(Holds("I == 3F && R == 85 && IM == 2 && IFF1 && !IFF2 && WZ == 1111 && MEMPTR == WZ"));
    EXPECT_TRUE(Holds("a == 12 && ix == 9abc")) << "case-insensitive";
    // Flags: C stays the register, CY is the carry
    _cpu.f = 0x41;  // Z, C
    EXPECT_TRUE(Holds("Z && CY && !NZ && !NC && PO && FP"));
    _cpu.f = 0x84;
    EXPECT_TRUE(Holds("NZ && NC && PE && FM"));
    _cpu.t = 12345;
    _state.frame_counter = 77;
    _state.p7FFD = 0x10;
    _state.flags = CF_TRDOS;
    EXPECT_TRUE(Holds("T == 12345d && FRAME == 77d && FD == 10 && DOS"));
    _state.flags = 0;
    EXPECT_FALSE(Holds("DOS"));
}

TEST_F(BreakpointCondition_Test, MemoryAndPages)
{
    _memory->DirectWriteToZ80Memory(0x5C78, 13);
    _memory->DirectWriteToZ80Memory(0x8000, 0xE8);
    _memory->DirectWriteToZ80Memory(0x8001, 0x03);
    _memory->DirectWriteToZ80Memory(0x8005, 0xC9);
    _cpu.hl = 0x8000;
    _cpu.ix = 0x8000;
    EXPECT_TRUE(Holds("M(5C78) == #0D"));
    EXPECT_TRUE(Holds("M(5C78) == 13d && M(5C78) == 0Dh"));
    EXPECT_FALSE(Holds("M(5C78) == 0D")) << "digits then d are decimal: 0D is 0";
    EXPECT_TRUE(Holds("W(HL) == 1000d")) << "a little-endian word";
    EXPECT_TRUE(Holds("IX->5 == 0C9")) << "a->b is M(a+b)";
    EXPECT_TRUE(Holds("M(IX + 5) == 0C9"));
    EXPECT_TRUE(Holds("(A & 0F) == 0 && (B > 5 || M(IX+5) == 0C9) && SP < 8000"));
    // 48K: ROM in window 0, RAM 5, 2, 0 in 1-3
    EXPECT_TRUE(Holds("PG0 == 0FF && PG1 == 5 && PG2 == 2 && PG3 == 0 && ROMPG != 0FF"));
}

TEST_F(BreakpointCondition_Test, AccessOperandsAndAliases)
{
    BreakpointConditionAccess write;
    write.writeAddress = 0x5C78;
    write.memoryValue = 0;
    EXPECT_TRUE(Holds("WR == 5C78 && MDT == 0 && VAL == 0 && ADDR == 5C78", write));
    EXPECT_TRUE(Holds("MWA == 5C78 && MWV == 0 && WADDR == 5C78 && VALUE == 0", write));
    EXPECT_TRUE(Holds("RD == 0FFFFFFFF && IN == 0FFFFFFFF")) << "no such access: FFFFFFFF (Unreal)";
    BreakpointConditionAccess out;
    out.portOut = 0x00FE;
    out.portValue = 0x10;
    EXPECT_TRUE(Holds("OUT == 0FE && VAL & 10 && PWV == 10 && PORT == 0FE && WPORT == 0FE", out));
}

TEST_F(BreakpointCondition_Test, RefusalsNameTheProblemAndItsColumn)
{
    EXPECT_EQ(Refused(""), "empty condition at column 1");
    EXPECT_EQ(Refused("A == "), "operand expected at column 6");
    EXPECT_EQ(Refused("A == (1"), "missing ')' for '(' at column 6 at column 8");
    EXPECT_EQ(Refused("A == 1)"), "unexpected ')' at column 7");
    EXPECT_EQ(Refused("QQ == 1"), "unknown name 'QQ' at column 1");
    EXPECT_EQ(Refused("A == 12G"), "bad number '12G' at column 6");
    EXPECT_EQ(Refused("A @ 1"), "'@' takes 'prev' at column 3");
    EXPECT_EQ(Refused("A == #12345678901234567"), "bad hex number at column 6") << "17 digits do not fit 64 bits";
    EXPECT_TRUE(Compile("A == #1234567890123456"));
    // The trigger supplies the access operands: none of these can ever hold, so they are refused, not inert
    EXPECT_EQ(Refused("RD == 4000", BRK_COND_EXEC), "RD is not supplied by this breakpoint's trigger at column 1");
    EXPECT_EQ(Refused("A == 0 && MWV == 0", BRK_COND_EXEC),
              "MWV is not supplied by this breakpoint's trigger at column 11");
    EXPECT_EQ(Refused("VAL == 1", BRK_COND_PORT_IN), "VAL is not supplied by this breakpoint's trigger at column 1")
        << "an IN breakpoint is checked before the port is read";
    EXPECT_TRUE(Compile("MWV == 0", BRK_COND_WRITE));
    EXPECT_TRUE(Compile("RD == 4000 || WR == 4000", BRK_COND_READ | BRK_COND_WRITE));
    // A program deeper than the stack
    std::string deep = "1";
    for (int i = 0; i < 40; i++)
        deep = "(A + " + deep + ")";
    EXPECT_NE(Refused(deep).find("too complex"), std::string::npos);
}

TEST_F(BreakpointCondition_Test, LabelsResolveAndPrintByName)
{
    LabelManager labels(_context);
    ASSERT_TRUE(labels.AddLabel("score", 0x8000, UINT16_MAX, UINT16_MAX));
    ASSERT_TRUE(labels.AddLabel("HL", 0x9000, UINT16_MAX, UINT16_MAX));
    _memory->DirectWriteToZ80Memory(0x8000, 0xE8);
    _memory->DirectWriteToZ80Memory(0x8001, 0x03);
    BreakpointConditionPtr program = Compile("W(score) >= 1000d", BRK_COND_ANY, BreakpointConditionDialect::Unreal, &labels);
    ASSERT_TRUE(program);
    EXPECT_TRUE(program->UsesLabels());
    EXPECT_EQ(program->Canonical(), "W(score) >= #3E8");
    EXPECT_EQ(program->Evaluate(Context()), Result::True);
    // A register name wins; a leading '.' forces the label
    _cpu.hl = 0x9000;
    program = Compile(".HL == HL", BRK_COND_ANY, BreakpointConditionDialect::Unreal, &labels);
    ASSERT_TRUE(program);
    EXPECT_EQ(program->Evaluate(Context()), Result::True);
}

TEST_F(BreakpointCondition_Test, CanonicalFormIsUnrealAndParsesBack)
{
    const char* const texts[] = {"a&0f==3", "MWV==0", "Z", "hl >= 0x4000 && m(hl) != 255d", "(A & 0F) == 3 || B > 5",
                                 "IX->5 == 0C9", "-1 < A", "!(A == 0)", "2 + 3 * 4 == 0E"};
    for (const char* text : texts)
    {
        BreakpointConditionPtr program = Compile(text);
        ASSERT_TRUE(program) << text;
        BreakpointConditionPtr again = Compile(program->Canonical());
        ASSERT_TRUE(again) << program->Canonical();
        EXPECT_EQ(again->Canonical(), program->Canonical()) << text;
    }
    EXPECT_EQ(Compile("a&0f==3")->Canonical(), "A & (#F == 3)") << "constant folding keeps the shape";
    EXPECT_EQ(Compile("hl >= 0x4000 && m(hl) != 255d")->Canonical(), "(HL >= #4000) && (M(HL) != #FF)");
    EXPECT_EQ(Compile("Z")->Canonical(), "(F & #40) != 0");
    // The C dialect prints in the Unreal form too
    EXPECT_EQ(Compile("A & 15 == 3", BRK_COND_ANY, BreakpointConditionDialect::C)->Canonical(), "A & (#F == 3)");
}

TEST_F(BreakpointCondition_Test, FastShapesMatchTheStackMachine)
{
    struct Case
    {
        const char* text;
        bool fast;
    };
    const Case cases[] = {
        {"A == 0", true},       {"HL >= 4000", true},    {"0 == A", true},          {"4000 < HL", true},
        {"(F & 40) != 0", true}, {"Z", true},            {"MWV == 0", true},        {"M(5C78) == #0D", true},
        {"A", true},            {"A == 0 && B == 1", false}, {"M(HL) == 0FF", false}, {"W(8000) >= 1000d", false},
        {"(A & 0F) == 3 && (B > 5 || M(IX+5) == 0C9) && SP < 8000", false},
    };
    std::mt19937 random(38);
    for (const Case& c : cases)
    {
        BreakpointConditionPtr program = Compile(c.text);
        ASSERT_TRUE(program) << c.text;
        EXPECT_EQ(program->IsFast(), c.fast) << c.text;
        for (int i = 0; i < 2000; i++)
        {
            _cpu.af = static_cast<uint16_t>(random());
            _cpu.bc = static_cast<uint16_t>(random());
            _cpu.hl = static_cast<uint16_t>(random());
            _cpu.ix = static_cast<uint16_t>(random());
            _cpu.sp = static_cast<uint16_t>(random());
            if (i % 4 == 0)
                _cpu.a = 0;  // the rare true case often enough
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + (random() & 0x3FFF)), static_cast<uint8_t>(random()));
            BreakpointConditionAccess access;
            access.writeAddress = random() & 0xFFFF;
            access.memoryValue = random() & (i % 3 ? 0xFF : 0);
            ASSERT_EQ(program->Evaluate(Context(access)), program->EvaluateCode(Context(access))) << c.text << " #" << i;
        }
    }
}

TEST_F(BreakpointCondition_Test, ShortCircuitAndErrors)
{
    // The right side of a false && is not evaluated: a missing CPU there is no error
    BreakpointConditionPtr program = Compile("HITS == 3 && A == 0");
    ASSERT_TRUE(program);
    BreakpointConditionContext noCpu;
    noCpu.hits = 2;
    EXPECT_EQ(program->Evaluate(noCpu), Result::False);
    noCpu.hits = 3;
    std::string why;
    EXPECT_EQ(program->Evaluate(noCpu, &why), Result::Error);
    EXPECT_EQ(why, "no CPU");
    program = Compile("FRAME > 0");
    EXPECT_EQ(program->Evaluate(BreakpointConditionContext{}, &why), Result::Error);
    EXPECT_EQ(why, "no FRAME here");
    // A dry run on a context refuses what cannot evaluate there
    std::string error;
    EXPECT_FALSE(BreakpointConditionProgram::Compile("M(HL) == 0", BRK_COND_ANY, nullptr, error,
                                                     BreakpointConditionDialect::Unreal, &noCpu));
    EXPECT_EQ(error, "the condition does not evaluate here: no CPU");
    // Addresses wrap at 64K, as the CPU's own
    _memory->DirectWriteToZ80Memory(0x0000, 0xAA);
    EXPECT_EQ(Compile("M(0FFFF + 1) == M(0)")->Evaluate(Context()), Result::True);
}

TEST_F(BreakpointCondition_Test, DialectNames)
{
    BreakpointConditionDialect dialect = BreakpointConditionDialect::C;
    EXPECT_TRUE(BreakpointConditionProgram::ParseDialect("", dialect));
    EXPECT_EQ(dialect, BreakpointConditionDialect::Unreal);
    EXPECT_TRUE(BreakpointConditionProgram::ParseDialect("C", dialect));
    EXPECT_EQ(dialect, BreakpointConditionDialect::C);
    EXPECT_FALSE(BreakpointConditionProgram::ParseDialect("pascal", dialect));
}

TEST_F(BreakpointCondition_Test, PrevIsTheValueOfTheEvaluationBefore)
{
    BreakpointConditionPtr down = Compile("M(#5C78) < M(#5C78)@prev");
    ASSERT_TRUE(down);
    EXPECT_TRUE(down->UsesPrevious());
    EXPECT_FALSE(down->IsFast());
    EXPECT_EQ(down->Canonical(), "M(#5C78) < M(#5C78)@prev");
    auto evaluate = [&](uint8_t value) {
        _memory->DirectWriteToZ80Memory(0x5C78, value);
        return down->Evaluate(Context());
    };
    EXPECT_EQ(evaluate(5), Result::False) << "the first evaluation: x@prev is x";
    EXPECT_EQ(evaluate(4), Result::True) << "went down";
    EXPECT_EQ(evaluate(4), Result::False);
    EXPECT_EQ(evaluate(7), Result::False) << "went up";
    EXPECT_EQ(evaluate(3), Result::True);
    down->ResetPrevious();
    EXPECT_EQ(evaluate(1), Result::False) << "after a reset it starts again";

    // An expression in brackets, two slots, any case; the canonical text compiles back
    BreakpointConditionPtr pair = Compile("(A + 1)@PREV != (A + 1) && B@prev == B");
    ASSERT_TRUE(pair);
    _cpu.a = 1;
    _cpu.b = 2;
    EXPECT_EQ(pair->Evaluate(Context()), Result::False);
    _cpu.a = 2;
    EXPECT_EQ(pair->Evaluate(Context()), Result::True);
    EXPECT_TRUE(Compile(pair->Canonical()));

    // A compile-time dry run does not count as the evaluation before
    std::string error;
    _memory->DirectWriteToZ80Memory(0x5C78, 9);
    const BreakpointConditionContext dry = Context();
    BreakpointConditionPtr checked =
        BreakpointConditionProgram::Compile("M(5C78) != M(5C78)@prev", BRK_COND_ANY, nullptr, error,
                                            BreakpointConditionDialect::Unreal, &dry);
    ASSERT_TRUE(checked) << error;
    _memory->DirectWriteToZ80Memory(0x5C78, 8);
    EXPECT_EQ(checked->Evaluate(Context()), Result::False);

    EXPECT_NE(Refused("A@next").find("'@' takes 'prev'"), std::string::npos);
    EXPECT_NE(Refused("A@").find("'@' takes 'prev'"), std::string::npos);
}

namespace
{
/// A machine with a register file, a beam and a copper (the Next's decoder on a real machine)
struct FakeConditionMachine : IBreakpointConditionMachine
{
    uint8_t regs[256] = {};
    uint16_t line = 0, hc = 0, copperPc = 0;
    bool NextRegister(uint8_t reg, uint8_t& value) const override
    {
        value = regs[reg];
        return true;
    }
    bool Beam(uint16_t& l, uint16_t& h) const override
    {
        l = line;
        h = hc;
        return true;
    }
    bool CopperPc(uint16_t& pc) const override
    {
        pc = copperPc;
        return true;
    }
};
}  // namespace

// platform-debugging P4: the paging latches of the extended machines, the CPU speed, the ROM of every window, the 8K
// slots; NR(x), the beam and the copper only where the machine answers them
TEST_F(BreakpointCondition_Test, TheExtendedMachinesLatchesSlotsAndSpeed)
{
    _state.p7FFD = 0x10;
    _state.pDFFD = 0x07;
    _state.p1FFD = 0x04;
    _state.pEFF7 = 0x08;
    _state.pFF77 = 0x41;
    _state.hw_turbo_ratio = 2;
    EXPECT_TRUE(Holds("DFFD == 7 && P1FFD == 4 && EFF7 == 8 && FF77 == 41 && CPUSPEED == 2"));
    EXPECT_TRUE(Holds("PDFFD == DFFD && PEFF7 == EFF7 && PFF77 == FF77 && P7FFD == FD"));
    _state.hw_turbo_ratio = 0;
    EXPECT_TRUE(Holds("CPUSPEED == 1")) << "a state without a ratio runs at the base clock";
    // 48K: ROM in window 0, RAM 5, 2, 0 in 1-3: 8K pages #0A #0B, 4 5, 0 1
    EXPECT_TRUE(Holds("MMU0 == 0FF && MMU1 == 0FF && MMU2 == 0A && MMU3 == 0B && MMU4 == 4 && MMU5 == 5 && "
                      "MMU6 == 0 && MMU7 == 1"));
    EXPECT_TRUE(Holds("ROMPG0 == ROMPG && ROMPG != 0FF && ROMPG1 == 0FF && ROMPG2 == 0FF && ROMPG3 == 0FF"));
    EXPECT_EQ(Compile("nr(#57) == mmu7 && p1ffd")->Canonical(), "(NR(#57) == MMU7) && P1FFD");

    // No machine: NR(x), the beam and the copper are errors, which the dry run of a compile reports
    std::string why;
    EXPECT_EQ(Compile("NR(7) == 0")->Evaluate(Context(), &why), Result::Error);
    EXPECT_EQ(why, "no NR here");
    EXPECT_EQ(Compile("HC == 0")->Evaluate(Context(), &why), Result::Error);
    EXPECT_EQ(why, "no HC here");
    std::string error;
    const BreakpointConditionContext dryRun = Context();
    EXPECT_FALSE(BreakpointConditionProgram::Compile("CPC == 0", BRK_COND_ANY, nullptr, error,
                                                     BreakpointConditionDialect::Unreal, &dryRun));
    EXPECT_NE(error.find("no CPC here"), std::string::npos) << error;
    // Another CPU's breakpoint has none of the machine's
    EXPECT_NE(Refused("NR(7) == 0", BRK_COND_ANY | BRK_COND_TARGET).find("the machine's"), std::string::npos);
    EXPECT_NE(Refused("DFFD == 0", BRK_COND_ANY | BRK_COND_TARGET).find("the machine's"), std::string::npos);
    EXPECT_NE(Refused("VC == 0", BRK_COND_ANY | BRK_COND_TARGET).find("the machine's"), std::string::npos);

    FakeConditionMachine machine;
    machine.regs[0x57] = 0x21;
    machine.line = 0x105;
    machine.hc = 300;
    machine.copperPc = 17;
    _machine = &machine;
    EXPECT_TRUE(Holds("NR(57) == 21 && NR(50 + 7) == 21 && VC == 105 && HC == 300d && CPC == 17d"));
    EXPECT_EQ(Compile("NR(100) == 0")->Evaluate(Context(), &why), Result::Error);
    EXPECT_EQ(why, "NR register outside 0..#FF");
}
