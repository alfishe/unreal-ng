#pragma once

// zasm's compound instructions (Documentation/Compound instructions.txt): conveniences that stand for several Z80
// instructions without side effects on the registers they do not name: LD BC,DE · LD DE,(IX+d) · LD (HL++),BC ·
// LD A,(BC++) · LD R,(--HL) · RR BC · SRL HL · ADD (HL++) · BIT 3,(HL++) ...

#include <vector>

#include "unrealasm/ir.h"

namespace unrealasm::dialects
{
/// The Z80 instructions the compound `s` stands for; false when `s` is no compound form
bool ExpandZasmCompound(const ir::Statement& s, std::vector<ir::Statement>& out);
}  // namespace unrealasm::dialects
