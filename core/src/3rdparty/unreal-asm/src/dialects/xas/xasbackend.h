#pragma once

// The XAS backend (asm-synchronizer.md §15.2, part B, Z3): the IR written as XAS 4.18-9.10 source. XAS computes left
// to right in 16-bit unsigned words with + - * / ! only, no parentheses and no unary minus, and compares labels on
// their first 7 characters without case (research-xas-to-sjasmplus.md §2). An expression is written as such a chain
// (regrouped where + - * ! allow it; high / low bytes and negation by arithmetic); one that cannot be is written as
// its value, from the source's layout (BackendOptions::labelValues). Conditions whose value is known are decided
// here, so XAS's single block level holds only repeats and the conditions left to it (IFNZ / IFZ, 7.x).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class XasBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "xas"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
};
}  // namespace unrealasm::dialects
