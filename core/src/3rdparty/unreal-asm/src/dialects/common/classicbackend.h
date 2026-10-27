#pragma once

// The writer the backends of the classic cross assemblers share (research-pasmo-backend.md,
// research-z88dk-backend.md): pasmo 0.5 and z88dk's z80asm. Both lack sjasmplus' DUP, DISP and SAVEBIN and differ in
// their expressions; the target picks the differences (priorities, words, true value, PHASE or a written-out
// displacement, sections for several ORGs).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
enum class ClassicTarget
{
    Pasmo,
    Z80asm,
};

BackendResult WriteClassic(const ir::Program& program, const BackendOptions& options, ClassicTarget target);

/// An expression's value as the layout gives its names (fixed ones only); none when a name is unknown
std::optional<int64_t> LayoutValue(const ir::Expr& e, const std::map<std::string, SourceLabel>& values);
}  // namespace unrealasm::dialects
