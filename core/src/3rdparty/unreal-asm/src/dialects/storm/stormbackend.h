#pragma once

// The STORM backend (asm-synchronizer.md §15.2, part B, Z3): the IR written as STORM 1.0beta / 1.3 source. STORM
// computes in 16-bit unsigned words with its own priorities and postfix unary operators, gives 1 for a true comparison,
// keeps labels of up to 7 characters, a line of up to 40 characters and no macros, conditions or blocks; only one line
// repeats (".n") (research-storm-to-sjasmplus.md §2). Macros are expanded, WHILE and DUP of several lines unrolled,
// conditions decided, names assigned with DEFL / "=" written as their values (the RetroWriter core).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class StormBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "storm"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
};
}  // namespace unrealasm::dialects
