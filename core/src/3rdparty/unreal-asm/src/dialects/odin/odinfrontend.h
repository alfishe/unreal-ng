#pragma once

// Odin source (the text of an .odn document, as OdinCodec spells the tokens out) -> IR. Odin is Matt Davies' editor and
// assembler for the ZX Spectrum Next: its sources are close to sjasmplus (its own test suite builds the same program with both):
// labels with an optional colon, `;` comments, $ for the location counter, $12 hexadecimal, %101 binary, 'c', C operators and
// the words << >> MOD, ORG, EQU, DB / DEFB, DW / DEFW, DS / DEFS, DZ / DEFZ (a zero-terminated string), BIN / INCBIN, LOAD /
// INCLUDE, and every Z80N mnemonic. OPT, SAVE, TAB, ENT and the other editor / debugger directives are kept as text.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class OdinFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "odin"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
