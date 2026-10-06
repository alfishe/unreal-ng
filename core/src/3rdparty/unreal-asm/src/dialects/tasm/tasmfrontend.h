#pragma once

// TASM -> IR (research-tasm.md for the format; the TASM 4.0 description in ZX Format #3 and the TASM 4.12 article in
// Scenergy #1 for the syntax): one statement per line, the label in column 0, expressions left to right without
// priorities on 16-bit words, postfix operators ^ (swap bytes) { (high byte) } (low byte) and in 4.0 [ ] (16-bit
// rotations), PHASE / UNPHASE, INCLUDE / INCBIN without quotes, PUSH / POP with several registers. TASM 4.12 adds
// .IF (true when the value is 0) / .ELSE / .ENDIF, .LOCAL with ...labels, DEFMAC / ENDMAC with \0..\9 and the
// \c \n \s \r operators, DISPLAY and [address] memory reads. The version comes from the document (the tasm codec
// sets it); a text file without one is read as 4.12, whose syntax is a superset except for [ ].

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class TasmFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "tasm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
