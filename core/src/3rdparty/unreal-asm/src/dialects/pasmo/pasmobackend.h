#pragma once

// IR -> pasmo 0.5 source (research-pasmo-backend.md): pasmo's priorities (HIGH / LOW and the unary operators bind more
// loosely than "+", so every unary operation is written in parentheses), 16-bit unsigned words, single-quoted texts,
// MACRO with named parameters and LOCAL, PROC / LOCAL for local blocks, REPT ... ENDM (a REPT body with labels goes
// into a macro, since pasmo's LOCAL does not work in REPT). pasmo has no PHASE: a displacement is written out as labels
// "EQU $+__UNREALASM_D", $ as ($+__UNREALASM_D) and JR / DJNZ targets as (target)-__UNREALASM_D. What pasmo cannot
// express (memory reads while assembling, pages, IFUSED, WHILE) is reported.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class PasmoBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "pasmo"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
};
}  // namespace unrealasm::dialects
