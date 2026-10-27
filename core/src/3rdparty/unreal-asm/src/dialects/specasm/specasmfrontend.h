#pragma once

// Specasm source (the .s text that saexport writes and saimport reads; the .x object files of the editor are not text) -> IR.
// Rules from the Specasm manual (docs/specasm.md) and checked against saimport + salink on the sources of
// tools/verification/unreal-asm/checks/specasmcheck.py. One item per line: `.Name` defines a label on a line of its own,
// `.name equ expression`; an instruction or directive; a string line starting with a quote character (" or ' = the text,
// @ or # = a length byte and the text, either closed by the same character or running to the end of the line); `;` comments.
// A leading `=` marks an expression operand (`ld a, =10*2`, `call =target`); the marker is dropped. Numbers are decimal,
// $hex or 'c'; operators - ~ * / % + - << >> and & | ^ (the last three at one priority). DB / DW / DS / ALIGN / ORG / INCLUDE,
// NBRK (NEXTREG 2,8), and every Z80N instruction. Global (upper-case first letter) and local labels share one namespace here.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class SpecasmFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "specasm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
