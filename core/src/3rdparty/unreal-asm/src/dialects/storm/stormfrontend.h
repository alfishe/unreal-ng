#pragma once

// STORM -> IR (research-storm.md for the format; STORM 1.3's help, the edition of its ZX Format #7 description, for
// the syntax): any number of statements per line separated by ":", any number of operands per instruction (LD
// HL,1,DE,2 is two loads, PUSH BC,DE two pushes, JR NZ,L1,L2 a JR NZ and a JR), ".n" in the label field repeats the
// line n times (0 = 256), expressions with priorities on 16-bit words, every unary operator postfix ([ high, ] low,
// ^ and ` round to a multiple of 256, ' times 256, ~ negate, @ logical not), comparisons that give 0 or 1, a whole
// operand in parentheses is memory (0+(x) is a value), ORG run[,place], DS count,pattern... (count bytes of the
// pattern repeated), "@AEDF" texts of hex bytes, INCB / INCL with several names, built-in macros (LD HL,BC; ADD DE,HL;
// EX HL,DE; OUT (n); IN r; EXA).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class StormFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "storm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
