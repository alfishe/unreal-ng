#pragma once

// The PASM backend (asm-synchronizer.md §15.2, part B, Z4): the IR written as Power Assembler 3.0 text. PASM computes
// strictly left to right in 16-bit words with + - * / and no parentheses, takes mnemonics in capitals, names of up to
// 14 characters (a letter first, case kept), a byte operand only in 0..255, character constants of one character,
// "value DUP count" as the only item of DB / DW, ORG once and first, EQU on names defined above it (its $ there is 0),
// and reads at most 64 columns of a line (research-power-assembler.md §3, checked in PASM 3.0). Expressions are chains
// (ChainWriter), macros expanded, repeats unrolled, conditions decided.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class PasmBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "pasm"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
};
}  // namespace unrealasm::dialects
