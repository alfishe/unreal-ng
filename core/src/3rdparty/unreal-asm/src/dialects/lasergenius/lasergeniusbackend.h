#pragma once

// The Laser Genius backend (asm-synchronizer.md §15.2, part B, Z4): the IR written as Laser Genius 1.04 source. Laser
// Genius computes 16-bit unsigned words with C-like priorities and [ ] for grouping, comparisons that give 1,
// COND / ELSE / ENDC, and stores bytes only where PUT says while ORG sets the address (research-laser-genius.md §4).
// Macros are expanded, repeats unrolled, names assigned with DEFL / "=" written as their values (the RetroWriter
// core).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class LaserGeniusBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "lasergenius"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
    bool WantsLabelValues() const override { return true; }
    bool JoinsSjasmplusProject() const override { return true; }
};
}  // namespace unrealasm::dialects
