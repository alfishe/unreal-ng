#pragma once

// The TASM backend (asm-synchronizer.md §15.2, part B, Z3): the IR written as TASM 4.12 source (2.0 / 3 / 4.0 by
// targetVersion). TASM computes left to right in 16-bit words with + - * / | & (4.0 also ! XOR and the postfix { } ^)
// and parentheses, keeps labels of any length (4.12; 7 characters before it), reads an operand that starts with "(" as memory, and has no DUP or
// WHILE; 4.12's .IF assembles its part when the value is 0 and does not nest (research-tasm-to-sjasmplus.md §2).
// Macros are expanded, WHILE and DUP unrolled, the names assigned with DEFL / "=" written as their values (the
// RetroWriter core).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class TasmBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "tasm"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
};
}  // namespace unrealasm::dialects
