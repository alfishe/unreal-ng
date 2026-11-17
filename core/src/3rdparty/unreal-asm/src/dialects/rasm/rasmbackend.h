#pragma once

// The rasm backend: the IR written as rasm 3.x source (research-sjasmplus-to-rasm.md). rasm computes C-like expressions
// in doubles (its `\` the integer division, its comparisons 1 when true, all of them on one priority level), takes
// labels in any case as the same name, and computes IF, DEFS and REPEAT counts in the order of the text (a label
// defined further down is unknown there). The RetroWriter core walks the source: macros expanded, WHILE and repeats
// with labels unrolled, the conditions whose value the source's layout gives decided, the names assigned with DEFL /
// "=" written as their values, a later label in an IF, ORG or DEFS as its value; what remains is written as rasm's own
// IF / ELSE / ENDIF, REPEAT / REND and ORG logical,physical.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class RasmBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "rasm"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
};
}  // namespace unrealasm::dialects
