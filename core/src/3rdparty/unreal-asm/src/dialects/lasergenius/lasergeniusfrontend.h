#pragma once

// Laser Genius (Oasis Software, 1986) -> IR. The rules come from its manual and from Laser Genius 1.04 assembling
// probes in unreal-ng (research-laser-genius.md): one statement a line ("label:" with a colon, the mnemonic, operands,
// "; comment"), a paragraph number in front allowed; names case-sensitive with letters, digits, _ . $; expressions on
// unsigned 16-bit words with C-like precedence except that & | ^ share one level and && || another, comparisons and
// logical operators giving 1, [ ] as parentheses, unary - ! ^ (complement) * (the word at an address); numbers #hex,
// nnnnH, %binary, @octal, "c" and "\13" characters; $ the statement's address, . where its bytes go. ORG sets the
// address, PUT where the bytes go; DB DEFB DEFM, DW DEFW, DS DEFS (zeros), EQU, DL DEFL (redefinable), COND ELSE ENDC,
// name: MACRO \p1,\p2 ... ENDM and \name arguments; *WHILE *ENDW, *REPEAT *UNTIL, *INCLUDE, *PRINT; the other
// directives only steer listing and output.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class LaserGeniusFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "lasergenius"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
