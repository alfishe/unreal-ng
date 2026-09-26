// disasm-unreal.h - z80ex_dasm wrapper producing Unreal text format.
//
// z80ex_dasm templates are upper case with substitution markers; every
// substituted value is emitted as "#XXXX" by its internal printf formats.
// ToUnrealText() converts one such line to the debugger format (§4.2.1):
// lower-case mnemonic and register names, upper-case hex values, "#"
// stripped, negative ($) displacements printed as -NN, literal "0xNN"
// (RST templates) folded to NN, and the mnemonic padded to 5 columns when
// an operand follows ("ld   sp,C000" but "di", "ldir", "nop").
#pragma once

#include <cstdint>
#include <string>

#include "model/model.h"

namespace dbg {

struct UnrealInstr {
    int len = 0;        // instruction length in bytes
    std::string text;   // Unreal format mnemonic ("call 8020")
};

// Disassemble one instruction at "addr" reading from a flat 64K map.
UnrealInstr UnrealDisasm(const uint8_t* mem, uint16_t addr);

// Convert one raw z80ex_dasm line ("LD SP,#00C0") to Unreal text.
std::string ToUnrealText(const char* dasm);

// Backward heuristic (§4.2.5): disassemble forward from max(addr-16, 0) and
// return the start of the last instruction that begins before "addr". May
// land mid-instruction on data (accepted behaviour, quirk Q7).
uint16_t UnrealPrevInstruction(const uint8_t* mem, uint16_t addr);

// Branch analysis of the instruction at "pc" (§4.2.4): taken/target/next-PC
// plus the HALT/block/call/loop classification. Pure function of mem + regs,
// so every backend that can serve a flat 64K image reuses it (mock executes
// locally; REST disassembles fetched memory instead of trusting the wire).
BranchInfo UnrealBranchInfo(const uint8_t* mem, uint16_t pc, const Z80Regs& regs);

}  // namespace dbg
