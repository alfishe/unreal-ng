#pragma once

// Z80 facts shared by the dialect plugins: the instruction set (undocumented forms included), register and condition
// names, how many operands an instruction takes (to split ALASM / STORM multi-operand lines).

#include <string>
#include <string_view>

namespace unrealasm::dialects::z80
{
/// A Z80 instruction mnemonic in lower case (ld, sli, inf ...)
bool IsMnemonic(std::string_view lower);
/// A register name in lower case, normalized: ixh ixl iyh iyl for the halves (hx lx xh ...), af' with its apostrophe
std::string NormalizeRegister(std::string_view lower);
bool IsRegister(std::string_view normalized);
bool IsCondition(std::string_view lower);
/// Operands one instance of the instruction takes when an ALASM line lists several (LD L,0,H,1 -> 2): 0 = no split
int SplitArity(std::string_view mnemonic);
/// Instructions whose first operand may be a condition
bool TakesCondition(std::string_view mnemonic);
std::string Lower(std::string_view text);
std::string Upper(std::string_view text);
}  // namespace unrealasm::dialects::z80
