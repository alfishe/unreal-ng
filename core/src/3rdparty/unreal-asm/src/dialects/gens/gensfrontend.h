#pragma once

// GENS (HiSoft Devpac: GENS1-GENS4) -> IR (the Devpac 3 and 4.1 manuals, section 2; every rule below checked on GENS4
// running in unreal-ng, research-gens-to-sjasmplus.md): the label in column 0 (a ':' after it is skipped), only its
// first 6 characters count; mnemonics, registers and conditions in capitals (a lower-case word is a label or a
// macro); expressions strictly left to right on 16-bit two's complement words with + - * / ? (mod) & @ (or) ! (xor),
// signed division, # hex, % binary, "c" characters, $ (the address of the current byte or word in DEFB / DEFW);
// ORG, EQU, DEFB, DEFW, DEFS, DEFM with any delimiter, ENT; IF / ELSE / END (END ends the condition); macros
// NAME MAC ... ENDM with parameters =0..=31 passed by value; *F name includes a file, the other * commands only
// change the listing.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class GensFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "gens"; }
    FrontendResult Parse(const SourceDocument& source) const override;
    /// The labels of the other files take part in the 6-character match (a label defined in an included file)
    FrontendResult ParseInProject(const SourceDocument& source, const std::vector<const SourceDocument*>& project) const override;
};
}  // namespace unrealasm::dialects
