#include "z80cfdecoder.h"
#include "emulator/memory/memory.h"

/// @file z80cfdecoder.cpp
/// @brief Implementation of the lightweight Z80 control flow instruction decoder.
///
/// @details
/// This file implements the high-performance decoder for Z80 control flow instructions.
/// All decoding is done via direct byte manipulation and flag bitmask testing — no strings,
/// no heap allocations, no disassembler infrastructure.
///
/// The decoder handles all Z80 control flow instruction variants:
///   - Unprefixed: JP, JR, CALL, RET, RST, DJNZ, JP (HL)
///   - DD-prefixed: JP (IX)
///   - FD-prefixed: JP (IY)
///   - ED-prefixed: RETI, RETN (and undocumented RETN variants)

// Include the Z80CFType enum definition
#include "emulator/memory/calltrace.h"

// ============================================================================
// Lookup Table: Pre-computed control flow opcode classification
// ============================================================================

/// @brief Pre-computed table marking which unprefixed opcodes are control flow instructions.
///
/// @details This table is indexed by the first byte of the instruction. A value of true
///          means the opcode is (or could be, in the case of prefixes) a control flow
///          instruction. Prefix bytes DD, FD, and ED are marked true because they can
///          lead to control flow instructions (JP (IX), JP (IY), RETI/RETN).
///
/// The table covers:
///   - 0x10: DJNZ
///   - 0x18, 0x20, 0x28, 0x30, 0x38: JR / JR cc
///   - 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8: RET cc
///   - 0xC2, 0xCA, 0xD2, 0xDA, 0xE2, 0xEA, 0xF2, 0xFA: JP cc,nn
///   - 0xC3: JP nn
///   - 0xC4, 0xCC, 0xD4, 0xDC, 0xE4, 0xEC, 0xF4, 0xFC: CALL cc,nn
///   - 0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF: RST n
///   - 0xC9: RET
///   - 0xCD: CALL nn
///   - 0xDD, 0xED, 0xFD: Prefix bytes (may lead to CF instructions)
///   - 0xE9: JP (HL)
// clang-format off
const bool Z80ControlFlowDecoder::s_cfOpcodeTable[256] =
{
    // 0x00-0x0F
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    // 0x10-0x1F: DJNZ at 0x10, JR at 0x18, JR NZ at 0x20 (next row)
    true,  false, false, false, false, false, false, false,
    true,  false, false, false, false, false, false, false,
    // 0x20-0x2F: JR NZ at 0x20, JR Z at 0x28
    true,  false, false, false, false, false, false, false,
    true,  false, false, false, false, false, false, false,
    // 0x30-0x3F: JR NC at 0x30, JR C at 0x38
    true,  false, false, false, false, false, false, false,
    true,  false, false, false, false, false, false, false,
    // 0x40-0x4F
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    // 0x50-0x5F
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    // 0x60-0x6F
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    // 0x70-0x7F
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    // 0x80-0x8F
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    // 0x90-0x9F
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    // 0xA0-0xAF
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    // 0xB0-0xBF
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    // 0xC0-0xCF: RET NZ, -, JP NZ, JP nn, CALL NZ, -, -, RST 00,
    //            RET Z,  RET, JP Z, [CB], CALL Z, CALL nn, -, RST 08
    true,  false, true,  true,  true,  false, false, true,
    true,  true,  true,  false, true,  true,  false, true,
    // 0xD0-0xDF: RET NC, -, JP NC, -, CALL NC, -, -, RST 10,
    //            RET C,  -, JP C,  -, CALL C,  [DD], -, RST 18
    true,  false, true,  false, true,  false, false, true,
    true,  false, true,  false, true,  true,  false, true,
    // 0xE0-0xEF: RET PO, -, JP PO, -, CALL PO, -, -, RST 20,
    //            RET PE, JP (HL), JP PE, -, CALL PE, [ED], -, RST 28
    true,  false, true,  false, true,  false, false, true,
    true,  true,  true,  false, true,  true,  false, true,
    // 0xF0-0xFF: RET P,  -, JP P,  -, CALL P,  -, -, RST 30,
    //            RET M,  -, JP M,  -, CALL M,  [FD], -, RST 38
    true,  false, true,  false, true,  false, false, true,
    true,  false, true,  false, true,  true,  false, true,
};
// clang-format on

// ============================================================================
// Condition Code Evaluation
// ============================================================================

