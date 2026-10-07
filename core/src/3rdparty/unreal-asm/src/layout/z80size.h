#pragma once

// How many bytes a Z80 instruction takes, from its IR form (Zilog's Z80 CPU User Manual, the undocumented forms
// sjasmplus accepts: IXH / IXL halves, SLI / SLL, OUT (C),0, IN F,(C), the CB forms with (IX+d) and a register, and
// sjasmplus' fake instructions LD rr,rr' / LD rr,(IX+d) / LD (IX+d),rr / SUB HL,rr). The value of an operand never
// changes the size, so pass 1 already knows every address.

#include <string>

#include "unrealasm/ir.h"

namespace unrealasm::layout
{
/// The size in bytes; 0 with `error` set when the form is no Z80 instruction sjasmplus accepts
int InstructionSize(const ir::Statement& s, std::string& error);
}  // namespace unrealasm::layout
