#pragma once

// Z80 facts shared by the dialect plugins: the instruction set (undocumented forms included), register and condition
// names, how many operands an instruction takes (to split ALASM / STORM multi-operand lines).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "unrealasm/ir.h"

namespace unrealasm::dialects::z80
{
/// A Z80 instruction mnemonic in lower case (ld, sli, inf ...)
bool IsMnemonic(std::string_view lower);
/// A mnemonic only the ZX Spectrum Next (Z80N) has: swapnib, mirror, test, bsla ... ldirx (ADD, PUSH and JP take extra forms
/// there, but keep their Z80 names and are IsMnemonic)
bool IsZ80nMnemonic(std::string_view lower);
/// A sjasmplus directive line (DEVICE ZXSPECTRUMNEXT, OPT --zxnext[=cspect]) that turns the Z80N instructions on
bool EnablesZ80n(std::string_view directive);
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
/// Upper-case hex digits of a value, at least `digits` of them (zero-padded)
std::string HexDigits(uint64_t value, int digits);
/// A 16-bit product as Primus 2.9 and ZAsm 3.15 make it: by the multiplier's bits from the top (r*2, +multiplicand),
/// stopping at the first carry out of 16 bits with the value so far (both measured on 84-100 products)
uint32_t StopAtCarryProduct(uint32_t multiplicand, uint32_t multiplier);
/// The value of an expression of numbers and names `symbol` knows, in sjasmplus' and z80asm's 32-bit arithmetic (+ - *
/// & | ^ << ~ and unary minus; anything else unknown)
std::optional<int64_t> ConstantValue(const ir::Expr& e, const std::function<std::optional<int64_t>(const std::string&)>& symbol);

/// A Z80N instruction's bytes: ED, `code`, then `layout`: 'n' nothing, 'b' a byte (operand 0), 'B' two bytes (operands 0
/// and 1), 'w' a word low byte first (the last operand), 'W' a word high byte first (PUSH nn)
struct Z80nForm
{
    int code = 0;
    char layout = 'n';
};
/// The Z80N form of an instruction (NEXTREG r,n; SWAPNIB; ADD HL,A; PUSH nn ...); none for a Z80 one
std::optional<Z80nForm> Z80nEncoding(const ir::Statement& s);
}  // namespace unrealasm::dialects::z80
