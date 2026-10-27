#pragma once

// The ZEUS backend (asm-synchronizer.md §15.2, part B, Z4): the IR written as ZEUS 1983 source (GG, PHT 1.1 / v7.E and
// Primus 2.9 by targetVersion "gg" / "pht" / "1.1" / "primus"). ZEUS computes strictly left to right with + - & ! (! OR;
// v7.E, "pht", also *), without
// parentheses, has no conditions, macros or repeats, and DISP is an offset from ORG; labels are letters and digits,
// up to 14 characters (research-zeus-to-sjasmplus.md §2). Expressions are chains (ChainWriter), macros expanded,
// repeats unrolled, conditions decided, the undocumented instructions written as bytes.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class ZeusBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "zeus"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
    bool Includes(const BackendOptions& options) const override
    {
        return options.targetVersion == "pht" || options.targetVersion == "1.1" || options.targetVersion == "primus";   // 1983, GG: none
    }
};
}  // namespace unrealasm::dialects
