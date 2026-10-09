#pragma once

// PROMETHEUS (Proxima, 1990-1993) -> IR. The rules come from its manual and the annotated reconstruction of the
// program (github.com/oldcompcz/prometheus, "The Liver of PROMETHEUS", chapters 22 and 27; research-prometheus.md):
// the label in column 0 (names in capitals: letters, digits, _), mnemonics and registers in any case (HX LX HY LY the
// index halves, SLIA = SLI); expressions strictly left to right on 16-bit words with + - * / ? (unsigned division and
// remainder), a sign in front of a term, # hex, % binary, "A" / "AB" characters, $; ORG (both addresses), PUT (where
// the bytes go, the address unchanged), EQU, ENT (the RUN address), DEFB, DEFM "text", DEFS n (a hole), DEFW.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class PrometheusFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "prometheus"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
