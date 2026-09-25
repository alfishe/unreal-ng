// asm-subset.h - tiny subset assembler for the assemble dialog (§4.2 mode 2).
//
// Accepts the same syntax the Unreal disassembler prints: lower-case
// mnemonics, upper-case hex operands without a prefix, 'c' character
// constants, (hl)/(bc)/(de)/(nn)/(ix+d)/(iy-d) operand forms and the
// condition codes nz z nc c po pe p m. Covers the common Z80 instruction
// set; anything else fails with an error so the dialog re-opens (§4.2).
// Relative jumps (jr/djnz) are computed against "addr".
#pragma once

#include <cstdint>
#include <string>

#include "backend/debugger-backend.h"

namespace dbg {

AssembleResult AssembleSubset(uint16_t addr, const std::string& text);

}  // namespace dbg
