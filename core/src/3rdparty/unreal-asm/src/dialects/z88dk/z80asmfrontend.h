#pragma once

// z88dk's z80asm 2.3 source -> IR (rules from its scanner, parser grammar and the preprocessor, checked against the
// assembler and the cases of its test suite: tools/verification/unreal-asm/checks/z80asmcheck.py): labels as NAME: or
// .NAME (a bare name in column 0 is an opcode, a macro call, or the left side of EQU / = / DEFL / MACRO), C-style
// expressions on 32-bit words (** and ?: have no IR counterpart and stay text), numbers 1Fh $1F 0x1F 101b %101 @101 0b101,
// 'c' characters, "..." strings with C escapes, ASMPC for the location counter; DEFB / DEFW / DEFS, DEFC / EQU / =,
// ORG, INCLUDE / INCBIN / BINARY, IF / IFDEF / IFNDEF / ELIF / ELSE / ENDIF (also written #if ...), MACRO name a b /
// name MACRO a / name: MACRO a, REPT n ... ENDR / ENDM, LOCAL. SECTION / MODULE / PUBLIC / EXTERN / GLOBAL / XDEF / XREF /
// LIB / ASSERT / ALIGN / DEFVARS / DEFGROUP and the other linker directives are kept as text and reported by the backend.
// The Z80N mnemonics are read when the document says so (z80asm -mz80n).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class Z80asmFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "z80asm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