/// @brief Evaluate a Z80 condition code against the flags register.
///
/// @details The condition code is a 3-bit value extracted from bits [5:3] of
///          conditional branch opcodes. This function performs a single array
///          lookup followed by a bitmask test — no branching beyond the switch.
///
///          Z80 condition encoding:
///            000 = NZ (Not Zero)      — Z flag clear
///            001 = Z  (Zero)          — Z flag set
///            010 = NC (No Carry)      — C flag clear
///            011 = C  (Carry)         — C flag set
///            100 = PO (Parity Odd)    — P/V flag clear
///            101 = PE (Parity Even)   — P/V flag set
///            110 = P  (Plus/Positive) — S flag clear
///            111 = M  (Minus/Negative)— S flag set
bool Z80ControlFlowDecoder::IsConditionMet(uint8_t condCode, uint8_t flags)
{
    // Flag bit positions: C=0x01, N=0x02, P/V=0x04, H=0x10, Z=0x40, S=0x80
    // Raw hex used to avoid macro collision with cpulogic.h (CF, ZF, SF, etc.)
    switch (condCode & 0x07)
    {
        case 0: return !(flags & 0x40);    // NZ (Zero flag not set)
        case 1: return  (flags & 0x40);    // Z  (Zero flag set)
        case 2: return !(flags & 0x01);    // NC (Carry flag not set)
        case 3: return  (flags & 0x01);    // C  (Carry flag set)
        case 4: return !(flags & 0x04);    // PO (P/V flag not set - parity odd)
        case 5: return  (flags & 0x04);    // PE (P/V flag set - parity even)
        case 6: return !(flags & 0x80);    // P  (Sign flag not set - positive)
        case 7: return  (flags & 0x80);    // M  (Sign flag set - minus)
        default: return false;  // unreachable
    }
}

// ============================================================================
// Memory Helpers
// ============================================================================

/// @brief Read a 16-bit little-endian word from the Memory interface.
uint16_t Z80ControlFlowDecoder::ReadWord(Memory* memory, uint16_t addr)
{
    if (!memory)
        return 0;
    uint8_t lo = memory->DirectReadFromZ80Memory(addr);
    uint8_t hi = memory->DirectReadFromZ80Memory(addr + 1);
    return static_cast<uint16_t>((hi << 8) | lo);
}

// ============================================================================
// Fast Pre-filter
// ============================================================================

bool Z80ControlFlowDecoder::IsControlFlowOpcode(uint8_t byte0)
{
    return s_cfOpcodeTable[byte0];
}

// ============================================================================
// ED-Prefix Decoder (RETI / RETN variants)
// ============================================================================

/// @brief Decode an ED-prefixed control flow instruction.
///
/// @details The only ED-prefixed control flow instructions are RETI and RETN:
///   - 0xED 0x4D: RETI (Return from Interrupt)
///   - 0xED 0x45: RETN (Return from Non-Maskable Interrupt)
///   - 0xED 0x55/5D/65/6D/75/7D: Undocumented RETN variants
///
/// All are unconditional returns that read the return address from the stack.
bool Z80ControlFlowDecoder::DecodeED(uint8_t second_byte, uint16_t sp, Memory* memory,
                                      Z80ControlFlowResult& result)
{
    switch (second_byte)
    {
        case 0x4D:  // RETI
            result.type = Z80CFType::RETI;
            result.target_addr = ReadWord(memory, sp);
            result.taken = true;
            result.instruction_len = 2;
            return true;

        case 0x45:  // RETN (documented)
        case 0x55:  // RETN (undocumented)
        case 0x65:  // RETN (undocumented)
        case 0x75:  // RETN (undocumented)
            result.type = Z80CFType::RET;
            result.target_addr = ReadWord(memory, sp);
            result.taken = true;
            result.instruction_len = 2;
            return true;

        case 0x5D:  // RETI (undocumented)
        case 0x6D:  // RETI (undocumented)
        case 0x7D:  // RETI (undocumented)
            result.type = Z80CFType::RETI;
            result.target_addr = ReadWord(memory, sp);
            result.taken = true;
            result.instruction_len = 2;
            return true;

        default:
            return false;
    }
}

// ============================================================================
// Unprefixed Opcode Decoder
// ============================================================================

