#pragma once

// PROMETHEUS' $ in an instruction is the address of the byte the operand's value goes into, not the instruction's
// (checked in PROMETHEUS 48K, research-prometheus.md §5: LD HL,$ gives the instruction's address +1, LD IX,$ and
// LD BC,($) +2, JR $ the offset -1, the d of LD (IY+d),7 +2, the n of LD (IX+5),n +3).

#include <algorithm>
#include <cstdint>

#include "unrealasm/ir.h"

namespace unrealasm::dialects::prometheus
{
/// How far into the instruction the value of operand `k` goes (0: it has none, or goes into the opcode)
inline int64_t OperandField(const ir::Statement& s, size_t k)
{
    using Kind = ir::Operand::Kind;
    const auto& ops = s.operands;
    if (k >= ops.size() || s.mnemonic == "rst" || s.mnemonic == "im" || ((s.mnemonic == "bit" || s.mnemonic == "set" || s.mnemonic == "res") && k == 0))
        return 0;
    const auto any = [&](auto test) { return std::any_of(ops.begin(), ops.end(), test); };
    const bool indexed = any([](const ir::Operand& o) { return o.kind == Kind::Indexed; });
    const bool index = indexed || any([](const ir::Operand& o) {
                           return (o.kind == Kind::Register || o.kind == Kind::Indirect) &&
                                  (o.text == "ix" || o.text == "iy" || o.text == "ixh" || o.text == "ixl" || o.text == "iyh" || o.text == "iyl");
                       });
    // LD BC / DE / SP with (nn): ED 4B / 43 ...
    const bool ed = s.mnemonic == "ld" && any([](const ir::Operand& o) { return o.kind == Kind::Memory; }) &&
                    any([](const ir::Operand& o) { return o.kind == Kind::Register && (o.text == "bc" || o.text == "de" || o.text == "sp"); });
    switch (ops[k].kind)
    {
        case Kind::Indexed: return 2;
        case Kind::Immediate: case Kind::Memory: return indexed ? 3 : index || ed ? 2 : 1;
        default: return 0;
    }
}
}  // namespace unrealasm::dialects::prometheus
