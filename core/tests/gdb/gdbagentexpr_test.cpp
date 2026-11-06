/// @file gdbagentexpr_test.cpp
/// @brief GDBAgentExpression: gdb's agent-expression bytecode (Z0 conditions, ConditionalBreakpoints+) in the core
/// condition language. Each program runs on a reference interpreter with gdbserver's semantics (64-bit unsigned
/// stack, gdbserver/ax.cc) and as its core program, on random registers and memory: the truths must agree.

#include <gtest/gtest.h>

#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "gdbagentexpr.h"
#include "debugger/breakpoints/breakpointcondition.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{
using Code = std::vector<uint8_t>;

class GDBAgentExpression_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    Z80Registers _cpu{};
    EmulatorState _state{};

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

    void Randomize(std::mt19937& random)
    {
        _cpu.af = static_cast<uint16_t>(random());
        _cpu.bc = static_cast<uint16_t>(random() & 0x0103);
        _cpu.de = static_cast<uint16_t>(random());
        _cpu.hl = static_cast<uint16_t>(random() | 0x8000);
        _cpu.sp = static_cast<uint16_t>(random() | 0x8000);
        _cpu.pc = static_cast<uint16_t>(random() & 0x80FF);
        _cpu.ix = static_cast<uint16_t>(random());
        _cpu.iy = static_cast<uint16_t>(random());
        _cpu.alt.af = static_cast<uint16_t>(random());
        _cpu.i = static_cast<uint8_t>(random());
        _cpu.r_low = static_cast<uint8_t>(random());
        _cpu.r_hi = static_cast<uint8_t>(random() & 0x80);
        _cpu.iff1 = static_cast<uint8_t>(random() & 1);
        _cpu.iff2 = static_cast<uint8_t>(random() & 1);
        _cpu.im = static_cast<uint8_t>(random() % 3);
        _state.p7FFD = static_cast<uint8_t>(random() & 0x1F);
        for (uint16_t a : {_cpu.hl, static_cast<uint16_t>(_cpu.hl + 2), _cpu.sp, static_cast<uint16_t>(_cpu.sp + 1)})
            _memory->DirectWriteToZ80Memory(a, static_cast<uint8_t>(random() & 7));
        if (random() % 4 == 0)
        {
            // the word at SP is PC often enough
            _memory->DirectWriteToZ80Memory(_cpu.sp, static_cast<uint8_t>(_cpu.pc));
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(_cpu.sp + 1), static_cast<uint8_t>(_cpu.pc >> 8));
        }
    }

    uint64_t Reg(uint16_t n) const
    {
        const uint16_t pairs[] = {_cpu.af, _cpu.bc, _cpu.de, _cpu.hl, _cpu.sp, _cpu.pc,
                                  _cpu.ix, _cpu.iy, _cpu.alt.af, _cpu.alt.bc, _cpu.alt.de, _cpu.alt.hl};
        if (n < 12)
            return pairs[n];
        switch (n)
        {
            case 12: return (_cpu.i << 8) | (_cpu.r_low & 0x7F) | (_cpu.r_hi & 0x80);
            case 13: return (_cpu.iff1 ? 1 : 0) | (_cpu.iff2 ? 2 : 0);
            case 14: return _cpu.im;
            default: return _state.p7FFD;
        }
    }

    uint64_t Mem(uint64_t address, int bytes) const
    {
        uint64_t v = 0;
        for (int i = bytes - 1; i >= 0; i--)
            v = (v << 8) | _memory->DirectReadFromZ80Memory(static_cast<uint16_t>(address + static_cast<uint64_t>(i)));
        return v;
    }

    /// gdbserver's interpreter (ax.cc gdb_eval_agent_expr) for the opcodes the translation takes
    std::optional<uint64_t> Reference(const Code& code) const
    {
        std::vector<uint64_t> s;
        size_t pc = 0;
        auto u = [&](size_t size) {
            uint64_t v = 0;
            for (size_t i = 0; i < size; i++)
                v = (v << 8) | code[pc + i];
            pc += size;
            return v;
        };
        while (pc < code.size())
        {
            const uint8_t op = code[pc++];
            uint64_t b = 0;
            if ((op >= 0x02 && op <= 0x0B) || (op >= 0x0F && op <= 0x11) || op == 0x13 || op == 0x14 || op == 0x15)
            {
                b = s.back();
                s.pop_back();
            }
            uint64_t& a = s.empty() ? b : s.back();
            switch (op)
            {
                case 0x02: a += b; break;
                case 0x03: a -= b; break;
                case 0x04: a *= b; break;
                case 0x05: a = static_cast<uint64_t>(static_cast<int64_t>(a) / static_cast<int64_t>(b)); break;
                case 0x06: a /= b; break;
                case 0x07: a = static_cast<uint64_t>(static_cast<int64_t>(a) % static_cast<int64_t>(b)); break;
                case 0x08: a %= b; break;
                case 0x09: a <<= b; break;
                case 0x0A: a = static_cast<uint64_t>(static_cast<int64_t>(a) >> b); break;
                case 0x0B: a >>= b; break;
                case 0x0E: a = a == 0 ? 1 : 0; break;
                case 0x0F: a &= b; break;
                case 0x10: a |= b; break;
                case 0x11: a ^= b; break;
                case 0x12: a = ~a; break;
                case 0x13: a = a == b ? 1 : 0; break;
                case 0x14: a = static_cast<int64_t>(a) < static_cast<int64_t>(b) ? 1 : 0; break;
                case 0x15: a = a < b ? 1 : 0; break;
                case 0x16:
                {
                    const uint64_t n = u(1);
                    if (n < 64)
                    {
                        const uint64_t sign = uint64_t{1} << (n - 1);
                        a = ((a & ((uint64_t{1} << n) - 1)) ^ sign) - sign;
                    }
                    break;
                }
                case 0x2A:
                {
                    const uint64_t n = u(1);
                    if (n < 64)
                        a &= (uint64_t{1} << n) - 1;
                    break;
                }
                case 0x17: a = Mem(a, 1); break;
                case 0x18: a = Mem(a, 2); break;
                case 0x19: a = Mem(a, 4); break;
                case 0x20:
                {
                    const uint64_t to = u(2);
                    const uint64_t c = s.back();
                    s.pop_back();
                    if (c != 0)
                        pc = static_cast<size_t>(to);
                    break;
                }
                case 0x21: pc = static_cast<size_t>(u(2)); break;
                case 0x22: s.push_back(u(1)); break;
                case 0x23: s.push_back(u(2)); break;
                case 0x24: s.push_back(u(4)); break;
                case 0x25: s.push_back(u(8)); break;
                case 0x26: s.push_back(Reg(static_cast<uint16_t>(u(2)))); break;
                case 0x27: return s.back();
                case 0x28: s.push_back(s.back()); break;
                case 0x29: s.pop_back(); break;
                case 0x2B: std::swap(s[s.size() - 1], s[s.size() - 2]); break;
                case 0x32:
                {
                    const uint64_t n = u(1);
                    s.push_back(s[s.size() - 1 - n]);
                    break;
                }
                case 0x33:
                {
                    const uint64_t c = s[s.size() - 1];
                    s[s.size() - 1] = s[s.size() - 2];
                    s[s.size() - 2] = s[s.size() - 3];
                    s[s.size() - 3] = c;
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    BreakpointConditionPtr Translate(const Code& code)
    {
        std::string text;
        std::string reason;
        EXPECT_TRUE(GDBAgentExpression::toCondition(code, text, reason)) << reason;
        std::string error;
        BreakpointConditionPtr program = BreakpointConditionProgram::Compile(text, BRK_COND_EXEC, nullptr, error);
        EXPECT_TRUE(program) << text << ": " << error;
        return program;
    }

    std::string Refused(const Code& code)
    {
        std::string text;
        std::string reason;
        EXPECT_FALSE(GDBAgentExpression::toCondition(code, text, reason)) << text;
        return reason;
    }
};
} // namespace

TEST_F(GDBAgentExpression_Test, TranslationAgreesWithTheAgent)
{
    const Code programs[] = {
        // $pc == 0x8000
        {0x26, 0x00, 0x05, 0x23, 0x80, 0x00, 0x13, 0x27},
        // (short)$de < 0
        {0x26, 0x00, 0x02, 0x16, 0x10, 0x22, 0x00, 0x14, 0x27},
        // $a < $f >> 1 (the bytes of af through shifts and zero_ext)
        {0x26, 0x00, 0x00, 0x22, 0x08, 0x0B, 0x2A, 0x08, 0x26, 0x00, 0x00, 0x2A, 0x08, 0x22, 0x01, 0x0B, 0x14, 0x27},
        // $hl > 100 && *(char *)$hl == 5: if_goto / goto
        {0x22, 0x64, 0x26, 0x00, 0x03, 0x14, 0x20, 0x00, 0x0E, 0x22, 0x00, 0x21, 0x00, 0x17,
         0x26, 0x00, 0x03, 0x17, 0x16, 0x08, 0x22, 0x05, 0x13, 0x27},
        // $bc == 1 || $de < 0x4000: dup / if_goto / pop
        {0x26, 0x00, 0x01, 0x22, 0x01, 0x13, 0x28, 0x20, 0x00, 0x12, 0x29, 0x26, 0x00, 0x02, 0x23, 0x40, 0x00, 0x15,
         0x27},
        // (unsigned)$sp < 0x9000
        {0x26, 0x00, 0x04, 0x2A, 0x10, 0x23, 0x90, 0x00, 0x15, 0x27},
        // (($hl + $de) * 2 - 1) & 0xFFFF < 0x8000
        {0x26, 0x00, 0x03, 0x26, 0x00, 0x02, 0x02, 0x22, 0x02, 0x04, 0x22, 0x01, 0x03, 0x24, 0x00, 0x00, 0xFF, 0xFF,
         0x0F, 0x23, 0x80, 0x00, 0x15, 0x27},
        // $hl % 7 == 3 (unsigned)
        {0x26, 0x00, 0x03, 0x22, 0x07, 0x08, 0x22, 0x03, 0x13, 0x27},
        // (short)$de / 3 < -100 (signed)
        {0x26, 0x00, 0x02, 0x16, 0x10, 0x22, 0x03, 0x05, 0x25, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x9C, 0x14,
         0x27},
        // (short)$de % -5 == 2 (a negative divisor)
        {0x26, 0x00, 0x02, 0x16, 0x10, 0x25, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x07, 0x22, 0x02, 0x13,
         0x27},
        // *(unsigned short *)$sp == $pc
        {0x26, 0x00, 0x04, 0x18, 0x26, 0x00, 0x05, 0x13, 0x27},
        // *(unsigned long *)$hl < 0x00030000
        {0x26, 0x00, 0x03, 0x19, 0x24, 0x00, 0x03, 0x00, 0x00, 0x15, 0x27},
        // ($ir & 0xFF) > 0x40 (R with its bit 7)
        {0x22, 0x40, 0x26, 0x00, 0x0C, 0x22, 0xFF, 0x0F, 0x14, 0x27},
        // $iff == 3
        {0x26, 0x00, 0x0D, 0x22, 0x03, 0x13, 0x27},
        // $im == 1 && ($p7ffd & 0x10): one if_goto, the false path a constant
        {0x26, 0x00, 0x0E, 0x22, 0x01, 0x13, 0x20, 0x00, 0x0C, 0x22, 0x00, 0x27, 0x26, 0x00, 0x0F, 0x22, 0x10, 0x0F,
         0x27},
        // rot / swap / pick: 0x8000 < $af - 1
        {0x26, 0x00, 0x00, 0x22, 0x01, 0x23, 0x80, 0x00, 0x33, 0x03, 0x2B, 0x32, 0x01, 0x14, 0x27},
        // ((!($de & 1) ^ ($bc & 1)) | (~~$bc & 2)) ^ (($hl << 4) >> 19): bit_not, lsh, rsh_signed of a value that is
        // not negative
        {0x26, 0x00, 0x02, 0x22, 0x01, 0x0F, 0x0E, 0x26, 0x00, 0x01, 0x22, 0x01, 0x0F, 0x11,
         0x26, 0x00, 0x01, 0x12, 0x12, 0x22, 0x02, 0x0F, 0x10, 0x26, 0x00, 0x03, 0x22, 0x04, 0x09, 0x22, 0x13, 0x0A,
         0x11, 0x27},
        // a || chain of three: $bc == 0 || $bc == 1 || $bc == 0x100
        {0x26, 0x00, 0x01, 0x22, 0x00, 0x13, 0x28, 0x20, 0x00, 0x1D, 0x29, 0x26, 0x00, 0x01, 0x22, 0x01, 0x13, 0x28,
         0x20, 0x00, 0x1D, 0x29, 0x26, 0x00, 0x01, 0x23, 0x01, 0x00, 0x13, 0x27},
    };
    std::mt19937 random(40);
    int index = 0;
    for (const Code& code : programs)
    {
        BreakpointConditionPtr program = Translate(code);
        ASSERT_TRUE(program) << "program " << index;
        int truths = 0;
        int falses = 0;
        for (int i = 0; i < 1500; i++)
        {
            Randomize(random);
            const std::optional<uint64_t> reference = Reference(code);
            ASSERT_TRUE(reference) << "program " << index;
            BreakpointConditionContext x;
            x.cpu = &_cpu;
            x.memory = _memory;
            x.state = &_state;
            const BreakpointConditionResult core = program->Evaluate(x);
            ASSERT_NE(core, BreakpointConditionResult::Error) << "program " << index;
            ASSERT_EQ(core == BreakpointConditionResult::True, *reference != 0)
                << "program " << index << ": " << program->Canonical() << " #" << i;
            (*reference != 0 ? truths : falses)++;
        }
        EXPECT_GT(truths, 0) << "program " << index << " never held";
        EXPECT_GT(falses, 0) << "program " << index << " always held";
        index++;
    }
}

TEST_F(GDBAgentExpression_Test, WhatTheCoreCanNotSayIsRefused)
{
    EXPECT_NE(Refused({0x22, 0x01, 0x21, 0x00, 0x00, 0x27}).find("forward"), std::string::npos) << "a loop";
    EXPECT_NE(Refused({0x2C, 0x00, 0x01, 0x27}).find("#2C"), std::string::npos) << "getv: a state variable";
    EXPECT_NE(Refused({0x22, 0x01, 0x01, 0x27}).find("#01"), std::string::npos) << "float";
    EXPECT_NE(Refused({0x0C, 0x27}).find("#0C"), std::string::npos) << "trace";
    EXPECT_NE(Refused({0x26, 0x00, 0x10, 0x27}).find("register 16"), std::string::npos) << "p1ffd";
    EXPECT_NE(Refused({0x26, 0x00, 0x03, 0x26, 0x00, 0x00, 0x05, 0x27}).find("division"), std::string::npos)
        << "a divisor that can be zero: the agent fails";
    EXPECT_NE(Refused({0x26, 0x00, 0x03, 0x22, 0x00, 0x06, 0x27}).find("division"), std::string::npos);
    EXPECT_NE(Refused({0x26, 0x00, 0x03, 0x26, 0x00, 0x00, 0x09, 0x27}).find("0..63"), std::string::npos);
    EXPECT_NE(Refused({0x26, 0x00, 0x03, 0x16, 0x10, 0x22, 0x01, 0x0A, 0x27}).find("negative"), std::string::npos)
        << "a signed shift of a sign-extended value";
    EXPECT_NE(Refused({0x26, 0x00, 0x03, 0x16, 0x10, 0x22, 0x01, 0x15, 0x27}).find("negative"), std::string::npos)
        << "an unsigned compare of a sign-extended value";
    EXPECT_NE(Refused({0x25, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x27}).find("15 hex"), std::string::npos);
    EXPECT_NE(Refused({0x26, 0x00, 0x03}).find("end"), std::string::npos);
    EXPECT_NE(Refused({0x13, 0x27}).find("empty"), std::string::npos);
    EXPECT_NE(Refused({0x23, 0x01}).find("past the end"), std::string::npos);
    EXPECT_FALSE(Refused({}).empty());
}

TEST_F(GDBAgentExpression_Test, ConstantPathsFold)
{
    std::string text;
    std::string reason;
    // const8 1; if_goto 8; const8 0; end; const8 7; end: the jump is always taken
    ASSERT_TRUE(GDBAgentExpression::toCondition({0x22, 0x01, 0x20, 0x00, 0x08, 0x22, 0x00, 0x27, 0x22, 0x07, 0x27},
                                                text, reason))
        << reason;
    EXPECT_EQ(text, "1");
    ASSERT_TRUE(GDBAgentExpression::toCondition({0x22, 0x00, 0x27}, text, reason)) << reason;
    EXPECT_EQ(text, "0");
}

TEST_F(GDBAgentExpression_Test, ConditionListIsTheOrOfItsExpressions)
{
    std::string text;
    std::string reason;
    // X8,<$pc == 0x8000>;X7,<$iff == 3>
    ASSERT_TRUE(GDBAgentExpression::listToCondition("X8,2600052380001327;X7,26000d22031327", text, reason)) << reason;
    std::string error;
    BreakpointConditionPtr program = BreakpointConditionProgram::Compile(text, BRK_COND_EXEC, nullptr, error);
    ASSERT_TRUE(program) << text << ": " << error;
    BreakpointConditionContext x;
    x.cpu = &_cpu;
    _cpu.pc = 0x8000;
    EXPECT_EQ(program->Evaluate(x), BreakpointConditionResult::True);
    _cpu.pc = 0x8001;
    EXPECT_EQ(program->Evaluate(x), BreakpointConditionResult::False);
    _cpu.iff1 = _cpu.iff2 = 1;
    EXPECT_EQ(program->Evaluate(x), BreakpointConditionResult::True);

    EXPECT_FALSE(GDBAgentExpression::listToCondition("X8,26000523800013", text, reason)) << "shorter than its length";
    EXPECT_FALSE(GDBAgentExpression::listToCondition("cmds:X2,0127", text, reason));
    EXPECT_FALSE(GDBAgentExpression::listToCondition("X3,2600zz", text, reason));
    EXPECT_FALSE(GDBAgentExpression::listToCondition("", text, reason));
}
