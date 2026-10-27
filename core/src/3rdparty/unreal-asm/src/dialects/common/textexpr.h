#pragma once

// Shared pieces of the text-source frontends (pasmo, z80asm, zasm, FantASM ...): the string and line helpers, a
// table-driven expression parser whose operator priorities and number spellings come from a per-dialect ExprRules, and
// the classification of an instruction operand. The older frontends (sjasmplus, asm80 ...) keep their own copies.

#include <string>
#include <string_view>
#include <vector>

#include "unrealasm/diagnostics.h"
#include "unrealasm/ir.h"

namespace unrealasm::dialects::text
{
struct Failure
{
    std::string reason;
};

struct BinaryOp
{
    const char* text;   ///< symbol ("<<") or word ("shl", matched in any case and only as a whole word)
    ir::Op op;
    int priority;
};

struct UnaryOp
{
    const char* text;
    ir::Op op;
    int priority;   ///< the operand is parsed at this priority: tighter-binding operators are part of it
};

struct ExprRules
{
    std::vector<BinaryOp> binary;
    std::vector<UnaryOp> unary;
    // Number spellings
    bool dollarHex = false;     ///< $FF
    bool hashHex = false;       ///< #FF
    bool ampersandBase = false; ///< &H1F &O17 &X1F &1F (hexadecimal)
    bool percentBinary = false; ///< %0101
    bool zeroX = true;          ///< 0x1F
    bool zeroB = false;         ///< 0b0101
    bool suffixes = true;       ///< 1Fh 101b 17o 17q 12d (the first character a digit)
    bool suffixOctalDecimal = true;   ///< the o / q / d suffixes besides h and b
    bool atBinary = false;      ///< @0101
    bool atOctal = false;       ///< @17 (rasm); a name may still start with @ when a letter follows
    bool doubleQuoteIsNumber = true;  ///< "A" is a character constant like 'A'
    bool twoCharsLowFirst = false;     ///< 'LH' is H * 256 + L (zmac); the other dialects read the first character as the high byte
    std::string currentWord;    ///< a name that means the location counter (z80asm ASMPC), matched in any case
    bool dollarInDigits = false;///< $ signs between the digits are ignored
    bool backslashEscapes = true;   ///< "..." strings read C escapes
    bool escapesInSingleQuotes = false;   ///< '...' reads them too (z80asm); otherwise '' is an apostrophe and nothing else is special
    // Names
    bool dotInNames = true, atInNames = true, questionInNames = true, dollarInNames = false;
    std::string currentAddress = "$";   ///< the symbol of the location counter
    std::vector<std::pair<std::string, ir::Op>> functions;   ///< name(expr) functions of one argument (zasm hi / lo)
    bool stringLastCharOps = false;     ///< DEFM "text" + 0x80: an operator applies to the last character (zasm)
    bool hashImmediate = false;         ///< #N marks an immediate operand (ld a,#5), not a hexadecimal number
    bool bracketIndirect = false;       ///< [hl] / [ix+d] / [nn] mean the same as the parenthesized forms
    bool definedOperator = false;       ///< DEFINED name -> ir::Op::Exists
};

bool IsNameStart(char c, const ExprRules& rules);
bool IsNameChar(char c, const ExprRules& rules);
std::string Trim(std::string_view text);
std::string Lower(std::string_view text);
std::string Upper(std::string_view text);
/// The text with the inside of every string replaced by 'x' (quotes kept)
std::string Mask(std::string_view t, bool escapesInSingleQuotes = false);
/// Splits at `separator` outside strings, parentheses, braces and brackets
std::vector<std::string> Split(std::string_view text, char separator, bool escapesInSingleQuotes = false);
/// The characters of a string literal starting at t[i] (a quote); i moves past it
std::string StringLiteral(std::string_view t, size_t& i, const ExprRules& rules);

/// An expression; what the parser cannot read stays as its text (Expr::Kind::Raw) with an Info diagnostic
ir::Expr ParseExpression(std::string_view text, const ExprRules& rules, uint32_t line, Diagnostics& diagnostics);

/// An operand of an instruction (registers, conditions, (hl), (ix+d), (nn), nn)
ir::Operand InstructionOperand(const std::string& text, bool conditionAllowed, bool flagAllowed, const ExprRules& rules, uint32_t line,
                               Diagnostics& diagnostics);
/// DB / DW items: strings of more than one character become String operands, the rest expressions
std::vector<ir::Operand> DataOperands(const std::string& rest, const ExprRules& rules, uint32_t line, Diagnostics& diagnostics);
}  // namespace unrealasm::dialects::text
