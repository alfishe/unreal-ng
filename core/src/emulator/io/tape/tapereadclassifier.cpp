#include "tapereadclassifier.h"

/// region <Bit tracker>

namespace
{
// Tracked bit positions are 9-bit masks: bits 0-7 are the register, bit 8 is
// the carry flag.
constexpr uint16_t CARRY = 0x100;
constexpr uint16_t EAR_BIT = 0x40;
constexpr uint16_t KEY_BITS = 0x1F;
constexpr uint8_t REG_A = 7;
constexpr uint8_t REG_HL_INDIRECT = 6;
constexpr int MAX_INSTRUCTIONS = 6;

enum class ShiftOp : uint8_t
{
    Rlc = 0, Rrc, Rl, Rr, Sla, Sra, Sll, Srl
};

uint16_t ApplyShift(uint16_t m, ShiftOp shift)
{
    const uint16_t bit0 = m & 0x01;
    const uint16_t bit7 = (m >> 7) & 0x01;
    const uint16_t carry = (m >> 8) & 0x01;
    const uint16_t low7 = m & 0x7F;
    const uint16_t high7 = (m & 0xFE) >> 1;

    switch (shift)
    {
        case ShiftOp::Rlc: return static_cast<uint16_t>((low7 << 1) | bit7 | (bit7 << 8));
        case ShiftOp::Rrc: return static_cast<uint16_t>(high7 | (bit0 << 7) | (bit0 << 8));
        case ShiftOp::Rl:  return static_cast<uint16_t>((low7 << 1) | carry | (bit7 << 8));
        case ShiftOp::Rr:  return static_cast<uint16_t>(high7 | (carry << 7) | (bit0 << 8));
        case ShiftOp::Sla:
        case ShiftOp::Sll: return static_cast<uint16_t>((low7 << 1) | (bit7 << 8));
        case ShiftOp::Sra: return static_cast<uint16_t>(high7 | (m & 0x80) | (bit0 << 8));
        case ShiftOp::Srl: return static_cast<uint16_t>(high7 | (bit0 << 8));
    }
    return m;
}

struct Tracker
{
    uint8_t reg = REG_A;      // register holding the value that was read
    uint16_t ear = EAR_BIT;   // where the EAR bit is now
    uint16_t keys = KEY_BITS; // where the key bits are now
    bool keyTested = false;   // a key bit was tested and the code fell through
    bool signFromReg = false; // the S flag currently mirrors bit 7 of reg

    void Rotate(ShiftOp shift, bool setsSign)
    {
        ear = ApplyShift(ear, shift);
        keys = ApplyShift(keys, shift);
        signFromReg = setsSign;
    }

    void ClearCarry()
    {
        ear &= static_cast<uint16_t>(~CARRY);
        keys &= static_cast<uint16_t>(~CARRY);
    }

    // Bits forced to a constant (OR n, SET, RES) no longer carry the input
    void Drop(uint16_t bits)
    {
        ear &= static_cast<uint16_t>(~bits);
        keys &= static_cast<uint16_t>(~bits);
    }

    TapeReadKind StopResult() const
    {
        if (keyTested)
            return TapeReadKind::Key;
        if ((ear & 0xFF) == 0 && (keys & 0xFF) != 0)
            return TapeReadKind::Key;
        return TapeReadKind::Other;
    }
};

// A test of bits `tested`: returns true when it decided the read
bool Test(Tracker& t, uint16_t tested, TapeReadKind& result)
{
    if (t.ear & tested)
    {
        result = TapeReadKind::Ear;
        return true;
    }
    if (t.keys & tested)
        t.keyTested = true;
    return false;
}
} // anonymous namespace

/// endregion </Bit tracker>

