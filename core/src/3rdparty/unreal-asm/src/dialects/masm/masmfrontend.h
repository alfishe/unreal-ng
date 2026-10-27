#pragma once

// MASM -> IR (research-masm-to-sjasmplus.md; MASM 1.1's help MASMHELP, 1.3's help, and MASM 1.1's own source, whose
// assembler is the reference): TASM 3's language with MASM's changes. One statement per line, the label in column 0
// (a trailing colon ignored), keywords only in capitals (the editor tokenizes nothing else: "ld a,b" is text),
// expressions left to right without priorities on 16-bit words with + - * / & | and @ (XOR), numbers #FF %101 255
// 0FFH "c", $; DEFB / DB with strings, DEFS / DS count[,bytes...] (the list repeats count times), DEFW / DW, EQU, ORG,
// PHASE / UNPHASE, INCLUDE / INCBIN with a bare file name, BEGIN n ... END (the block n times), and the built-in
// macro commands DOWN rr, UP rr, SYSTEM, SYSTEM+, STOPKEY [address] written out as the 1.1 assembler writes them.
// MASM 2.0 / 3.0's MAC / ENDM / IF / ELSE / ENDIF / BANK / BORDER / CLS have no documentation and no real source:
// kept as text and reported.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class MasmFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "masm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
