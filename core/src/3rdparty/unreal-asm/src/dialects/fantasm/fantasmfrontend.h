#pragma once

// FantASM 1.1 source -> IR (rules from its manual and the sources of its tests, which its own test script builds with
// sjasmplus --nofakes --zxnext=cspect as well: tools/verification/unreal-asm/checks/fantasmcheck.py). Labels start with a
// letter and may end with a colon; a name starting with a period is local to the stretch up to the next ordinary label (as
// in sjasmplus); CONST = expr or CONST EQU expr; ; and // comments; a colon separates several instructions on a line;
// numbers $12EF 0x12EF 012EFh %1010 01010b 'a'; ORG, DB / BYTE, DW / WORD, DS, DH / HEX, DZ, INCLUDE, BINARY / INCBIN,
// IF / IFDEF / IFNDEF / ELSE / ENDIF (also #if ...), MACRO name a,b ... ENDM. STRUCT / ENUM blocks, !opt, #pragma, !message
// and GLOBAL have no counterpart and are kept as text. The Z80N mnemonics are read when the document says so (-N).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class FantasmFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "fantasm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
