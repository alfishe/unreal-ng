#pragma once

// IR -> z88dk's z80asm source (research-z88dk-backend.md): C's priorities on 32-bit signed words (a 16-bit source's
// division, remainder, shift and comparisons masked), true 1, $hex, labels ending with ":", PHASE / DEPHASE, a SECTION
// for every ORG (z80asm takes one ORG per section), BINARY for INCBIN, REPT ... ENDR, MACRO with LOCAL. What z80asm
// cannot express (memory reads, pages, IFUSED, tests for defined names) is reported.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class Z88dkBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "z88dk"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
};
}  // namespace unrealasm::dialects
