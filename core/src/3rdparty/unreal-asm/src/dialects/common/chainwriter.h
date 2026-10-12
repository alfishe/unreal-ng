#pragma once

// A RetroWriter for targets that compute strictly left to right without parentheses (MASM, GENS; XAS keeps its own):
// an expression is written as a chain in which every operator takes an atom (a number, a name or $), regrouped where
// the operators allow (a+(b+c) as a+b+c, an atom of a commuting operator moved to the right), and as its value where it
// cannot be (research-sjasmplus-to-xas.md §2). A dialect gives its operator symbols and what differs in its arithmetic.

#include "dialects/common/retrowriter.h"

namespace unrealasm::dialects
{
class ChainWriter : public RetroWriter
{
public:
    using RetroWriter::RetroWriter;

protected:
    /// The target's symbol of a binary operator (Add Sub Mul Div Mod And Or Xor), nullptr when it has none
    virtual const char* OperatorSymbol(Op op) const = 0;
    /// What goes between the chain so far and an operator (MASM: "+0" before "@" after a name)
    virtual std::string Guard(const std::string&, const char*) const { return {}; }
    /// The high byte of the value so far (16-bit unsigned division: /256)
    virtual std::string HighOf(const std::string& chain) const { return chain + "/256"; }
    /// The target's division agrees with the source's on these operands (16-bit unsigned: both in 0..#FFFF)
    virtual bool DivisionAgrees(const Expr& a, const Expr& b) const { return InWord(a) && InWord(b); }
    /// A right shift as a division agrees on this operand
    virtual bool ShiftAgrees(const Expr& a) const { return InWord(a); }
    /// The longest expression the target's line takes (0: any); a longer one is written as its value where known
    virtual size_t ExpressionWidth() const { return 0; }
    virtual std::string NumberText(const Expr& e) const;

    std::optional<std::string> Atom(const Expr& e);
    static bool IsAtom(const Expr& e);
    std::optional<std::string> Text(const Expr& e) override;
    std::optional<std::string> Binary(const Expr& e);
    std::optional<std::string> Chained(const Expr& e);
};
}  // namespace unrealasm::dialects
