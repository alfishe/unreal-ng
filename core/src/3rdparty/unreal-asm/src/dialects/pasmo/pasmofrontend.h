#pragma once

// pasmo 0.5.5 source -> IR. The rules come from its manual (pasmodoc.html) and are checked against the pasmo binary
// (tools/verification/unreal-asm/checks/pasmocheck.py): labels at the start of a line (a colon is optional), reserved
// mnemonics / registers / directives in any case, 16-bit unsigned arithmetic with a true of FFFF, the operator
// priorities of its table (unary operators below the comparisons, HIGH / LOW lowest), the number spellings $FF #FF
// &H1F %101 0x1F 1Fh 101b 17o, "..." strings with C escapes and '...' strings with '' for an apostrophe. MACRO in both
// spellings, REPT, IF / IFDEF / IFNDEF / ELSE / ENDIF (ENDM closes the IFs open inside its block), INCLUDE / INCBIN with
// pasmo's file name rules, END with an entry point. PROC / ENDP / LOCAL / PUBLIC / IRP / EXITM / .SHIFT / .ERROR /
// .WARNING and the ## operator have no IR counterpart: they are kept as text and reported by the backend.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class PasmoFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "pasmo"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