/// @brief Decode an unprefixed control flow instruction.
///
/// @details This handles the bulk of Z80 control flow instructions. The opcode
///          byte is analyzed via bit patterns that match the Z80's internal
///          instruction decoding:
///
///          For conditional branches, the condition code is in bits [5:3]:
///            opcode & 0x38 >> 3 gives the condition code (0-7)
///
///          Opcode groups by bit patterns:
///            11_ccc_000  = RET cc        (conditional return)
///            11_ccc_010  = JP cc,nn      (conditional absolute jump)
///            11_ccc_100  = CALL cc,nn    (conditional call)
///            11_nnn_111  = RST n         (restart, n = opcode & 0x38)
///            11_000_011  = JP nn         (unconditional jump)
///            11_001_001  = RET           (unconditional return)
///            11_001_101  = CALL nn       (unconditional call)
///            11_101_001  = JP (HL)       (indirect jump via HL)
///            00_010_000  = DJNZ e        (decrement B, jump if not zero)
///            00_011_000  = JR e          (unconditional relative jump)
///            00_1cc_000  = JR cc,e       (conditional relative jump, cc = NZ/Z/NC/C only)
bool Z80ControlFlowDecoder::DecodeUnprefixed(uint8_t opcode, const uint8_t* bytes, uint16_t pc,
                                              uint8_t flags, uint8_t b_reg, uint16_t sp,
                                              uint16_t hl, Memory* memory,
                                              Z80ControlFlowResult& result)
{
    switch (opcode)
    {
        // ================================================================
        // DJNZ e (0x10)
        // ================================================================
        case 0x10:
            result.type = Z80CFType::DJNZ;
            result.target_addr = static_cast<uint16_t>(pc + 2 + static_cast<int8_t>(bytes[1]));
            result.taken = ((b_reg - 1) != 0);
            result.instruction_len = 2;
            return true;

        // ================================================================
        // JR e (0x18) — unconditional relative jump
        // ================================================================
        case 0x18:
            result.type = Z80CFType::JR;
            result.target_addr = static_cast<uint16_t>(pc + 2 + static_cast<int8_t>(bytes[1]));
            result.taken = true;
            result.instruction_len = 2;
            return true;

        // ================================================================
        // JR cc,e — conditional relative jumps
        // Only 4 conditions supported: NZ (0x20), Z (0x28), NC (0x30), C (0x38)
        // ================================================================
        case 0x20:  // JR NZ,e
            result.type = Z80CFType::JR;
            result.target_addr = static_cast<uint16_t>(pc + 2 + static_cast<int8_t>(bytes[1]));
            result.taken = IsConditionMet(0, flags);  // NZ
            result.instruction_len = 2;
            return true;

        case 0x28:  // JR Z,e
            result.type = Z80CFType::JR;
            result.target_addr = static_cast<uint16_t>(pc + 2 + static_cast<int8_t>(bytes[1]));
            result.taken = IsConditionMet(1, flags);  // Z
            result.instruction_len = 2;
            return true;

        case 0x30:  // JR NC,e
            result.type = Z80CFType::JR;
            result.target_addr = static_cast<uint16_t>(pc + 2 + static_cast<int8_t>(bytes[1]));
            result.taken = IsConditionMet(2, flags);  // NC
            result.instruction_len = 2;
            return true;

        case 0x38:  // JR C,e
            result.type = Z80CFType::JR;
            result.target_addr = static_cast<uint16_t>(pc + 2 + static_cast<int8_t>(bytes[1]));
            result.taken = IsConditionMet(3, flags);  // C
            result.instruction_len = 2;
            return true;

        // ================================================================
        // JP nn (0xC3) — unconditional absolute jump
        // ================================================================
        case 0xC3:
            result.type = Z80CFType::JP;
            result.target_addr = static_cast<uint16_t>(bytes[1] | (bytes[2] << 8));
            result.taken = true;
            result.instruction_len = 3;
            return true;

        // ================================================================
        // JP cc,nn — conditional absolute jumps
        // Pattern: 11_ccc_010 where ccc is condition code
        // ================================================================
        case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        case 0xE2: case 0xEA: case 0xF2: case 0xFA:
        {
            uint8_t condCode = (opcode >> 3) & 0x07;
            result.type = Z80CFType::JP;
            result.target_addr = static_cast<uint16_t>(bytes[1] | (bytes[2] << 8));
            result.taken = IsConditionMet(condCode, flags);
            result.instruction_len = 3;
            return true;
        }

        // ================================================================
        // JP (HL) (0xE9) — indirect jump via HL register pair
        // ================================================================
        case 0xE9:
            result.type = Z80CFType::JP;
            result.target_addr = hl;
            result.taken = true;
            result.instruction_len = 1;
            return true;

        // ================================================================
        // CALL nn (0xCD) — unconditional call
        // ================================================================
        case 0xCD:
            result.type = Z80CFType::CALL;
            result.target_addr = static_cast<uint16_t>(bytes[1] | (bytes[2] << 8));
            result.taken = true;
            result.instruction_len = 3;
            return true;

        // ================================================================
        // CALL cc,nn — conditional calls
        // Pattern: 11_ccc_100 where ccc is condition code
        // ================================================================
        case 0xC4: case 0xCC: case 0xD4: case 0xDC:
        case 0xE4: case 0xEC: case 0xF4: case 0xFC:
        {
            uint8_t condCode = (opcode >> 3) & 0x07;
            result.type = Z80CFType::CALL;
            result.target_addr = static_cast<uint16_t>(bytes[1] | (bytes[2] << 8));
            result.taken = IsConditionMet(condCode, flags);
            result.instruction_len = 3;
            return true;
        }

        // ================================================================
        // RET (0xC9) — unconditional return
        // ================================================================
        case 0xC9:
            result.type = Z80CFType::RET;
            result.target_addr = ReadWord(memory, sp);
            result.taken = true;
            result.instruction_len = 1;
            return true;

        // ================================================================
        // RET cc — conditional returns
        // Pattern: 11_ccc_000 where ccc is condition code
        // ================================================================
        case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        {
            uint8_t condCode = (opcode >> 3) & 0x07;
            result.type = Z80CFType::RET;
            result.target_addr = ReadWord(memory, sp);
            result.taken = IsConditionMet(condCode, flags);
            result.instruction_len = 1;
            return true;
        }

        // ================================================================
        // RST n — restart instructions
        // Pattern: 11_nnn_111, target = n * 8 = opcode & 0x38
        // ================================================================
        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF:
            result.type = Z80CFType::RST;
            result.target_addr = opcode & 0x38;
            result.taken = true;
            result.instruction_len = 1;
            return true;

        default:
            return false;
    }
}

