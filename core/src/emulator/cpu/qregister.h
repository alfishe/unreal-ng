#pragma once

#include <array>
#include <cstdint>

// The Z80's hidden Q latch: SCF / CCF take bits 5 and 3 of F from (A | (F & ~Q)). After an instruction that
// writes F, Q = F & 0x28; after any other instruction Q = 0. "Writes F" is a property of the instruction, not
// of the value: DEC A that leaves F as it was still loads Q, POP AF and EX AF,AF' change F and clear Q (they
// move F, the ALU flag path does not write it). The classes are those z80test's z80ccf checks after every
// instruction (Patrik Rak, z80test 1.2a; David Banks, hoglet67/Z80Decoder "Undocumented Flags").
//
// One mask per instruction, applied in Z80::Z80Step and the prefix handlers as Q = F & mask: no compare of F
// before and after, no test of the opcode.
// CPU-LIBRARY-MIGRATION(q-register): the engine sets Q in each handler's epilogue (Z80FinishFlags / NoFlags)
namespace z80q
{

/// Q mask of an instruction that writes F through the ALU flag path
inline constexpr uint8_t kWrites = 0x28;

/// Q mask of an instruction that does not write F
inline constexpr uint8_t kKeeps = 0x00;

/// Entry of a prefix byte (CB, ED, DD, FD) in kBase: the prefix handler has set Q from its own map, Z80Step keeps it
inline constexpr uint8_t kPrefixed = 0xFF;

/// Unprefixed map, also the DD / FD map (the index forms write F as their HL forms do): the Q mask, kPrefixed on the
/// prefix bytes. Z80Step: Q = F & mask unless kPrefixed, a branch taken only on prefixed instructions. A branchless
/// form that reads Q back to keep it (Q = (F & m) | (Q & keep)) measured about 2 % slower on a host frame
inline constexpr std::array<uint8_t, 256> kBase = []
{
    std::array<uint8_t, 256> t{};
    for (int op = 0; op < 256; ++op)
    {
        bool writes = false;
        if (op < 0x40)
        {
            writes = (op & 0x07) == 0x04 || (op & 0x07) == 0x05  // INC r / DEC r, (HL) included
                     || (op & 0x0F) == 0x09                      // ADD HL,rr
                     || (op & 0xC7) == 0x07;                     // RLCA RRCA RLA RRA DAA CPL SCF CCF
        }
        else if (op >= 0x80 && op < 0xC0)
        {
            writes = true;  // ADD ADC SUB SBC AND XOR OR CP with r / (HL)
        }
        else if (op >= 0xC0)
        {
            writes = (op & 0xC7) == 0xC6;  // ALU with n
        }
        t[op] = writes ? kWrites : kKeeps;
    }
    t[0xCB] = t[0xED] = t[0xDD] = t[0xFD] = kPrefixed;
    return t;
}();

/// CB map (and DDCB / FDCB): the rotates and shifts 00-3F and BIT 40-7F write F, RES / SET do not
constexpr uint8_t CbMask(uint8_t op)
{
    return op < 0x80 ? kWrites : kKeeps;
}

/// ED map
inline constexpr std::array<uint8_t, 256> kEd = []
{
    std::array<uint8_t, 256> t{};
    for (int op = 0x40; op < 0x80; ++op)
    {
        const int z = op & 0x07;
        const bool writes = z == 0                            // IN r,(C) / IN F,(C)
                            || z == 2                         // SBC HL,rr / ADC HL,rr
                            || z == 4                         // NEG and its mirrors
                            || op == 0x57 || op == 0x5F       // LD A,I / LD A,R
                            || op == 0x67 || op == 0x6F;      // RRD / RLD
        t[op] = writes ? kWrites : kKeeps;
    }
    for (int op = 0xA0; op < 0xC0; ++op)
    {
        if ((op & 0x07) < 4)
            t[op] = kWrites;  // LDI CPI INI OUTI, LDD CPD IND OUTD and their repeating forms
    }
    return t;
}();

}  // namespace z80q
