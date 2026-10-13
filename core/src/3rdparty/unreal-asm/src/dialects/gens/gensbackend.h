#pragma once

// The GENS backend (asm-synchronizer.md §15.2, part B, Z4): the IR written as GENS4 (HiSoft Devpac) source. GENS
// computes strictly left to right in 16-bit two's complement words with + - * / ? & @ ! and no parentheses, compares
// names on their first 6 characters with case, has IF / ELSE / END but no repeat or displacement and none of the
// undocumented instructions (research-gens-to-sjasmplus.md §2). Expressions are chains (ChainWriter), macros expanded,
// repeats unrolled, the undocumented instructions written as bytes.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class GensBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "gens"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
};
}  // namespace unrealasm::dialects