// ============================================================================
// Main Decoder Entry Point
// ============================================================================

/// @brief Decode a potential control flow instruction.
///
/// @details The decoding strategy is:
///   1. Check the first byte against the pre-computed lookup table (fast reject).
///   2. Handle prefix bytes (DD, FD, ED) with specialized sub-decoders.
///   3. Dispatch unprefixed opcodes to DecodeUnprefixed().
///
/// For DD/FD prefixes, only JP (IX) / JP (IY) are control flow instructions
/// (opcode 0xE9 after the prefix). The target comes from the IX/IY register.
///
/// For ED prefix, only RETI/RETN variants exist as control flow instructions.
bool Z80ControlFlowDecoder::Decode(const uint8_t* bytes, uint16_t pc,
                                    uint8_t flags, uint8_t b_reg, uint16_t sp,
                                    uint16_t hl, uint16_t ix, uint16_t iy,
                                    Memory* memory,
                                    Z80ControlFlowResult& result)
{
    if (!bytes)
        return false;

    uint8_t byte0 = bytes[0];

    // Fast reject: if first byte can't start a control flow instruction, bail out
    if (!s_cfOpcodeTable[byte0])
        return false;

    // Handle prefix bytes
    switch (byte0)
    {
        case 0xDD:  // IX prefix
        {
            uint8_t second_byte = bytes[1];
            if (second_byte == 0xE9)    // JP (IX)
            {
                result.type = Z80CFType::JP;
                result.target_addr = ix;
                result.taken = true;
                result.instruction_len = 2;
                return true;
            }
            return false;  // No other DD-prefixed CF instructions
        }

        case 0xFD:  // IY prefix
        {
            uint8_t second_byte = bytes[1];
            if (second_byte == 0xE9)    // JP (IY)
            {
                result.type = Z80CFType::JP;
                result.target_addr = iy;
                result.taken = true;
                result.instruction_len = 2;
                return true;
            }
            return false;  // No other FD-prefixed CF instructions
        }

        case 0xED:  // Extended prefix
            return DecodeED(bytes[1], sp, memory, result);

        default:
            // Unprefixed opcode
            return DecodeUnprefixed(byte0, bytes, pc, flags, b_reg, sp, hl, memory, result);
    }
}
