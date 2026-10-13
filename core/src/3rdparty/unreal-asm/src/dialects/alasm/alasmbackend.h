#pragma once

// The ALASM backend (asm-synchronizer.md §15.2, part B): the IR written as ALASM 3.8-5.09 source. ALASM computes left
// to right in 16-bit unsigned words and in one pass: an operation other than + and - may not use a label defined
// further down (its help, "Expressions"). An expression it cannot compute so (a later label, an operator it lacks, a
// division of values outside 0..#FFFF) is written as its value, from the source's layout (BackendOptions::labelValues).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class AlasmBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "alasm"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
};
}  // namespace unrealasm::dialects
