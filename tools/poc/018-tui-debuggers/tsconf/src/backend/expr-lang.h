// expr-lang.h - conditional breakpoint expression language (TDD-DBG-01 §10.2).
//
// Expressions compile to RPN when added and evaluate with 32-bit unsigned
// arithmetic; any non-zero result breaks. Precedence (high->low):
//   1  ! ~ M(x)  and the legacy a->b form (== M(a+b))
//   2  * % /     (division/modulo by 0 leaves the left operand)
//   3  + -
//   4  >> <<
//   5  > < = == >= <= !=
//   6  &   7 ^   8 |   9 &&   10 ||
// Parentheses group. Operands: Z80 registers (primed included), PC SP IX IY
// I R, FD (last #7FFD), OUT/IN/VAL (port access of this instruction or
// FFFFFFFF), DOS, hex numbers (must start with a digit), 'c' constants.
// The source text is upper-cased except any character immediately followed
// by a quote, so 'c' survives but alternate registers must be typed in
// upper case (a lower-case hl' fails the register lookup: original quirk).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "model/model.h"

namespace dbg {

// Machine state snapshot the evaluator sees. Filled by the backend before
// each evaluation (instruction boundary, §10.2).
struct EvalContext {
    const Z80Regs* regs = nullptr;
    const std::vector<uint8_t>* mem = nullptr;  // live 16-bit map, 64K
    uint32_t fd = 0;         // last value written to #7FFD
    uint32_t outPort = 0xFFFFFFFFu;  // port of the OUT by this instruction
    uint32_t inPort = 0xFFFFFFFFu;   // port of the IN
    uint32_t val = 0;        // value written/read by that access
    bool dos = false;        // TR-DOS ports active
    // Fork extensions (TDD-DBG-02 §10): unused by the classic variant.
    uint32_t rd = 0xFFFFFFFFu;
    uint32_t wr = 0xFFFFFFFFu;
    uint32_t mdt = 0;
    uint32_t page[4] = {0, 0, 0, 0};
};

namespace detail { struct ExprParser; }

class CompiledExpr {
public:
    // Compile "text" or return a human-readable error. On success the RPN
    // program is validated by a test evaluation against a zeroed context.
    static bool Compile(const std::string& text, CompiledExpr* out, std::string* error);

    // Evaluate; returns the raw 32-bit result (non-zero = break).
    uint32_t Eval(const EvalContext& ctx) const;

    // Fully parenthesised decompiled form (§10.2): "(A+B)", "!(X)", "M(X)".
    std::string Decompile() const;

    const std::string& Source() const { return source_; }

private:
    friend struct detail::ExprParser;

    enum class Tok : uint8_t {
        Num,      // push immediate
        RegA, RegF, RegB, RegC, RegD, RegE, RegH, RegL,
        RegAf, RegBc, RegDe, RegHl,
        RegAf2, RegBc2, RegDe2, RegHl2,
        RegA2, RegF2, RegB2, RegC2, RegD2, RegE2, RegH2, RegL2,
        RegPc, RegSp, RegIx, RegIy, RegI, RegR,
        VarFd, VarOut, VarIn, VarVal, VarDos,
        VarRd, VarWr, VarMdt, VarPg0, VarPg1, VarPg2, VarPg3,
        Mem,     // pop addr, push M(addr)
        Not,     // !
        BitNot,  // ~
        Mul, Div, Mod,
        Add, Sub,
        Shr, Shl,
        Lt, Gt, Le, Ge, Eq, Ne,
        And, Xor, Or,
        LAnd, LOr,
    };

    std::vector<std::pair<Tok, uint32_t>> rpn_;
    std::string source_;
};

}  // namespace dbg
