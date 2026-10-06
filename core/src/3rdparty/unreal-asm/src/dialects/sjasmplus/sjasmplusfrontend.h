#pragma once

// sjasmplus -> IR (sjasmplus 1.23 documentation): labels from column 0 (an optional colon), statements after blanks
// and between ':' separators, C-like operator priorities, case-insensitive keywords, named macro parameters,
// DUP / REPT, WHILE, DISP / PHASE, numbers as #FF $FF 0xFF 0FFh %101 0b101. Directives the IR has no kind for
// (DEVICE, SAVEBIN, MODULE, IFDEF ...) are kept as text, which the sjasmplus backend writes back.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class SjasmplusFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "sjasmplus"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
