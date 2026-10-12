#pragma once

// PASM's $ in an instruction is what it has put when it reads the operand (checked in PASM 3.0,
// research-power-assembler.md §3): the first of two operands is read before the opcode (LD ($),A: the instruction's
// address), the others after the prefix and opcode (JP $, LD HL,$, JR $: +1; LD BC,($) with ED, LD IX,$ with DD: +2).

#include <cstdint>

#include "unrealasm/ir.h"

namespace unrealasm::dialects::pasm
{
/// How far ahead of the instruction's address PASM's $ is in operand `k`
inline int64_t CurrentShift(const ir::Statement& s, size_t k)
{
    using Kind = ir::Operand::Kind;
    if (s.operands.size() == 2 && k == 0)
        return 0;
    int64_t bytes = 1;
    for (const ir::Operand& o : s.operands)
        if ((o.kind == Kind::Register && (o.text == "ix" || o.text == "iy")) || o.kind == Kind::Indexed ||
            (o.kind == Kind::Indirect && (o.text == "ix" || o.text == "iy")))
            bytes = 2;
    // LD rr,(nn) / LD (nn),rr with BC, DE or SP: ED 4B / ED 43 ...
    if (s.mnemonic == "ld" && s.operands.size() == 2)
    {
        const ir::Operand& a = s.operands[0];
        const ir::Operand& b = s.operands[1];
        auto pair = [](const ir::Operand& o) { return o.kind == Kind::Register && (o.text == "bc" || o.text == "de" || o.text == "sp"); };
        if ((pair(a) && b.kind == Kind::Memory) || (a.kind == Kind::Memory && pair(b)))
            return 2;
    }
    return bytes;
}
}  // namespace unrealasm::dialects::pasm
