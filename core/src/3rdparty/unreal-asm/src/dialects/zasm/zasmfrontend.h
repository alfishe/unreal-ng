#pragma once

// Megatokio's zasm 4 source -> IR (rules from its Documentation folder, checked against the zasm binary and the sources
// of its Test and Examples folders: tools/verification/unreal-asm/checks/zasmcheck.py). Labels start in column 1 (NAME or
// NAME: or NAME::), instructions are indented; directives are written #COMMAND or .COMMAND (the dot optional on the
// pseudo instructions); numbers 123 123d $7B &7B 7Bh 0x7B %101 101b 0b101 and 'a'; hi() / lo() functions; the operator
// priorities of its list (shifts, then & | ^ together, * / %, + -, comparisons, && ||); EQU / = / DEFL / SET / #DEFINE,
// DEFB / DEFW / DEFM / DEFS, #IF / #ELIF / #ELSE / #ENDIF, MACRO / ENDM, REPT / DUP / EDUP, #LOCAL / #ENDLOCAL,
// #INCLUDE, #INSERT, #CODE name, start, size (an ORG), .PHASE / .DEPHASE. #TARGET, #DATA, #ASSERT, .ALIGN ... are kept as text.
// The Z80N mnemonics are read when the document says so (zasm --z80n).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class ZasmFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "zasm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
