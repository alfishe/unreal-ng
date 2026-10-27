#pragma once

// Power Assembler (PASM 3.0, Oleg Sergeyev, 1995) -> IR. The rules come from its help (PAHLP3.0, decrypted in memory)
// and from PASM 3.0 assembling probes in unreal-ng (research-power-assembler.md): a label in column 0 (up to 14
// characters), the mnemonic, operands, "; comment"; expressions strictly left to right with + - * / on 16-bit words;
// # hex, % binary, 'c' characters, $ (in DB / DW the address of the item); DB / DW items with "value DUP count",
// strings in apostrophes; ORG once at the start (none: the code goes to 24576), ENT (no operand: the run address), EQU;
// ITXT / IBIN name (a text / code file read while compiling); SLI.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class PasmFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "pasm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};
}  // namespace unrealasm::dialects
