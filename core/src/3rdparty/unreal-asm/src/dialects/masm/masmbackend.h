#pragma once

// The MASM backend (asm-synchronizer.md §15.2, part B, Z3): the IR written as MASM 1.1 / 1.3 source. MASM computes
// left to right in 16-bit words with + - * / & | and @ (XOR), without parentheses or a unary minus; labels have up to
// 10 characters; BEGIN n ... END repeats a block without labels; keywords are capitals (research-masm-to-sjasmplus.md
// §1). An expression is written as such a chain (as for XAS), else as its value; macros are expanded, WHILE and the
// repeats BEGIN cannot hold unrolled, conditions decided (the RetroWriter core). targetVersion "3.0": no @ (MASM 3.0
// refuses it).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class MasmBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "masm"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
};
}  // namespace unrealasm::dialects