TapeReadKind TapeReadClassifier::Classify(ByteReader read, void* context, uint16_t pcAfterIn)
{
    auto at = [&](uint16_t address) { return read(context, address); };

    Tracker t;

    // Which register received the value
    const uint8_t prefix = at(static_cast<uint16_t>(pcAfterIn - 2));
    const uint8_t opcode = at(static_cast<uint16_t>(pcAfterIn - 1));
    if (prefix == 0xED && (opcode & 0xC7) == 0x40)
    {
        t.reg = (opcode >> 3) & 0x07;
        if (t.reg == REG_HL_INDIRECT)
            return TapeReadKind::Other;  // IN F,(C): flags only
    }
    else if (prefix != 0xDB)
    {
        return TapeReadKind::Other;  // INI/IND/INIR/INDR and the like
    }

    uint16_t pc = pcAfterIn;
    TapeReadKind result = TapeReadKind::Other;

    for (int i = 0; i < MAX_INSTRUCTIONS; i++)
    {
        const uint8_t op = at(pc);
        const bool onA = (t.reg == REG_A);

        // Main table, 1-byte ops on registers: LD r,r' / INC r / DEC r / LD r,n
        if (op >= 0x40 && op <= 0x7F && op != 0x76)
        {
            const uint8_t dst = (op >> 3) & 0x07;
            const uint8_t src = op & 0x07;
            if (dst == t.reg && src != t.reg)
            {
                return t.StopResult();  // tracked value overwritten
            }
            if (dst == REG_A && src == t.reg)
            {
                t.reg = REG_A;          // LD A,r: follow the copy
                t.signFromReg = false;
            }
            pc += 1;
            continue;
        }
        if ((op & 0xC7) == 0x04 || (op & 0xC7) == 0x05)  // INC r / DEC r
        {
            if (((op >> 3) & 0x07) == t.reg)
                return t.StopResult();
            t.signFromReg = false;
            pc += 1;
            continue;
        }
        if ((op & 0xC7) == 0x06)  // LD r,n
        {
            if (((op >> 3) & 0x07) == t.reg)
                return t.StopResult();
            pc += 2;
            continue;
        }

        switch (op)
        {
            case 0x00:  // NOP
                pc += 1;
                continue;

            case 0x07: case 0x0F: case 0x17: case 0x1F:  // RLCA / RRCA / RLA / RRA
            {
                if (onA)
                {
                    static const ShiftOp shifts[4] = { ShiftOp::Rlc, ShiftOp::Rrc, ShiftOp::Rl, ShiftOp::Rr };
                    t.Rotate(shifts[(op >> 3) & 0x03], false);
                }
                else
                {
                    t.ClearCarry();
                }
                pc += 1;
                continue;
            }

            case 0x2F:  // CPL: positions unchanged, flags C and S untouched
                pc += 1;
                continue;

            case 0x87:  // ADD A,A
                if (onA)
                    t.Rotate(ShiftOp::Sla, true);
                else
                    t.ClearCarry();
                pc += 1;
                continue;

            case 0xE6:  // AND n
            {
                if (!onA)
                {
                    t.ClearCarry();
                    pc += 2;
                    continue;
                }
                const uint16_t n = at(static_cast<uint16_t>(pc + 1));
                if (Test(t, n, result))
                    return result;
                // The value is masked by n: whatever n did not cover is gone
                t.ear &= n;
                t.keys &= n;
                return t.StopResult();
            }

            case 0xF6:  // OR n
            case 0xEE:  // XOR n
            {
                if (onA && op == 0xF6)
                    t.Drop(at(static_cast<uint16_t>(pc + 1)));
                t.ClearCarry();
                t.signFromReg = onA;
                pc += 2;
                continue;
            }

            case 0xFE:  // CP n: compares the whole value, the tracker cannot follow
                if (onA)
                    return t.StopResult();
                t.signFromReg = false;
                t.ClearCarry();
                pc += 2;
                continue;

            case 0x30: case 0x38:  // JR NC / JR C
            case 0xD2: case 0xDA:  // JP NC / JP C
            case 0xD4: case 0xDC:  // CALL NC / CALL C
            case 0xD0: case 0xD8:  // RET NC / RET C
            {
                if (Test(t, CARRY, result))
                    return result;
                pc += (op == 0x30 || op == 0x38) ? 2 : ((op == 0xD0 || op == 0xD8) ? 1 : 3);
                continue;
            }

            case 0xF2: case 0xFA:  // JP P / JP M
            case 0xF4: case 0xFC:  // CALL P / CALL M
            case 0xF0: case 0xF8:  // RET P / RET M
            {
                if (t.signFromReg && Test(t, 0x80, result))
                    return result;
                pc += (op == 0xF0 || op == 0xF8) ? 1 : 3;
                continue;
            }

            case 0x20: case 0x28:  // JR NZ / JR Z: the Z flag of an earlier test
                pc += 2;
                continue;
            case 0xC2: case 0xCA: case 0xE2: case 0xEA:  // JP NZ / Z / PO / PE
            case 0xC4: case 0xCC: case 0xE4: case 0xEC:  // CALL NZ / Z / PO / PE
                pc += 3;
                continue;
            case 0xC0: case 0xC8: case 0xE0: case 0xE8:  // RET NZ / Z / PO / PE
                pc += 1;
                continue;

            case 0x10:  // DJNZ: falls through when B reaches 0
                if (t.reg == 0)
                    return t.StopResult();
                pc += 2;
                continue;

            case 0xCB:
            {
                const uint8_t cb = at(static_cast<uint16_t>(pc + 1));
                const uint8_t x = cb >> 6;
                const uint8_t y = (cb >> 3) & 0x07;
                const uint8_t z = cb & 0x07;
                pc += 2;

                if (z != t.reg)
                {
                    if (x == 0)
                    {
                        t.ClearCarry();
                        t.signFromReg = false;
                    }
                    continue;
                }

                switch (x)
                {
                    case 0:  // rotates and shifts: S reflects the new bit 7
                        t.Rotate(static_cast<ShiftOp>(y), true);
                        continue;
                    case 1:  // BIT y,r
                        if (Test(t, static_cast<uint16_t>(1u << y), result))
                            return result;
                        continue;
                    default:  // RES / SET: the bit is forced to a constant
                        t.Drop(static_cast<uint16_t>(1u << y));
                        continue;
                }
            }

            default:
                break;
        }

        // Accumulator arithmetic and logic with a register operand
        if (op >= 0x80 && op <= 0xBF)
        {
            const uint8_t group = (op >> 3) & 0x07;  // ADD ADC SUB SBC AND XOR OR CP
            const uint8_t src = op & 0x07;

            if (!onA)
            {
                if (src == t.reg)
                    return t.StopResult();  // the tracked value is combined with an unknown one
                t.ClearCarry();
                t.signFromReg = false;
                pc += 1;
                continue;
            }

            if (src == REG_A)
            {
                if (group == 5)  // XOR A: the value is gone
                    return t.StopResult();
                if (group == 4 || group == 6)  // AND A / OR A: value kept, C cleared, S set from bit 7
                {
                    t.ClearCarry();
                    t.signFromReg = true;
                    pc += 1;
                    continue;
                }
                return t.StopResult();
            }

            if (group == 5)  // XOR r: positions unchanged
            {
                t.ClearCarry();
                t.signFromReg = true;
                pc += 1;
                continue;
            }

            // AND r / OR r / ADD r / CP r with an unknown operand
            return t.StopResult();
        }

        // Unconditional transfers, prefixes and everything else
        return t.StopResult();
    }

    return t.StopResult();
}
