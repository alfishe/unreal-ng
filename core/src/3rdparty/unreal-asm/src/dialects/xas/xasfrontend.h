#pragma once

// XAS -> IR (research-xas.md for the format, research-xas-to-sjasmplus.md for the language: XAS 7.447 and 4.18 checked
// in the emulator, the help files of 4.18 / 7.43): one statement per line, the label in column 0, labels compared on
// their first 7 characters without case, expressions left to right without priorities on unsigned 16-bit words
// (+ - * / and ! for XOR), postfix &L / &H (low / high byte) and 'L / 'R (16-bit rotations by one bit), no parentheses
// and no unary minus. Directives: ORG, EQU, DB / DW / DM / DS (DEFB / DEFW / DEFM / DEFS in 4.x / 5.x; DS n,w fills
// with the word w), a string in the command place (DM), WORK (a displacement that ORG keeps), ENT (the start address
// for Run), LTEXT / LCODE (LOADTEXT / LOADCODE), !ASSM n / !ASSM !ON / !ASSM !OFF and IFNZ / IFZ up to !CONT (one
// level: a block opened inside another is ignored, as XAS does), PUSH / POP with several registers, EX AF,AF, (IX) for
// (IX+0), IN / OUT with a port written without parentheses. 9.07m's .ASM / .END / .ON / .OFF / LTXT / LCOD are the
// same commands.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class XasFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "xas"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
