#pragma once

// ALASM -> IR (research-alasm.md for the format; ALASM 5.x help for the syntax): one statement per line, keywords in
// capitals, expressions evaluated left to right without priorities, LOCAL / ENDL label blocks, macros with \0..\9
// parameters, IF0 / IFN, DUP / EDUP, REPEAT / UNTIL0, DISP / ENT, label=expression, the pseudo-instructions EXA, EXD,
// JZ / JNZ / JC / JNC, INF, SLI and multi-operand lines (LD L,0,H,1).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class AlasmFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "alasm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
