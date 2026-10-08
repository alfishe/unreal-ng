#pragma once

// Roudoudou's rasm source -> IR (rules from its documentation, checked against the rasm binary on the decrunch sources of
// its repository: tools/verification/unreal-asm/checks/dialectcheck.py). Labels with a colon or at the start of a line;
// .name proximity labels (as in sjasmplus) and @name local labels, which inside a MACRO are local to each expansion; numbers
// #FF $FF 0xFF FFh %101 0b101 101b @17 (octal) and 'c'; C-like priorities with AND OR XOR MOD (%%) and hi() / lo(); EQU,
// NAME=expr variables, ORG, DEFB / DEFW / DEFS and synonyms, REPEAT n[,counter[,start]] ... REND, WHILE ... WEND, IF / IFNOT /
// IFDEF / IFNDEF / ELSE / ENDIF, MACRO name (a, b) ... MEND / ENDM, INCLUDE, INCBIN. Rasm computes in floating point and
// resolves EQU as a text alias; both are taken as ordinary integer constants here. SAVE, BANK, BUILDSNA, LIMIT, MODULE, the
// crunched sections and the Amstrad-specific directives are kept as text.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class RasmFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "rasm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
