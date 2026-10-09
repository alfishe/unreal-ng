#pragma once

// ASM80 / Asm80Win 2.02 (Copper Feet) -> IR. The rules come from its manual (Asm80.txt) and its own source
// (Asm80win.cpp, the same package), checked against asm80win.exe (research-asm80-to-sjasmplus.md): the label in
// column 0 (letters A-z, then letters, digits, $ _ #; 16 characters count; case matters), mnemonics, registers and
// conditions in any case; expressions strictly left to right on a 32-bit accumulator of unsigned 16-bit terms with
// + - * / % & | ^ (no parentheses, a sign only in front), a product cut to 16 bits, signed division, # hex, % binary,
// "c" characters, $; an expression naming an undefined symbol is 0. ORG, EQU, DEFB / DEFW (? = any value), DEFS n,b,
// DEFM "text", DISP / ENDD, ENT, IF expr / IF a=b (true on 0 / on equal) / ELSE / ENDIF, NAME MAC ... ENDM with
// parameters =0..=9 passed by value; keys in column 0: *F file (include), *B file[,start[,length]] (binary), *Pn
// (the page at #C000), the others only shape the listing and the output files.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class Asm80Frontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "asm80"; }
    FrontendResult Parse(const SourceDocument& source) const override;
    /// The labels of the other files take part in the 16-character match (a label defined in an included file)
    FrontendResult ParseInProject(const SourceDocument& source, const std::vector<const SourceDocument*>& project) const override;
};
}  // namespace unrealasm::dialects
