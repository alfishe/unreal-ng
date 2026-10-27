#pragma once

// The PROMETHEUS backend (asm-synchronizer.md §15.2, part B, Z4): the IR written as PROMETHEUS 48K source. PROMETHEUS
// computes strictly left to right in 16-bit words with + - * / ? (unsigned division and remainder) and no parentheses,
// keeps names of up to 8 characters in capitals, has no condition, repeat, macro or include, ORG sets both addresses
// and PUT only where the bytes go, DEFS leaves a hole, and a comment is a line of its own (research-prometheus.md §5).
// Expressions are chains (ChainWriter), macros expanded, repeats unrolled, conditions decided; every line is written
// as PROMETHEUS shows it once stored (its codec's record), so a line it cannot store is reported here.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class PrometheusBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "prometheus"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
    bool Includes(const BackendOptions&) const override { return false; }
};
}  // namespace unrealasm::dialects
