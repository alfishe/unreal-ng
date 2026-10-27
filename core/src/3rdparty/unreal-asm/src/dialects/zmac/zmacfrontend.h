#pragma once

// George Phillips' zmac source -> IR (rules from its documentation and checked against the zmac binary on the sources of
// testdata/zmac: tools/verification/unreal-asm/checks/dialectcheck.py). Labels with or without a colon, indentation
// unimportant (a bare name in column 1 is a label unless it is a mnemonic or pseudo-op); a period may precede any pseudo-op; a
// backslash separates statements; C operators with C's priorities (and the word forms mod shl shr and or xor eq ne lt gt le ge
// not high low); numbers 1Fh $1F 0x1F 101b 17o 17q 12d, 'a' and a two-character 'LH' as a word; DEFB / DEFW / DEFS and their
// synonyms, EQU, DEFL / SET / = ... ORG, PHASE / DEPHASE, END, IF / IFDEF / IFNDEF / IFEQ / IFNE / IFLT / IFGT / ELSE /
// ENDIF (COND / ENDC), INCLUDE / READ / IMPORT / MACLIB, INCBIN, MACRO / ENDM / REPT. The --mras and --zmac modes, the
// relocatable-output directives, the `++` and `+=` forms and the listing controls are not read.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class ZmacFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "zmac"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
