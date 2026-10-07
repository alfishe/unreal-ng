#pragma once

// ZEUS -> IR (research-zeus.md for the format; the 1983 manual, section 5, and the ZEUS v7.E help for the language;
// research-zeus-to-sjasmplus.md for what the assembler was seen to do): statements separated by ":", each with its
// own optional label (a first word that is no keyword), upper-case keywords only, expressions strictly left to right
// on 16-bit words (+ - & ! in every version, * / and %binary numbers from ZEUS 1.1 / v7.E on), "c character
// literals, DEFM /text/ with any delimiter, conditions V / NV for PE / PO, DISP as an offset from ORG (code put at
// ORG+DISP, run at ORG), ENT (an entry point, no code), INCLUDE / PLACE (PHT, v7.E) and INCBIN (GG).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class ZeusFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "zeus"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
