#pragma once

// The ZX-ASM backend (asm-synchronizer.md §15.2, part B, Z3): the IR written as ZAsm 3.15 source (ZX-ASM 3.0 / 3.10,
// ZAsm 3.2x-4.20 read it too). ZX-ASM computes left to right in 16-bit unsigned words with + - * / \ & | ! and
// parentheses; its functions .h .b .n .c .e act on the operand right before them; it has IF / ELSE / ENDIF and
// REPT / ENDR (research-zxasm-to-sjasmplus.md §2). Macros are expanded, WHILE and the repeats REPT cannot hold
// unrolled, names assigned with DEFL / "=" written as their values (the RetroWriter core). targetVersion "3.00" (ZX ASM
// 3.0 itself) and "2" (2.x) write around what those lack: & | !, the functions, REPT, EXA, OUT (C),0, DS fill; 2.x also
// parentheses.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class ZxasmBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "zxasm"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
};
}  // namespace unrealasm::dialects
